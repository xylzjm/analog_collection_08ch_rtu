/**
 * @file modbus_ao.c
 * @brief Modbus RTU slave active object: mirror, write transaction, storage.
 *
 * This AO owns the typed mirror of the Modbus data space, the
 * holding-register write transaction and the parameter persistence; the
 * modbus core calls the data-space callbacks directly in this AO's
 * execution context. reg_map is the pure wire map it calls. The write
 * transaction holds the project's atomicity discipline - validate, commit
 * into the FlashDB parameter database with a read-back check, then touch
 * the mirror - and a committed channel configuration is published as
 * CONFIG_SET_SIG.
 */

#include "modbus_ao.h"

#include <float.h>
#include <stdbool.h>
#include <string.h>

#include "elog.h"
#include "flashdb.h"
#include "reg_map.h"

Q_DEFINE_THIS_MODULE("modbus_ao")

/* Static events posted from the ISR event port (QEVT_INITIALIZER, ROM). */
static QEvt const frameEvt     = QEVT_INITIALIZER(FRAME_SIG);
static QEvt const txDoneEvt    = QEVT_INITIALIZER(TX_DONE_SIG);
static QEvt const txTimeoutEvt = QEVT_INITIALIZER(TX_TIMEOUT_SIG);

static void publish_cfg(modbus_ao_t* me, uint8_t ch, const ch_config_t* cfg);
static QState modbus_ao_initial(modbus_ao_t* const me, void const* const par);
static QState modbus_ao_serving(modbus_ao_t* const me, QEvt const* const e);

/*----------------------------------------------------------------------------*/
/* Fault word                                                                 */
/*----------------------------------------------------------------------------*/

/** @brief Synthesize the device fault word register value. */
static uint16_t fault_word(const modbus_ao_t* me)
{
    uint16_t latched = 0U;
    uint8_t ch;

    if ( me->store.commit_failed != 0U ) {
        latched |= REG_MAP_FAULT_PARAM_WRITE;
    }
    /* b2 only exists while the store is up: a dead store reports b3 alone. */
    if ( me->store.db_ok != 0U ) {
        uint8_t load_failed = (me->config.addr_ok == 0U) ? 1U : 0U;

        for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
            if ( me->config.cfg_ok[ch] == 0U ) {
                load_failed = 1U;
                break;
            }
        }
        if ( load_failed != 0U ) {
            latched |= REG_MAP_FAULT_PARAM_LOAD;
        }
    }
    if ( me->store.db_ok == 0U ) {
        latched |= REG_MAP_FAULT_FDB_INIT;
    }
    return reg_map_fault_word(me->telemetry.cjc_valid, latched);
}

/*----------------------------------------------------------------------------*/
/* Persistence                                                                */
/*----------------------------------------------------------------------------*/

/** @brief Result of one synchronous save. */
typedef enum {
    COMMIT_OK       = 0, /**< Read-back equals the requested value; committed. */
    COMMIT_NO_CHANGE = 1, /**< Same value as the snapshot; store untouched. */
    COMMIT_ERR      = -1 /**< Read-back differs; treat as not committed. */
} commit_status_t;

/** @brief Factory default channel configuration: TC type K, 10 Hz. */
static const ch_config_t default_ch_cfg = {
    (uint8_t)CH_TYPE_TC,   /**< type */
    (uint8_t)CH_TC_TYPE_K, /**< tc_type */
    2U,                    /**< rate_code: 10 Hz */
    0.0f,                  /**< linear_min (unused for TC) */
    0.0f,                  /**< linear_max (unused for TC) */
    0.0f,                  /**< offset */
    1.0f,                  /**< gain */
};

/** @brief Factory default Modbus slave address. */
static const uint16_t default_dev_addr = REG_MAP_DEFAULT_DEV_ADDR;

/** @brief FlashDB database name (diagnostics only). */
#define PARAM_DB_NAME "param"

/** @brief The one parameter database; owned here, opened in the ctor. */
static struct fdb_kvdb param_db;

/**
 * @brief Default parameter table: the single source of keys and default bodies.
 *
 * Rows are fdb_default_kv_node so fdb_kvdb_init() consumes them directly.
 * The key set is the 8-position board layout even when fewer channels are
 * populated, so a SKU variant keeps the same KV layout. The last entry is
 * the device address.
 *
 * Vendor type forces non-const node fields; the bodies stay const (.rodata).
 * The initializer casts are the one vendor-boundary compromise. Nothing ever
 * writes the array: fdb_kvdb_init() retains the kvs pointer for the DB
 * lifetime, which is why the array is static.
 */
static struct fdb_default_kv_node param_defaults[] = {
#define X(pos) {(char*)"cfg/ch" #pos, (void*)&default_ch_cfg, sizeof(default_ch_cfg)},
    BOARD_POSITIONS(X)
#undef X
    {(char*)"dev/addr", (void*)&default_dev_addr, sizeof(default_dev_addr)},
};

/* Tripwire: deleting the dev/addr row would push key_addr() out of bounds.
 * Derived-vs-derived: the schema row count is not hand-maintained. */
Q_ASSERT_STATIC((sizeof(param_defaults) / sizeof(param_defaults[0]))
                == BOARD_POSITION_COUNT + 1U);

/** @brief KV key of one channel configuration row. */
static const char* key_cfg(uint8_t ch)
{
    return param_defaults[ch].key;
}

/** @brief KV key of the device address row. */
static const char* key_addr(void)
{
    return param_defaults[BOARD_POSITION_COUNT].key;
}

/**
 * @brief Read one key's body from the parameter database.
 *
 * Owns the fdb_blob lifecycle and the saved.len contract: FlashDB reports
 * the full stored length even when the copy was truncated, and 0 means the
 * key is missing. blob.saved.len is pre-zeroed because fdb_blob_make() does
 * not touch the saved block. No default substitution ever happens here.
 *
 * @param key NUL-terminated key.
 * @param buf Destination buffer.
 * @param cap Capacity of @p buf.
 * @param stored_len Receives the full stored length (0 when the key is missing).
 * @return true when the key exists, false when it is missing.
 */
static bool param_read(const char* key, void* buf, size_t cap, size_t* stored_len)
{
    struct fdb_blob blob;

    blob.saved.len = 0U;
    (void)fdb_kv_get_blob(&param_db, key, fdb_blob_make(&blob, buf, cap));
    *stored_len = blob.saved.len;
    return blob.saved.len != 0U;
}

/**
 * @brief Commit one body to the parameter database synchronously.
 *
 * Write, then read back unconditionally; the outcome is decided by the
 * read-back comparison, never by the library return code (which is only
 * logged). This is the one home of "commit = write then read back".
 *
 * @param key NUL-terminated key.
 * @param body Body to store.
 * @param len Body length in bytes.
 * @return COMMIT_OK only when the stored bytes equal the requested ones,
 *         COMMIT_ERR otherwise.
 */
static commit_status_t param_commit(const char* key, const void* body, size_t len)
{
    uint8_t verify[sizeof(ch_config_t)]; /* the longest body */
    struct fdb_blob blob;
    fdb_err_t st;
    size_t stored_len;

    st = fdb_kv_set_blob(&param_db, key, fdb_blob_make(&blob, body, len));
    /* Consumed by the log lines below; the host elog stub compiles them out.
     * Only the read-back decides the outcome. */
    (void)st;

    /* A body longer than the verify buffer can never be confirmed equal. */
    if ( !param_read(key, verify, sizeof(verify), &stored_len)
         || (stored_len != len)
         || (len > sizeof(verify))
         || (memcmp(verify, body, len) != 0) ) {
        elog_error("modbus_ao", "commit failed: %s (fdb %d, read back %u)",
                   key, (int)st, (unsigned)stored_len);
        return COMMIT_ERR;
    }
    elog_info("modbus_ao", "commit ok: %s (fdb %d)", key, (int)st);
    return COMMIT_OK;
}

/**
 * @brief Report whether a float holds a finite value.
 *
 * @param x Value to test.
 * @return true for finite values, false for NaN and either infinity.
 */
static bool f32_is_finite(float x)
{
    return (x == x) && (x >= -FLT_MAX) && (x <= FLT_MAX);
}

/**
 * @brief Validate one configuration and normalize it in place.
 *
 * Validation and normalization are one operation so that semantically equal
 * configurations always compare equal byte for byte (the change check in
 * save_cfg relies on memcmp). The contract is documented on ch_config_t.
 *
 * @param cfg Configuration to validate; written only on success.
 * @return true when the configuration is valid (and now normalized), false
 *         when it must be rejected. On false the input is left untouched.
 */
static bool config_validate(ch_config_t* cfg)
{
    ch_config_t norm;

    if ( cfg == NULL ) {
        return false;
    }
    norm = *cfg;

    if ( norm.type > (uint8_t)CH_TYPE_0_10V ) {
        return false;
    }

    if ( norm.type == (uint8_t)CH_TYPE_TC ) {
        if ( norm.tc_type > (uint8_t)CH_TC_TYPE_T ) {
            return false;
        }
    }
    else {
        norm.tc_type = CH_TC_TYPE_NONE;
    }

    if ( norm.rate_code > CH_CONFIG_RATE_CODE_MAX ) {
        return false;
    }

    if ( !f32_is_finite(norm.linear_min) || !f32_is_finite(norm.linear_max)
         || !f32_is_finite(norm.offset) || !f32_is_finite(norm.gain) ) {
        return false;
    }

    if ( norm.type != (uint8_t)CH_TYPE_4_20MA
         && norm.type != (uint8_t)CH_TYPE_0_10V ) {
        norm.linear_min = 0.0f;
        norm.linear_max = 0.0f;
    }

    *cfg = norm;
    return true;
}

/** @brief Report whether a slave address is legal (1..247). */
static bool addr_valid(uint16_t addr)
{
    return (addr >= MODBUS_SLAVE_ADDRESS_MIN) && (addr <= MODBUS_SLAVE_ADDRESS_MAX);
}

/**
 * @brief Read one configuration key and validate its body.
 *
 * @return true when the key exists, has the expected size and validates.
 */
static bool load_cfg(modbus_ao_t* me, uint8_t ch)
{
    const char* key = key_cfg(ch);
    ch_config_t cfg;
    size_t len;

    if ( !param_read(key, &cfg, sizeof(cfg), &len) ) {
        elog_warn("modbus_ao", "load missing: %s", key);
        return false;
    }
    if ( len != sizeof(cfg) ) {
        elog_warn("modbus_ao", "load size: %s (%u != %u)", key,
                  (unsigned)len, (unsigned)sizeof(cfg));
        return false;
    }
    if ( !config_validate(&cfg) ) {
        elog_warn("modbus_ao", "load invalid: %s", key);
        return false;
    }
    me->config.cfg[ch]    = cfg;
    me->config.cfg_ok[ch] = 1U;
    return true;
}

/**
 * @brief Read the slave address key and check its range.
 *
 * @param addr Receives the stored address.
 * @return true when the key exists, has the expected size and is 1..247.
 */
static bool load_addr(modbus_ao_t* me, uint16_t* addr)
{
    const char* key = key_addr();
    size_t len;

    if ( !param_read(key, addr, sizeof(*addr), &len) ) {
        elog_warn("modbus_ao", "load missing: %s", key);
        return false;
    }
    if ( len != sizeof(*addr) ) {
        elog_warn("modbus_ao", "load size: %s (%u != %u)", key,
                  (unsigned)len, (unsigned)sizeof(*addr));
        return false;
    }
    if ( !addr_valid(*addr) ) {
        elog_warn("modbus_ao", "load range: %s (%u)", key, (unsigned)*addr);
        return false;
    }
    return true;
}

/**
 * @brief Load every key into the mirror and the change-check base.
 *
 * Missing, corrupt or semantically invalid keys are load failures and are
 * never replaced by defaults: the affected channel stays unconfigured.
 * A completely empty store is the factory first boot: the default table
 * materialized at construction and loading succeeds.
 */
static void load_all(modbus_ao_t* me)
{
    uint8_t ch;
    uint16_t addr;

    if ( me->store.db_ok == 0U ) {
        return;
    }

    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        me->config.cfg_ok[ch] = 0U;
        (void)load_cfg(me, ch);
    }

    me->config.addr_ok = 0U;
    if ( load_addr(me, &addr) ) {
        me->config.addr    = addr;
        me->config.addr_ok = 1U;
    } else {
        me->config.addr = REG_MAP_DEFAULT_DEV_ADDR;
    }
}

/**
 * @brief Persist one channel configuration synchronously.
 *
 * Change check against the mirror first; a changed value goes through
 * param_commit() (write + read-back), which decides the outcome.
 */
static commit_status_t save_cfg(modbus_ao_t* me,
                                uint8_t ch,
                                const ch_config_t* cfg)
{
    if ( (ch >= CH_CONFIG_CHANNEL_COUNT) || (me->store.db_ok == 0U) ) {
        return COMMIT_ERR;
    }

    if ( (me->config.cfg_ok[ch] != 0U)
         && (memcmp(&me->config.cfg[ch], cfg, sizeof(*cfg)) == 0) ) {
        return COMMIT_NO_CHANGE;
    }

    if ( param_commit(key_cfg(ch), cfg, sizeof(*cfg)) != COMMIT_OK ) {
        return COMMIT_ERR;
    }

    me->config.cfg[ch]    = *cfg;
    me->config.cfg_ok[ch] = 1U;
    return COMMIT_OK;
}

/** @brief Persist the slave address synchronously (same discipline). */
static commit_status_t save_addr(modbus_ao_t* me, uint16_t addr)
{
    if ( (me->store.db_ok == 0U) || !addr_valid(addr) ) {
        return COMMIT_ERR;
    }

    if ( (me->config.addr_ok != 0U) && (me->config.addr == addr) ) {
        return COMMIT_NO_CHANGE;
    }

    if ( param_commit(key_addr(), &addr, sizeof(addr)) != COMMIT_OK ) {
        return COMMIT_ERR;
    }

    me->config.addr    = addr;
    me->config.addr_ok = 1U;
    return COMMIT_OK;
}

/*----------------------------------------------------------------------------*/
/* Data-space callbacks                                                       */
/*----------------------------------------------------------------------------*/

static modbus_slave_exception_t coils(void* ctx,
                                      uint16_t start,
                                      uint16_t count,
                                      uint8_t* buffer,
                                      modbus_slave_register_mode_t mode)
{
    (void)ctx;
    (void)buffer;
    (void)mode;

    /* No coils in this product: the space itself is unmapped. */
    return reg_map_check(REG_SPACE_COIL, start, count, NULL);
}

static modbus_slave_exception_t discrete(void* ctx,
                                         uint16_t start,
                                         uint16_t count,
                                         uint8_t* buffer,
                                         modbus_slave_register_mode_t mode)
{
    (void)ctx;
    (void)buffer;
    (void)mode;

    return reg_map_check(REG_SPACE_DISCRETE, start, count, NULL);
}

static modbus_slave_exception_t input(void* ctx,
                                      uint16_t start,
                                      uint16_t count,
                                      uint8_t* buffer,
                                      modbus_slave_register_mode_t mode)
{
    modbus_ao_t* me       = (modbus_ao_t*)ctx;
    const reg_span_t* span = NULL;
    modbus_slave_exception_t ex;

    ex = reg_map_check(REG_SPACE_INPUT, start, count, &span);
    if ( ex != MODBUS_SLAVE_EX_NONE ) {
        return ex;
    }
    if ( mode != MODBUS_SLAVE_REG_READ ) {
        return MODBUS_SLAVE_EX_ILLEGAL_FUNCTION;
    }

    reg_map_input_encode(me->telemetry.value, me->telemetry.status, me->telemetry.cjc_temp, fault_word(me),
                         span, start, count, buffer);
    return MODBUS_SLAVE_EX_NONE;
}

/**
 * @brief Holding register write pipeline: validate, commit, then update.
 *
 * A configuration write is atomic per frame: nothing is mirrored or
 * reported unless the commit succeeded and the read-back matches.
 */
static modbus_slave_exception_t holding_write(modbus_ao_t* me,
                                              uint16_t start,
                                              const reg_span_t* span,
                                              uint16_t count,
                                              const uint8_t* buffer)
{
    commit_status_t st;

    if ( span->kind == REG_SPAN_DEV ) {
        uint16_t addr = reg_map_dev_decode(buffer);

        if ( !addr_valid(addr) ) {
            return MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE;
        }

        st = save_addr(me, addr);
        if ( st == COMMIT_ERR ) {
            me->store.commit_failed = 1U;
            return MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE;
        }
        if ( st == COMMIT_OK ) {
            me->store.commit_failed = 0U;
        }
        /* COMMIT_NO_CHANGE: mirror already holds the requested value. */
        return MODBUS_SLAVE_EX_NONE;
    }

    {
        uint8_t ch = span->channel;
        uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
        ch_config_t cand;

        /* Read-modify-write: any single register write lands correctly. */
        reg_map_ch_encode(&me->config.cfg[ch], block);
        memcpy(&block[2U * (uint16_t)(start - span->base)], buffer, 2U * count);
        reg_map_ch_decode(block, &cand);
        if ( !config_validate(&cand) ) {
            return MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE;
        }

        st = save_cfg(me, ch, &cand);
        if ( st == COMMIT_ERR ) {
            me->store.commit_failed = 1U;
            return MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE;
        }
        if ( st == COMMIT_OK ) {
            me->store.commit_failed = 0U;
            me->config.applied[ch]  = 0U;
            publish_cfg(me, ch, &cand);
        } else if ( me->config.applied[ch] == 0U ) {
            /* Persisted already, but the channel has not accepted it yet:
             * a same-value rewrite is the retry trigger. */
            publish_cfg(me, ch, &cand);
        }
        return MODBUS_SLAVE_EX_NONE;
    }
}

static modbus_slave_exception_t holding(void* ctx,
                                        uint16_t start,
                                        uint16_t count,
                                        uint8_t* buffer,
                                        modbus_slave_register_mode_t mode)
{
    modbus_ao_t* me       = (modbus_ao_t*)ctx;
    const reg_span_t* span = NULL;
    modbus_slave_exception_t ex;

    ex = reg_map_check(REG_SPACE_HOLDING, start, count, &span);
    if ( ex != MODBUS_SLAVE_EX_NONE ) {
        return ex;
    }

    if ( mode == MODBUS_SLAVE_REG_WRITE ) {
        return holding_write(me, start, span, count, buffer);
    }

    {
        uint8_t block[REG_MAP_CH_CFG_LEN * 2U];

        if ( span->kind == REG_SPAN_CH_CFG ) {
            reg_map_ch_encode(&me->config.cfg[span->channel], block);
        } else {
            reg_map_dev_encode(me->config.addr, block);
        }
        memcpy(buffer, &block[2U * (uint16_t)(start - span->base)], 2U * count);
    }
    return MODBUS_SLAVE_EX_NONE;
}

static uint8_t address(void* ctx)
{
    modbus_ao_t* me = (modbus_ao_t*)ctx;

    return (uint8_t)me->config.addr;
}

/*----------------------------------------------------------------------------*/
/* Configuration publishing                                                   */
/*----------------------------------------------------------------------------*/

/**
 * @brief Publish one CONFIG_SET_SIG event for one channel.
 *
 * Used by both the boot load and the holding-register write pipeline.
 */
static void publish_cfg(modbus_ao_t* me, uint8_t ch, const ch_config_t* cfg)
{
    config_set_evt_t* evt = Q_NEW(config_set_evt_t, CONFIG_SET_SIG);

    (void)me; /* the publish sender; referenced only in Q_SPY builds */

    evt->ch  = ch;
    evt->cfg = *cfg;
    QACTIVE_PUBLISH(&evt->super, Q_ACTIVE_UPCAST(me));
}

/*----------------------------------------------------------------------------*/
/* ISR event port                                                             */
/*----------------------------------------------------------------------------*/

static void on_frame(void* ctx)
{
    modbus_ao_t* me = (modbus_ao_t*)ctx;

    QACTIVE_POST(Q_ACTIVE_UPCAST(me), &frameEvt, (void*)0);
}

static void on_sent(void* ctx)
{
    modbus_ao_t* me = (modbus_ao_t*)ctx;

    QACTIVE_POST(Q_ACTIVE_UPCAST(me), &txDoneEvt, (void*)0);
}

static void on_timeout(void* ctx)
{
    modbus_ao_t* me = (modbus_ao_t*)ctx;

    QACTIVE_POST(Q_ACTIVE_UPCAST(me), &txTimeoutEvt, (void*)0);
}

/*----------------------------------------------------------------------------*/
/* Diagnostics                                                                */
/*----------------------------------------------------------------------------*/

/** @brief CJC heartbeats between two queue water-level diagnostics. */
#define MODBUS_AO_DIAG_PERIOD_HEARTBEATS 60U

/**
 * @brief Report the low-water mark of every started AO's event queue.
 *
 * The capacity argument of docs/qp-application-design.md section 7 rests
 * on scheduling properties (QV dispatches one event per activation and
 * AoModbus is the highest-priority subscriber), not on queue headroom;
 * this diagnostic turns that argument into measured evidence. getQueueMin
 * reports the smallest free space seen since start - the boot burst and
 * the 8x100 Hz soak both leave their footprint in one number per AO.
 */
static void report_queue_watermarks(const modbus_ao_t* me)
{
    uint8_t prio;

    elog_debug("modbus_ao", "tx done=%u timeout=%u",
               (unsigned)me->diag.tx_done_cnt, (unsigned)me->diag.tx_timeout_cnt);

    for ( prio = (uint8_t)APP_PRIO_CH_FIRST; prio <= (uint8_t)APP_PRIO_MODBUS; ++prio ) {
        uint16_t const min_free = QActive_getQueueMin(prio);

        if ( min_free != 0xFFFFU ) { /* unstarted AOs report sentinel */
            elog_debug("modbus_ao", "qmin prio=%u free=%u",
                       (unsigned)prio, (unsigned)min_free);
        }
    }
}

/*----------------------------------------------------------------------------*/
/* Construction                                                               */
/*----------------------------------------------------------------------------*/

/** @brief Report-Slave-ID payload; no per-instance state. */
static const uint8_t additional[] = "8CH-AI-RTU";

/** @brief Identity reported by function code 0x11. */
static const modbus_slave_identity_t identity = {
    0xFFU,                              /**< slave_id */
    1U,                                 /**< is_running */
    additional,                         /**< additional */
    (uint16_t)(sizeof(additional) - 1U) /**< additional_length */
};

void modbus_ao_ctor(modbus_ao_t* me,
                    modbus_slave_t* slave,
                    const modbus_slave_config_t* slave_cfg)
{
    uint8_t ch;
    modbus_slave_config_t config;
    modbus_slave_port_event_t ev;
    struct fdb_default_kv factory;
    fdb_err_t st;

    /* Both halves are required: the slave core is built in this step. */
    Q_ASSERT(slave != NULL);
    Q_ASSERT(slave_cfg != NULL);

    QActive_ctor(&me->super, Q_STATE_CAST(&modbus_ao_initial));

    me->slave               = slave;
    me->store.db_ok         = 0U;
    me->store.commit_failed = 0U;
    me->config.addr         = REG_MAP_DEFAULT_DEV_ADDR;
    me->config.addr_ok      = 0U;
    me->telemetry.cjc_temp  = 0.0f;
    me->telemetry.cjc_valid = 0U;
    me->diag.tx_done_cnt    = 0U;
    me->diag.tx_timeout_cnt = 0U;
    me->diag.hb_ctr         = 0U;

    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        /* Channels report "unconfigured" until the first MEASURE_SIG. */
        memset(&me->config.cfg[ch], 0, sizeof(me->config.cfg[ch]));
        me->config.cfg_ok[ch]     = 0U;
        me->config.applied[ch]    = 0U;
        me->telemetry.value[ch]   = 0.0f;
        me->telemetry.status[ch]  = (uint16_t)CH_STATUS_UNCONFIGURED;
    }

    /* The parameter database opens here (FlashDB brings up FAL itself in
     * _fdb_init_ex, so no explicit fal_init()); loading runs in the initial
     * transition. The default_kv wrapper is a stack temp: the vendor copies
     * it by value and retains only the kvs pointer. */
    factory.kvs = param_defaults;
    factory.num = sizeof(param_defaults) / sizeof(param_defaults[0]);
    st          = fdb_kvdb_init(&param_db, PARAM_DB_NAME, BOARD_PARAM_PARTITION,
                                &factory, NULL);
    if ( st != FDB_NO_ERR ) {
        elog_error("modbus_ao", "fdb kvdb init failed (fdb %d)", (int)st);
    } else {
        me->store.db_ok = 1U;
    }

    /* This module owns the callbacks and identity half of the config; the
     * core copies the table and the instance's copy is the live one. */
    config = *slave_cfg;
    config.callbacks.ctx               = me;
    config.callbacks.coils             = &coils;
    config.callbacks.discrete_inputs   = &discrete;
    config.callbacks.holding_registers = &holding;
    config.callbacks.input_registers   = &input;
    config.callbacks.address           = &address;
    config.identity                    = identity;
    if ( modbus_slave_ctor(slave, &config) != MODBUS_SLAVE_OK ) {
        /* Defect: the config was vetted at assembly. */
        Q_ERROR();
    }

    /* The core copies this port; it is still stopped here. */
    ev.ctx               = me;
    ev.on_frame_received = on_frame;
    ev.on_frame_sent     = on_sent;
    ev.on_tx_timeout     = on_timeout;
    (void)modbus_slave_set_event_port(slave, &ev);
}

/*----------------------------------------------------------------------------*/
/* State machine                                                              */
/*----------------------------------------------------------------------------*/

/**
 * @brief Top-most initial transition: boot load, then serve.
 *
 * Runs inside QActive_start(), before QF_run(): the loaded configurations
 * are published while every channel is already started and subscribed.
 */
static QState modbus_ao_initial(modbus_ao_t* const me, void const* const par)
{
    uint8_t ch;

    Q_UNUSED_PAR(par);

    /* Subscriptions live in the initial transition (QP/C example style). */
    QActive_subscribe(Q_ACTIVE_UPCAST(me), MEASURE_SIG);
    QActive_subscribe(Q_ACTIVE_UPCAST(me), CJC_SIG);
    QActive_subscribe(Q_ACTIVE_UPCAST(me), CFG_APPLIED_SIG);

    load_all(me);
    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        if ( me->config.cfg_ok[ch] != 0U ) {
            publish_cfg(me, ch, &me->config.cfg[ch]);
        }
    }

    return Q_TRAN(&modbus_ao_serving);
}

/**
 * @brief Serving: the resident service state after the core starts.
 */
static QState modbus_ao_serving(modbus_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            if ( modbus_slave_start(me->slave) != MODBUS_SLAVE_OK ) {
                /* Board-level defect; the port was vetted at construction. */
                Q_ERROR();
            }
            status = Q_HANDLED();
            break;
        }
        case FRAME_SIG: {
            (void)modbus_slave_advance(me->slave);
            status = Q_HANDLED();
            break;
        }
        case TX_DONE_SIG: {
            ++me->diag.tx_done_cnt;
            status = Q_HANDLED();
            break;
        }
        case TX_TIMEOUT_SIG: {
            ++me->diag.tx_timeout_cnt;
            status = Q_HANDLED();
            break;
        }
        case MEASURE_SIG: {
            measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

            me->telemetry.value[m->ch]  = m->value;
            me->telemetry.status[m->ch] = m->status;
            status                      = Q_HANDLED();
            break;
        }
        case CFG_APPLIED_SIG: {
            uint8_t ch = Q_EVT_CAST(cfg_applied_evt_t)->ch;

            if ( ch < CH_CONFIG_CHANNEL_COUNT ) {
                me->config.applied[ch] = 1U;
            }
            status = Q_HANDLED();
            break;
        }
        case CJC_SIG: {
            cjc_evt_t const* cjc = Q_EVT_CAST(cjc_evt_t);

            me->telemetry.cjc_temp  = cjc->temp_c;
            me->telemetry.cjc_valid = cjc->valid;

            /* The 1 Hz heartbeat doubles as the diagnostic cadence. */
            ++me->diag.hb_ctr;
            if ( me->diag.hb_ctr >= MODBUS_AO_DIAG_PERIOD_HEARTBEATS ) {
                me->diag.hb_ctr = 0U;
                report_queue_watermarks(me);
            }
            status = Q_HANDLED();
            break;
        }
        default: {
            status = Q_SUPER(&QHsm_top);
            break;
        }
    }
    return status;
}
