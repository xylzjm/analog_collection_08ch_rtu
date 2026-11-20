/**
 * @file test_modbus_data.c
 * @brief Callback-level tests of the write transaction and persistence.
 *
 * Builds a real (unstarted) slave core over fake line/timer/critical ports
 * and drives its data-space callbacks directly with the in-memory FlashDB
 * fake - no frames, no dispatch of the AO under test - so the whole
 * transaction discipline runs exactly as the core would invoke it: validate,
 * commit (through the real persistence code over the fdb_fake medium with a
 * read-back check), then mirror, then publish. Publishes are observed on a
 * recorder AO. Boot loading (the initial transition) and the CFG_APPLIED_SIG
 * handler live at the event level in test_modbus_ao.c.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qpc.h"

#include "app.h"
#include "fdb_fake.h"
#include "flashdb.h"
#include "modbus_ao.h"
#include "modbus_port_fake.h"
#include "reg_map.h"

static int checks;
static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        ++checks;                                                  \
        if ( !(cond) ) {                                           \
            ++failures;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while ( 0 )

/*----------------------------------------------------------------------------*/
/* FlashDB fake medium                                                        */
/*----------------------------------------------------------------------------*/

/** @brief Factory fixture body for the fake-medium contract checks. */
static const uint8_t fixture_body[4] = {0x11U, 0x22U, 0x33U, 0x44U};

/* Same vendor-boundary casts as modbus_ao.c: the bodies stay const. */
static struct fdb_default_kv_node fixture_nodes[2] = {
    {(char*)"cfg/ch0", (void*)fixture_body, sizeof(fixture_body)},
    {(char*)"dev/addr", (void*)fixture_body, sizeof(fixture_body)},
};

/* One zero-length row: the vendor reads value_len == 0 as a C string. */
static struct fdb_default_kv_node str_nodes[1] = {
    {(char*)"str/k", (void*)"abc", 0},
};

/** @brief FlashDB database name (diagnostics only), as modbus_ao.c names it. */
#define PARAM_DB_NAME "param"

/**
 * @brief Pin the contract of the fdb_fake medium.
 *
 * The fake is load-bearing for every persistence check in this suite, so
 * its faithfulness rules are pinned here next to its main consumer:
 * fdb_blob_make() leaves saved alone; get reports the full stored length
 * (0 on miss) and copies min(len, size); set counts and records before
 * honouring fail_write; init materializes the default table only into an
 * empty medium, reads a zero-length row as a C string, and is not a set;
 * a hole is never backfilled; a closed database answers like the vendor's
 * db_init_ok guards; a NULL body deletes the key.
 */
static void test_fake_medium(void)
{
    struct fdb_blob blob;
    struct fdb_default_kv fixture;
    struct fdb_default_kv str_fixture;
    uint8_t preset[2]  = {0xA5U, 0x5AU};
    uint8_t body8[8]   = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
    uint8_t dst[6];
    uint8_t buf[8]     = {0};
    size_t n;

    fixture.kvs = fixture_nodes;
    fixture.num = sizeof(fixture_nodes) / sizeof(fixture_nodes[0]);
    str_fixture.kvs = str_nodes;
    str_fixture.num = sizeof(str_nodes) / sizeof(str_nodes[0]);

    /* fdb_blob_make sets buf/size only; saved stays untouched. */
    blob.saved.len = 0x5A5AU;
    CHECK(fdb_blob_make(&blob, buf, sizeof(buf)) == &blob);
    CHECK(blob.buf == buf);
    CHECK(blob.size == sizeof(buf));
    CHECK(blob.saved.len == 0x5A5AU);

    /* An empty medium materializes the whole default table (factory boot),
     * and materialization is not a set. */
    fdb_fake_reset();
    CHECK(fdb_kvdb_init(NULL, PARAM_DB_NAME, BOARD_PARAM_PARTITION, &fixture, NULL) == FDB_NO_ERR);
    CHECK(fdb_fake.init_calls == 1U);
    CHECK(fdb_fake.set_calls == 0U);

    /* Round trip out of the medium; saved.len is the full stored length. */
    blob.saved.len = 0U;
    n = fdb_kv_get_blob(NULL, "cfg/ch0", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK(n == sizeof(fixture_body));
    CHECK(blob.saved.len == sizeof(fixture_body));
    CHECK(memcmp(buf, fixture_body, sizeof(fixture_body)) == 0);
    blob.saved.len = 0U;
    n = fdb_kv_get_blob(NULL, "dev/addr", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK(n == sizeof(fixture_body));

    /* A preset body wins over the default and a second init neither
     * overwrites nor backfills (factory-boot rule). */
    CHECK(fdb_kv_set_blob(NULL, "cfg/ch0", fdb_blob_make(&blob, preset, sizeof(preset)))
          == FDB_NO_ERR);
    CHECK(fdb_fake.set_calls == 1U);
    CHECK(strcmp(fdb_fake.last_key, "cfg/ch0") == 0);
    CHECK(fdb_fake.last_len == sizeof(preset));
    CHECK(fdb_kvdb_init(NULL, PARAM_DB_NAME, BOARD_PARAM_PARTITION, &fixture, NULL) == FDB_NO_ERR);
    CHECK(fdb_fake.init_calls == 2U);
    CHECK(fdb_fake.set_calls == 1U);
    blob.saved.len = 0U;
    n = fdb_kv_get_blob(NULL, "cfg/ch0", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK(n == sizeof(preset));
    CHECK(memcmp(buf, preset, sizeof(preset)) == 0);

    /* fail_write: the attempt is counted and recorded before it is
     * rejected - last_* hold the FAILED attempt's values - and the
     * targeted entry keeps its old body. */
    fdb_fake.fail_write = 1U;
    CHECK(fdb_kv_set_blob(NULL, "dev/addr", fdb_blob_make(&blob, body8, sizeof(body8)))
          == FDB_WRITE_ERR);
    fdb_fake.fail_write = 0U;
    CHECK(fdb_fake.set_calls == 2U);
    CHECK(strcmp(fdb_fake.last_key, "dev/addr") == 0);
    CHECK(fdb_fake.last_len == sizeof(body8));
    blob.saved.len = 0U;
    n = fdb_kv_get_blob(NULL, "dev/addr", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK(n == sizeof(fixture_body));
    CHECK(memcmp(buf, fixture_body, sizeof(fixture_body)) == 0);

    /* Truncated copy, full length reported, tail of the buffer untouched. */
    CHECK(fdb_kv_set_blob(NULL, "cfg/ch2", fdb_blob_make(&blob, body8, sizeof(body8)))
          == FDB_NO_ERR);
    memset(dst, 0xCC, sizeof(dst));
    blob.saved.len = 0U;
    n = fdb_kv_get_blob(NULL, "cfg/ch2", fdb_blob_make(&blob, dst, 4U));
    CHECK(n == 4U);
    CHECK(blob.saved.len == sizeof(body8));
    CHECK((dst[4] == 0xCCU) && (dst[5] == 0xCCU));

    /* A miss is a miss: nothing copied, saved.len 0. */
    memset(buf, 0x5A, sizeof(buf));
    blob.saved.len = 77U;
    n = fdb_kv_get_blob(NULL, "absent", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK(n == 0U);
    CHECK(blob.saved.len == 0U);
    CHECK(buf[0] == 0x5AU);

    /* A hole is never backfilled: one planted row survives init and the
     * missing default rows stay missing (factory-boot rule). */
    fdb_fake_reset();
    fdb_fake_put("cfg/ch0", preset, sizeof(preset));
    CHECK(fdb_kvdb_init(NULL, PARAM_DB_NAME, BOARD_PARAM_PARTITION, &fixture, NULL) == FDB_NO_ERR);
    blob.saved.len = 0U;
    n = fdb_kv_get_blob(NULL, "cfg/ch0", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK((n == sizeof(preset)) && (memcmp(buf, preset, sizeof(preset)) == 0));
    blob.saved.len = 0U;
    n = fdb_kv_get_blob(NULL, "dev/addr", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK((n == 0U) && (blob.saved.len == 0U));

    /* A zero-length default row is a C string (fdb_kvdb.c fdb_kv_set_default):
     * the stored length is strlen(value), not 0. */
    fdb_fake_reset();
    (void)fdb_kvdb_init(NULL, PARAM_DB_NAME, BOARD_PARAM_PARTITION, &str_fixture, NULL);
    blob.saved.len = 0U;
    n = fdb_kv_get_blob(NULL, "str/k", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK((n == 3U) && (memcmp(buf, "abc", 3U) == 0));

    /* set() with a NULL body deletes the key (fdb_kvdb.c set_kv). */
    fdb_fake_put("cfg/ch4", preset, sizeof(preset));
    (void)fdb_kv_set_blob(NULL, "cfg/ch4", fdb_blob_make(&blob, NULL, 0U));
    blob.saved.len = 0U;
    n = fdb_kv_get_blob(NULL, "cfg/ch4", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK(n == 0U);

    /* A closed database answers like the vendor's db_init_ok guards: get
     * reports a miss without touching saved.len, set answers FDB_INIT_FAILED
     * before any attempt is recorded. */
    fdb_fake_reset();
    fdb_fake.fail_init = 1U;
    CHECK(fdb_kvdb_init(NULL, PARAM_DB_NAME, BOARD_PARAM_PARTITION, &fixture, NULL) == FDB_INIT_FAILED);
    blob.saved.len = 77U;
    n = fdb_kv_get_blob(NULL, "cfg/ch0", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK((n == 0U) && (blob.saved.len == 77U));
    CHECK(fdb_kv_set_blob(NULL, "cfg/ch0", fdb_blob_make(&blob, preset, sizeof(preset)))
          == FDB_INIT_FAILED);
    CHECK(fdb_fake.set_calls == 0U);
    fdb_fake.fail_init = 0U;
}

/*----------------------------------------------------------------------------*/
/* Harness                                                                    */
/*----------------------------------------------------------------------------*/

static modbus_ao_t ao;
static modbus_slave_t slave_storage;
static modbus_port_fake_t port;
static uint8_t rx_buffer[256];
static uint8_t tx_buffer[256];

static QSubscrList subscr[MAX_SIG];
static QF_MPOOL_EL(measure_evt_t) pool_a[APP_POOL_A_BLOCK_NUM];
static QF_MPOOL_EL(config_set_evt_t) pool_b[APP_POOL_B_BLOCK_NUM];

/* Publish observer for CONFIG_SET_SIG. */
static QActive recorder;
static QEvtPtr rec_queue[32];
static uint8_t rec_started;

/** @brief Configurations published since setup, in order. */
static struct {
    uint8_t ch;
    ch_config_t cfg;
} published[16];
static uint16_t published_count;

static QState rec_state(QHsm* const me, QEvt const* const e)
{
    (void)me;
    (void)e;
    return Q_SUPER(&QHsm_top);
}

static QState rec_handler(QHsm* const me, QEvt const* const e)
{
    (void)me;
    (void)e;
    return Q_TRAN(&rec_state);
}

/** @brief Drain the recorder and collect CONFIG_SET payloads. */
static void rec_collect(void)
{
    while ( !QEQueue_isEmpty(&recorder.eQueue) ) {
        QEvt const* e = QActive_get_(&recorder);

        if ( e->sig == CONFIG_SET_SIG ) {
            config_set_evt_t const* c = Q_EVT_CAST(config_set_evt_t);

            published[published_count].ch  = c->ch;
            published[published_count].cfg = c->cfg;
            ++published_count;
        }
        QF_gc(e);
    }
}

/**
 * @brief Reset the world and construct the AO over a fresh fake medium.
 *
 * @param db_fail Non-zero scripts a database init failure (b3 path).
 */
static void setup(uint8_t db_fail)
{
    modbus_slave_config_t cfg = {0};

    if ( rec_started != 0U ) {
        QActive_unregister_(&recorder);
    }
    memset(subscr, 0, sizeof(subscr));
    QActive_psInit(subscr, MAX_SIG);

    fdb_fake_reset();
    fdb_fake.fail_init = db_fail;

    /* The observer must be up before any write can publish. */
    QActive_ctor(&recorder, Q_STATE_CAST(&rec_handler));
    QActive_start(&recorder, Q_PRIO(30U, 0U),
                  rec_queue, 32U, (void*)0, 0U, (void*)0);
    rec_started = 1U;
    QActive_subscribe(&recorder, CONFIG_SET_SIG);

    published_count = 0U;

    /* A real but unstarted slave core: wiring() drives its callbacks. */
    modbus_port_fake_init(&port);
    modbus_port_fake_config(&port, &cfg,
                            rx_buffer, (uint16_t)sizeof(rx_buffer),
                            tx_buffer, (uint16_t)sizeof(tx_buffer));

    modbus_ao_ctor(&ao, &slave_storage, &cfg);
}

static void setup_normal(void)
{
    setup(0U);
}

/** @brief The core's own callback copy, wired to the AO under test. */
static const modbus_slave_callbacks_t* wiring(void)
{
    const modbus_slave_callbacks_t* cb = &slave_storage.callbacks;

    CHECK(cb->ctx == &ao);
    CHECK(cb->holding_registers != NULL);
    CHECK(cb->input_registers != NULL);
    CHECK(cb->coils != NULL);
    CHECK(cb->discrete_inputs != NULL);
    CHECK(cb->address != NULL);
    return cb;
}

/** @brief Write @p count registers starting at @p start into a block. */
static modbus_slave_exception_t write_regs(const modbus_slave_callbacks_t* cb,
                                           uint16_t start,
                                           uint16_t count,
                                           const uint8_t* bytes)
{
    return cb->holding_registers(cb->ctx, start, count,
                                 (uint8_t*)bytes, MODBUS_SLAVE_REG_WRITE);
}

/** @brief Read @p count registers starting at @p start. */
static modbus_slave_exception_t read_regs(const modbus_slave_callbacks_t* cb,
                                          uint16_t start,
                                          uint16_t count,
                                          uint8_t* bytes)
{
    return cb->holding_registers(cb->ctx, start, count,
                                 bytes, MODBUS_SLAVE_REG_READ);
}

/** @brief Build one channel configuration block in wire order. */
static void make_block(uint8_t type,
                       uint8_t tc_type,
                       uint16_t rate_code,
                       float linear_min,
                       float linear_max,
                       float offset,
                       float gain,
                       uint8_t* block)
{
    ch_config_t cfg;

    cfg.type       = type;
    cfg.tc_type    = tc_type;
    cfg.rate_code  = rate_code;
    cfg.linear_min = linear_min;
    cfg.linear_max = linear_max;
    cfg.offset     = offset;
    cfg.gain       = gain;
    reg_map_ch_encode(&cfg, block);
}

/** @brief Decode the body of the last fake-medium write as a configuration. */
static ch_config_t last_cfg(void)
{
    ch_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    if ( fdb_fake.last_len <= sizeof(cfg) ) {
        memcpy(&cfg, fdb_fake.last_value, fdb_fake.last_len);
    }
    return cfg;
}

/** @brief Read the device fault word through the input callback. */
static uint16_t read_fault(const modbus_slave_callbacks_t* cb)
{
    uint8_t reply[2];

    CHECK(cb->input_registers(cb->ctx, REG_MAP_SYS_BASE + 4U, 1U, reply,
                              MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_NONE);
    return modbus_slave_get_u16(reply);
}

/*----------------------------------------------------------------------------*/
/* Boot state of the callback level                                           */
/*----------------------------------------------------------------------------*/

/** @brief A fresh mirror answers from the constructor defaults. */
static void test_init_state(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t one[2];

    setup_normal();
    cb = wiring();

    CHECK(fdb_fake.init_calls == 1U);
    CHECK(fdb_fake.set_calls == 0U); /* materialization is not a commit */
    CHECK(ao.store.db_ok == 1U);
    CHECK(ao.store.commit_failed == 0U);
    CHECK(ao.config.addr == REG_MAP_DEFAULT_DEV_ADDR);
    CHECK(cb->address(cb->ctx) == (uint8_t)REG_MAP_DEFAULT_DEV_ADDR);
    CHECK(ao.telemetry.status[0] == (uint16_t)CH_STATUS_UNCONFIGURED);
    CHECK(ao.config.applied[0] == 0U);
    CHECK(ao.config.cfg_ok[0] == 0U);

    /* Unmapped spaces stay honest. */
    CHECK(cb->coils(cb->ctx, 0U, 1U, NULL, MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_ILLEGAL_FUNCTION);
    CHECK(cb->discrete_inputs(cb->ctx, 0U, 1U, NULL, MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_ILLEGAL_FUNCTION);
    CHECK(read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block) == MODBUS_SLAVE_EX_NONE);
    CHECK(modbus_slave_get_u16(&block[0]) == 0U);

    /* Input registers are read-only. */
    CHECK(cb->input_registers(cb->ctx, 0U, 1U, one, MODBUS_SLAVE_REG_WRITE)
          == MODBUS_SLAVE_EX_ILLEGAL_FUNCTION);
}

/*----------------------------------------------------------------------------*/
/* Validation matrix (EX 03 path)                                             */
/*----------------------------------------------------------------------------*/

/** @brief An invalid candidate rejects the whole frame and touches nothing. */
static void test_validation_matrix(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t before[REG_MAP_CH_CFG_LEN * 2U];
    uint16_t i;

    static const struct {
        uint8_t reg;   /* register to corrupt */
        uint16_t word; /* wire word to plant */
    } rejects[] = {
        {0U, 11U},     /* composite type code with no input type */
        {0U, 0xFFFFU}, /* unknown composite code -> invalid sentinel */
        {0U, 0x63U},   /* type code 99: no such input type */
        {1U, 13U},     /* rate code past the last tier */
        {1U, 0xFFFFU}, /* rate code without a tier */
    };

    setup_normal();
    cb = wiring();

    /* A valid block first, so the reject has a mirror to leave untouched. */
    make_block((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U,
               0.0f, 0.0f, 0.0f, 1.0f, block);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block) == MODBUS_SLAVE_EX_NONE);
    read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, before);

    for ( i = 0U; i < (sizeof(rejects) / sizeof(rejects[0])); ++i ) {
        memcpy(block, before, sizeof(block));
        modbus_slave_put_u16(&block[2U * rejects[i].reg], rejects[i].word);
        CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
              == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);
        read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block);
        CHECK(memcmp(block, before, sizeof(block)) == 0);
    }
    CHECK(fdb_fake.set_calls == 1U); /* only the valid frame above */
    rec_collect();
    CHECK(published_count == 1U);

    /* NaN / Inf in the calibration fields reject as well. */
    {
        static const struct {
            uint8_t reg;
            uint16_t hi;
            uint16_t lo;
        } bad_floats[] = {
            {2U, 0x7FC0U, 0x0000U}, /* NaN linear_min */
            {4U, 0x7F80U, 0x0000U}, /* +Inf linear_max */
            {6U, 0xFF80U, 0x0000U}, /* -Inf offset */
            {8U, 0x7FC0U, 0x0000U}, /* NaN gain */
        };

        for ( i = 0U; i < (sizeof(bad_floats) / sizeof(bad_floats[0])); ++i ) {
            memcpy(block, before, sizeof(block));
            modbus_slave_put_u16(&block[2U * bad_floats[i].reg], bad_floats[i].hi);
            modbus_slave_put_u16(&block[2U * bad_floats[i].reg + 2U], bad_floats[i].lo);
            CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
                  == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);
        }
    }
    CHECK(fdb_fake.set_calls == 1U);
    rec_collect();
    CHECK(published_count == 1U);

    /* An out-of-span range is a mapping error, not a value error. */
    CHECK(write_regs(cb, 10U, 1U, before) == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
    CHECK(write_regs(cb, 0U, 11U, before) == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
    CHECK(cb->input_registers(cb->ctx, 72U, 1U, before, MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
}

/*----------------------------------------------------------------------------*/
/* Write transaction                                                          */
/*----------------------------------------------------------------------------*/

/** @brief A full-block write commits, mirrors, publishes and reads back. */
static void test_write_commit(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t echo[REG_MAP_CH_CFG_LEN * 2U];
    ch_config_t saved;

    setup_normal();
    cb = wiring();

    make_block((uint8_t)CH_TYPE_4_20MA, CH_TC_TYPE_NONE, 9U,
               4.0f, 20.0f, 0.5f, 2.0f, block);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block) == MODBUS_SLAVE_EX_NONE);

    /* The commit happened once and the KV body is the encoded block. */
    CHECK(fdb_fake.set_calls == 1U);
    CHECK(strcmp(fdb_fake.last_key, "cfg/ch0") == 0);
    CHECK(fdb_fake.last_len == sizeof(ch_config_t));
    saved = last_cfg();
    CHECK(saved.type == (uint8_t)CH_TYPE_4_20MA);
    CHECK(saved.rate_code == 9U);
    CHECK(saved.linear_max == 20.0f);
    CHECK(saved.gain == 2.0f);

    /* Mirror and change-check base moved with the commit. */
    CHECK(ao.config.cfg[0].type == (uint8_t)CH_TYPE_4_20MA);
    CHECK(ao.config.cfg[0].gain == 2.0f);
    CHECK(ao.config.cfg_ok[0] == 1U);
    CHECK(ao.config.applied[0] == 0U); /* persisted, not yet applied */
    CHECK(ao.store.commit_failed == 0U);

    /* One publish, carrying the committed configuration. */
    rec_collect();
    CHECK(published_count == 1U);
    CHECK(published[0].ch == 0U);
    CHECK(published[0].cfg.type == (uint8_t)CH_TYPE_4_20MA);
    CHECK(published[0].cfg.rate_code == 9U);

    /* The next read returns exactly what was written. */
    CHECK(read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, echo) == MODBUS_SLAVE_EX_NONE);
    CHECK(memcmp(echo, block, sizeof(block)) == 0);

    /* The write transaction is also the normalization point: the
     * non-linear configuration's linear limits come back zeroed (the
     * documented ch_config_t contract). */
    make_block((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_T, 12U,
               -1.0f, 9.0f, -0.25f, 0.99f, block);
    CHECK(write_regs(cb, 16U, REG_MAP_CH_CFG_LEN, block) == MODBUS_SLAVE_EX_NONE);
    make_block((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_T, 12U,
               0.0f, 0.0f, -0.25f, 0.99f, echo);
    CHECK(read_regs(cb, 16U, REG_MAP_CH_CFG_LEN, block) == MODBUS_SLAVE_EX_NONE);
    CHECK(memcmp(block, echo, sizeof(block)) == 0);
    CHECK(ao.config.cfg[1].linear_min == 0.0f);
    CHECK(ao.config.cfg[1].linear_max == 0.0f);
    CHECK(ao.config.cfg[1].offset == -0.25f);
    rec_collect();
    CHECK(published_count == 2U);
    CHECK(published[1].ch == 1U);
}

/** @brief Single-register writes land through the read-modify-write. */
static void test_write_single_register(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t one[2];
    uint8_t echo[REG_MAP_CH_CFG_LEN * 2U];

    setup_normal();
    cb = wiring();

    /* Start from 50 Hz (a change from anything, so it commits). */
    make_block((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 9U,
               0.0f, 0.0f, 0.0f, 1.0f, block);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block) == MODBUS_SLAVE_EX_NONE);

    /* Register 1 is the rate code: 10 Hz (code 2); all else survives. */
    modbus_slave_put_u16(one, 2U);
    CHECK(write_regs(cb, 1U, 1U, one) == MODBUS_SLAVE_EX_NONE);
    CHECK(ao.config.cfg[0].rate_code == 2U);
    CHECK(ao.config.cfg[0].type == (uint8_t)CH_TYPE_TC);
    CHECK(ao.config.cfg[0].gain == 1.0f);

    CHECK(read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, echo) == MODBUS_SLAVE_EX_NONE);
    CHECK(modbus_slave_get_u16(&echo[2]) == 2U);
    rec_collect();
    CHECK(published_count == 2U);

    /* A type write re-maps tc_type along with it and still commits. */
    modbus_slave_put_u16(one, 8U);
    CHECK(write_regs(cb, 0U, 1U, one) == MODBUS_SLAVE_EX_NONE);
    CHECK(ao.config.cfg[0].type == (uint8_t)CH_TYPE_PT100);
    CHECK(ao.config.cfg[0].tc_type == CH_TC_TYPE_NONE);
    CHECK(ao.config.cfg[0].rate_code == 2U);

    /* A single invalid register rejects the whole candidate. */
    modbus_slave_put_u16(one, 13U);
    CHECK(write_regs(cb, 1U, 1U, one) == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);
    CHECK(ao.config.cfg[0].rate_code == 2U);
}

/** @brief The same-value write short-circuits storage but re-notifies. */
static void test_no_change_retries(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];

    setup_normal();
    cb = wiring();

    make_block((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 9U,
               0.0f, 0.0f, 0.0f, 1.0f, block);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block) == MODBUS_SLAVE_EX_NONE);
    CHECK(fdb_fake.set_calls == 1U);
    rec_collect();
    CHECK(published_count == 1U);

    /* Same value while unapplied: no second write, but the channel gets a
     * fresh CONFIG_SET so a failed apply can be retried by rewriting the
     * same block. Once the channel answers with CFG_APPLIED_SIG the same
     * value is a true no-op (covered in test_modbus_ao.c). */
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block) == MODBUS_SLAVE_EX_NONE);
    CHECK(fdb_fake.set_calls == 1U);
    rec_collect();
    CHECK(published_count == 2U);
    CHECK(published[1].ch == 0U);

    /* The same short-circuit guards the address register. */
    {
        uint8_t one[2];

        modbus_slave_put_u16(one, 1U);
        CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one) == MODBUS_SLAVE_EX_NONE);
        CHECK(fdb_fake.set_calls == 2U); /* first address write commits */
        CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one) == MODBUS_SLAVE_EX_NONE);
        CHECK(fdb_fake.set_calls == 2U); /* same value: store untouched */
    }
    rec_collect();
    CHECK(published_count == 2U); /* address writes never publish */
}

/** @brief A wrong read-back is not committed: EX 04, b1, retry succeeds. */
static void test_commit_failure(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t before[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t next_block[REG_MAP_CH_CFG_LEN * 2U];

    setup_normal();
    cb = wiring();

    make_block((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 9U,
               0.0f, 0.0f, 0.0f, 1.0f, block);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block) == MODBUS_SLAVE_EX_NONE);
    read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, before);

    /* A changed value whose read-back is wrong is not committed. */
    make_block((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 10U,
               0.0f, 0.0f, 0.0f, 3.0f, next_block);
    fdb_fake.corrupt_read = 1U;
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, next_block)
          == MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE);
    fdb_fake.corrupt_read = 0U;
    CHECK(ao.store.commit_failed != 0U);
    CHECK(ao.config.cfg[0].rate_code == 9U); /* change-check base untouched */
    read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block);
    CHECK(memcmp(block, before, sizeof(block)) == 0);
    rec_collect();
    CHECK(published_count == 1U);

    /* The latch is visible through the fault word (b2 is inherent to this
     * level: without a boot load cfg_ok stays 0, b0 needs a CJC heartbeat). */
    CHECK((read_fault(cb) & REG_MAP_FAULT_PARAM_WRITE) != 0U);

    /* The retry is not short-circuited (the change-check base never moved). */
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, next_block) == MODBUS_SLAVE_EX_NONE);
    CHECK(fdb_fake.set_calls == 3U);
    CHECK(ao.store.commit_failed == 0U);
    CHECK((read_fault(cb) & REG_MAP_FAULT_PARAM_WRITE) == 0U);
    rec_collect();
    CHECK(published_count == 2U);

    /* A failed write behaves like a wrong read-back. */
    make_block((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 11U,
               0.0f, 0.0f, 0.0f, 1.0f, block);
    fdb_fake.fail_write = 1U;
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
          == MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE);
    fdb_fake.fail_write = 0U;
    CHECK(ao.store.commit_failed != 0U);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block) == MODBUS_SLAVE_EX_NONE);
    CHECK(fdb_fake.set_calls == 5U);
    CHECK(ao.store.commit_failed == 0U);
}

/*----------------------------------------------------------------------------*/
/* Device register                                                            */
/*----------------------------------------------------------------------------*/

/** @brief Address writes commit, validate bounds and fail like configs. */
static void test_address_register(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t one[2];
    uint8_t echo[2];

    setup_normal();
    cb = wiring();

    modbus_slave_put_u16(one, 247U);
    CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one) == MODBUS_SLAVE_EX_NONE);
    CHECK(ao.config.addr == 247U);
    CHECK(ao.config.addr_ok == 1U);
    CHECK(cb->address(cb->ctx) == 247U);
    CHECK(fdb_fake.set_calls == 1U);
    CHECK(strcmp(fdb_fake.last_key, "dev/addr") == 0);
    rec_collect();
    CHECK(published_count == 0U); /* address writes never publish */

    /* The address legality matrix (register 128 write validation). */
    modbus_slave_put_u16(one, 0U);
    CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);
    modbus_slave_put_u16(one, 248U);
    CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);
    modbus_slave_put_u16(one, 0xFFFFU);
    CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);
    CHECK(ao.config.addr == 247U);

    CHECK(read_regs(cb, REG_MAP_DEV_BASE, 1U, echo) == MODBUS_SLAVE_EX_NONE);
    CHECK(modbus_slave_get_u16(echo) == 247U);

    /* A failing address commit behaves like a failing config commit. */
    modbus_slave_put_u16(one, 200U);
    fdb_fake.corrupt_read = 1U;
    CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one)
          == MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE);
    fdb_fake.corrupt_read = 0U;
    CHECK(ao.store.commit_failed != 0U);
    CHECK(ao.config.addr == 247U); /* not adopted */
    CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one) == MODBUS_SLAVE_EX_NONE);
    CHECK(ao.store.commit_failed == 0U);
    CHECK(ao.config.addr == 200U);
}

/*----------------------------------------------------------------------------*/
/* Store lifecycle and fault word                                             */
/*----------------------------------------------------------------------------*/

/** @brief A failed database open fails safe (EX 04, b3, no b2). */
static void test_init_failure(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t one[2];

    /* The database never opened: b3 latched and nothing materialized. */
    setup(1U);
    cb = wiring();
    CHECK(fdb_fake.init_calls == 1U);
    CHECK(ao.store.db_ok == 0U);
    CHECK(read_fault(cb) == (uint16_t)(REG_MAP_FAULT_FDB_INIT | REG_MAP_FAULT_CJC));

    /* Every commit fails safe: EX 04, b1 latched, the store untouched. */
    make_block((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 9U,
               0.0f, 0.0f, 0.0f, 1.0f, block);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
          == MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE);
    CHECK(fdb_fake.set_calls == 0U);
    CHECK(ao.store.commit_failed != 0U);
    CHECK(read_fault(cb) == (uint16_t)(REG_MAP_FAULT_FDB_INIT
                                       | REG_MAP_FAULT_PARAM_WRITE
                                       | REG_MAP_FAULT_CJC));
    CHECK(ao.store.db_ok == 0U);

    /* An address commit behaves exactly the same. */
    modbus_slave_put_u16(one, 247U);
    CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one)
          == MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE);
    CHECK(read_fault(cb) == (uint16_t)(REG_MAP_FAULT_FDB_INIT
                                       | REG_MAP_FAULT_PARAM_WRITE
                                       | REG_MAP_FAULT_CJC));
}

/**
 * @brief Telemetry mirrors encode through the input callback.
 *
 * The mirror fields are written by the MEASURE_SIG/CJC_SIG handlers at the
 * event level; this level pokes them to prove the encoding path.
 */
static void test_telemetry_mirror(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t reply[8];
    uint32_t bits;
    float value;

    setup_normal();
    cb = wiring();

    ao.telemetry.value[1]   = 42.5f;
    ao.telemetry.status[1]  = (uint16_t)CH_STATUS_OK;
    ao.telemetry.cjc_temp   = 25.0f;
    ao.telemetry.cjc_valid  = 1U;

    /* Channel 2 telemetry block: registers 8..10. */
    CHECK(cb->input_registers(cb->ctx, 8U, 3U, reply, MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_NONE);
    bits = ((uint32_t)modbus_slave_get_u16(&reply[0]) << 16)
           | (uint32_t)modbus_slave_get_u16(&reply[2]);
    memcpy(&value, &bits, sizeof(value));
    CHECK(value == 42.5f);
    CHECK(modbus_slave_get_u16(&reply[4]) == (uint16_t)CH_STATUS_OK);

    /* With a valid cold junction the fault word is clear at this level:
     * cfg_ok/addr_ok are still 0 (no boot load here), so b2 shows. */
    CHECK(read_fault(cb) == (uint16_t)REG_MAP_FAULT_PARAM_LOAD);
}

/*----------------------------------------------------------------------------*/

int main(void)
{
    QF_init();
    QActive_psInit(subscr, MAX_SIG);
    QF_poolInit(pool_a, sizeof(pool_a), sizeof(pool_a[0]));
    QF_poolInit(pool_b, sizeof(pool_b), sizeof(pool_b[0]));

    test_fake_medium();
    test_init_state();
    test_validation_matrix();
    test_write_commit();
    test_write_single_register();
    test_no_change_retries();
    test_commit_failure();
    test_address_register();
    test_init_failure();
    test_telemetry_mirror();

    printf("%d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
