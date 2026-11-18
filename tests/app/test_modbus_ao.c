/**
 * @file test_modbus_ao.c
 * @brief Host tests for the AoModbus write pipeline and mirrors.
 *
 * The slave core is the real modbus_slave library driven over fake
 * line/timer/critical ports; the param component is the scriptable stub.
 * Frames are fed byte by byte through modbus_slave_rx_byte(), captured by
 * modbus_slave_timer_expired() (which posts FRAME_SIG through the event
 * port) and concluded by the real AO dispatch of advance(). Expected
 * replies are spelled out as literal bytes with an independent bitwise
 * CRC16, not produced by the code under test.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qpc.h"

#include "app_events.h"
#include "host_support.h"
#include "modbus_ao.h"
#include "modbus_port_fake.h"
#include "param_stub.h"
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

/** @brief Buffers the core requires; caller-owned, core-owned content. */
static uint8_t rx_buffer[256];
static uint8_t tx_buffer[256];

static modbus_ao_t ao;
static modbus_slave_t slave;
static modbus_port_fake_t port;
static QEvtPtr ao_queue[MODBUS_AO_QUEUE_LEN];
static QSubscrList subscr[MAX_SIG];
static QF_MPOOL_EL(measure_evt_t) pool_a[APP_POOL_A_BLOCK_NUM];
static QF_MPOOL_EL(config_set_evt_t) pool_b[APP_POOL_B_BLOCK_NUM];

/* Publish observer for CONFIG_SET_SIG. */
static QActive recorder;
static QEvtPtr rec_queue[32];
static uint8_t ao_started;
static uint8_t rec_started;

/** @brief Configurations published by the last scenario, in order. */
static struct {
    uint8_t ch;
    ch_config_t cfg;
} published[CH_CONFIG_CHANNEL_COUNT + 2U];
static uint16_t published_count;

/** @brief Configurations the scenario's own boot load published. */
static uint16_t boot_publish_count;

/*----------------------------------------------------------------------------*/
/* Harness                                                                    */
/*----------------------------------------------------------------------------*/

/** @brief Independent bitwise Modbus CRC16, low byte first on the wire. */
static uint16_t crc16(const uint8_t* data,
                      uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;

    for ( i = 0U; i < len; ++i ) {
        uint8_t shift;

        crc ^= (uint16_t)data[i];
        for ( shift = 0U; shift < 8U; ++shift ) {
            if ( (crc & 0x0001U) != 0U ) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

static void build_frame(uint8_t* out,
                        uint16_t* len,
                        uint8_t addr,
                        const uint8_t* pdu,
                        uint16_t pdu_len)
{
    uint16_t crc;

    out[0] = addr;
    memcpy(&out[1], pdu, pdu_len);
    crc               = crc16(out, (uint16_t)(pdu_len + 1U));
    out[pdu_len + 1U] = (uint8_t)(crc & 0xFFU);
    out[pdu_len + 2U] = (uint8_t)(crc >> 8);
    *len              = (uint16_t)(pdu_len + 3U);
}

static void feed_frame(const uint8_t* frame,
                       uint16_t len)
{
    uint16_t i;

    for ( i = 0U; i < len; ++i ) {
        (void)modbus_slave_rx_byte(&slave, frame[i]);
    }
    /* T3.5 expiry: the frame is captured, on_frame_received posts FRAME_SIG. */
    (void)modbus_slave_timer_expired(&slave);
}

static void drain(void)
{
    while ( !QEQueue_isEmpty(&ao.super.eQueue) ) {
        QEvt const* e = QActive_get_(Q_ACTIVE_UPCAST(&ao));

        QASM_DISPATCH(Q_ACTIVE_UPCAST(&ao), e, 0U);
        QF_gc(e);
    }
}

/** @brief Pump the TXE stream to completion, then the TC interrupt. */
static void pump_tx(void)
{
    uint16_t i;

    for ( i = 0U; i < 300U; ++i ) {
        (void)modbus_slave_tx_ready(&slave);
    }
    (void)modbus_slave_tx_complete(&slave);
    drain(); /* TX_DONE_SIG lands in the queue */
}

/** @brief Capture the reply bytes and reset the line recorder. */
static uint16_t tx_take(uint8_t* out,
                        uint16_t max)
{
    uint16_t n = port.tx_len;

    if ( n > max ) {
        n = max;
    }
    memcpy(out, port.tx_bytes, n);
    port.tx_len = 0U;
    return n;
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

/** @brief Build the slave, attach it and bring the AO up. */
static void scenario_begin_ex(const param_boot_result_t* boot)
{
    modbus_slave_config_t cfg;

    if ( ao_started != 0U ) {
        QActive_unregister_(Q_ACTIVE_UPCAST(&ao));
    }
    if ( rec_started != 0U ) {
        QActive_unregister_(&recorder);
    }
    memset(subscr, 0, sizeof(subscr));
    QActive_psInit(subscr, MAX_SIG);

    param_stub_reset();
    if ( boot != NULL ) {
        param_stub_set_boot(boot);
    }
    modbus_port_fake_init(&port);
    published_count = 0U;

    /* The observer must be up before the boot load publishes. */
    QActive_ctor(&recorder, Q_STATE_CAST(&rec_handler));
    QActive_start(&recorder, Q_PRIO(30U, 0U),
                  rec_queue, 32U, (void*)0, 0U, (void*)0);
    rec_started = 1U;
    QActive_subscribe(&recorder, CONFIG_SET_SIG);

    modbus_ao_ctor(&ao);

    cfg.baudrate    = 9600U;
    cfg.rx_buffer   = rx_buffer;
    cfg.rx_capacity = (uint16_t)sizeof(rx_buffer);
    cfg.tx_buffer   = tx_buffer;
    cfg.tx_capacity = (uint16_t)sizeof(tx_buffer);
    modbus_port_fake_line(&port, &cfg.line);
    modbus_port_fake_timer(&port, &cfg.timer);
    modbus_port_fake_critical(&port, &cfg.critical);
    cfg.callbacks = *modbus_ao_callbacks(&ao);
    cfg.identity  = *modbus_ao_identity();
    CHECK(modbus_slave_ctor(&slave, &cfg) == MODBUS_SLAVE_OK);

    modbus_ao_attach(&ao, &slave);

    QActive_start(Q_ACTIVE_UPCAST(&ao), Q_PRIO(11U, 0U),
                  ao_queue, MODBUS_AO_QUEUE_LEN, (void*)0, 0U, (void*)0);
    ao_started = 1U;

    /* Serving entry: start opens reception and arms T3.5. */
    CHECK(port.rx_enabled != 0U);
    CHECK(port.tx_enabled == 0U);

    /*
     * start() leaves the core discarding until the first T3.5 expiry; the
     * real TMR14 delivers it. Without it, received bytes are dropped.
     */
    (void)modbus_slave_timer_expired(&slave);
    rec_collect();
    boot_publish_count = published_count;
}

static void scenario_begin(void)
{
    scenario_begin_ex(NULL);
}

/** @brief End-of-scenario invariants: queues drained, criticals balanced. */
static void scenario_end(void)
{
    drain();
    rec_collect();
    CHECK(QEQueue_isEmpty(&ao.super.eQueue));
    CHECK(QEQueue_isEmpty(&recorder.eQueue));
    CHECK(port.nest == 0);
    CHECK(port.enter_calls == port.exit_calls);
}

/*----------------------------------------------------------------------------*/
/* Scenarios                                                                  */
/*----------------------------------------------------------------------------*/

/** @brief Boot load publishes every usable channel configuration once. */
static void test_boot_load_publishes(void)
{
    uint8_t reply[64];
    uint16_t reply_len;
    uint8_t frame[8];
    uint16_t frame_len;
    uint8_t pdu[5]                  = {0x03U, 0x00U, 0x00U, 0x00U, 0x05U};
    static const uint8_t expected[] = {
        0x01U, 0x03U, 0x0AU, 0x00U, 0x03U, 0x00U, 0x02U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    uint8_t full[sizeof(expected) + 2U];
    uint16_t crc;

    scenario_begin();
    CHECK(boot_publish_count == CH_CONFIG_CHANNEL_COUNT);
    {
        uint8_t ch;

        for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
            CHECK(published[ch].ch == ch);
            CHECK(published[ch].cfg.type == (uint8_t)CH_TYPE_TC);
            CHECK(published[ch].cfg.rate_code == 2U);
        }
    }

    /* The mirror answers reads from the loaded configuration. */
    build_frame(frame, &frame_len, 1U, pdu, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();

    memcpy(full, expected, sizeof(expected));
    crc                         = crc16(full, (uint16_t)sizeof(expected));
    full[sizeof(expected)]      = (uint8_t)(crc & 0xFFU);
    full[sizeof(expected) + 1U] = (uint8_t)(crc >> 8);

    reply_len = tx_take(reply, sizeof(reply));
    CHECK(reply_len == sizeof(full));
    CHECK(memcmp(reply, full, sizeof(full)) == 0);
    CHECK(ao.tx_done_cnt == 1U);
    scenario_end();
}

/** @brief A full-block FC16 write commits, mirrors and republishes. */
static void test_fc16_write_pipeline(void)
{
    uint8_t reply[64];
    uint16_t reply_len;
    uint8_t frame[32];
    uint16_t frame_len;
    uint8_t pdu[27];
    ch_config_t next;
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint16_t i;

    scenario_begin();

    next.type       = (uint8_t)CH_TYPE_4_20MA;
    next.tc_type    = CH_TC_TYPE_NONE;
    next.rate_code  = 9U; /* 50 Hz */
    next.linear_min = 4.0f;
    next.linear_max = 20.0f;
    next.offset     = 0.5f;
    next.gain       = 2.0f;
    reg_map_ch_encode(&next, block);

    pdu[0] = 0x10U;
    pdu[1] = 0x00U;
    pdu[2] = 0x00U; /* start 0 */
    pdu[3] = 0x00U;
    pdu[4] = 0x0AU; /* 10 registers */
    pdu[5] = 20U;   /* byte count */
    for ( i = 0U; i < 20U; ++i ) {
        pdu[6U + i] = block[i];
    }
    build_frame(frame, &frame_len, 1U, pdu, 26U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();

    /* EX 00 reply: addr, FC16, start, quantity. */
    {
        static const uint8_t head[] = {0x01U, 0x10U, 0x00U, 0x00U, 0x00U, 0x0AU};
        uint8_t full[8];
        uint16_t crc;

        memcpy(full, head, sizeof(head));
        crc     = crc16(full, (uint16_t)sizeof(head));
        full[6] = (uint8_t)(crc & 0xFFU);
        full[7] = (uint8_t)(crc >> 8);

        reply_len = tx_take(reply, sizeof(reply));
        CHECK(reply_len == 8U);
        CHECK(memcmp(reply, full, 8U) == 0);
    }

    /* The commit happened once, the mirror and the publish agree with it. */
    CHECK(param_stub.save_cfg_calls == 1U);
    CHECK(param_stub.last_ch == 0U);
    CHECK(param_stub.last_cfg.type == (uint8_t)CH_TYPE_4_20MA);
    CHECK(param_stub.last_cfg.rate_code == 9U);
    CHECK(param_stub.last_cfg.linear_max == 20.0f);
    CHECK(ao.image.cfg[0].type == (uint8_t)CH_TYPE_4_20MA);
    CHECK(ao.image.cfg[0].gain == 2.0f);
    rec_collect();
    CHECK(published_count == (uint16_t)(boot_publish_count + 1U));
    CHECK(published[boot_publish_count].ch == 0U);
    CHECK(published[boot_publish_count].cfg.type == (uint8_t)CH_TYPE_4_20MA);
    CHECK(published[boot_publish_count].cfg.rate_code == 9U);
    scenario_end();
}

/** @brief A rejected write answers EX 03 and touches neither mirror nor flash. */
static void test_invalid_write_rejected(void)
{
    uint8_t reply[64];
    uint16_t reply_len;
    uint8_t frame[32];
    uint16_t frame_len;
    uint8_t pdu[27];
    ch_config_t bad;
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint16_t i;

    scenario_begin();

    bad.type       = 0x63U; /* no such input type */
    bad.tc_type    = CH_TC_TYPE_NONE;
    bad.rate_code  = 2U;
    bad.linear_min = 0.0f;
    bad.linear_max = 0.0f;
    bad.offset     = 0.0f;
    bad.gain       = 1.0f;
    reg_map_ch_encode(&bad, block);

    pdu[0] = 0x10U;
    pdu[1] = 0x00U;
    pdu[2] = 0x00U;
    pdu[3] = 0x00U;
    pdu[4] = 0x0AU;
    pdu[5] = 20U;
    for ( i = 0U; i < 20U; ++i ) {
        pdu[6U + i] = block[i];
    }
    build_frame(frame, &frame_len, 1U, pdu, 26U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();

    {
        static const uint8_t head[] = {0x01U, 0x90U, 0x03U};
        uint8_t full[5];
        uint16_t crc;

        memcpy(full, head, sizeof(head));
        crc     = crc16(full, (uint16_t)sizeof(head));
        full[3] = (uint8_t)(crc & 0xFFU);
        full[4] = (uint8_t)(crc >> 8);

        reply_len = tx_take(reply, sizeof(reply));
        CHECK(reply_len == 5U);
        CHECK(memcmp(reply, full, 5U) == 0);
    }

    CHECK(param_stub.save_cfg_calls == 0U);
    CHECK(ao.image.cfg[0].type == (uint8_t)CH_TYPE_TC);
    rec_collect();
    CHECK(published_count == boot_publish_count);
    scenario_end();
}

/** @brief Address change: mirror moves, old address goes silent, new works. */
static void test_address_change(void)
{
    uint8_t reply[64];
    uint16_t reply_len;
    uint8_t frame[8];
    uint16_t frame_len;
    uint8_t pdu_set[5]  = {0x06U, 0x00U, 0x80U, 0x00U, 0xF7U};
    uint8_t pdu_read[5] = {0x03U, 0x00U, 0x80U, 0x00U, 0x01U};

    scenario_begin();

    /* Change to 247: EX 00 echo, mirror and core agree afterwards. */
    build_frame(frame, &frame_len, 1U, pdu_set, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    reply_len = tx_take(reply, sizeof(reply));
    CHECK(reply_len == 8U);
    CHECK(param_stub.save_addr_calls == 1U);
    CHECK(ao.image.addr_image == 247U);
    CHECK(modbus_slave_get_address(&slave) == 247U);

    /* The old address no longer answers. */
    build_frame(frame, &frame_len, 1U, pdu_read, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    CHECK(tx_take(reply, sizeof(reply)) == 0U);

    /* The new address is served. */
    build_frame(frame, &frame_len, 247U, pdu_read, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    reply_len = tx_take(reply, sizeof(reply));
    CHECK(reply_len == 7U); /* addr, FC03, count, register, CRC */
    CHECK(reply[0] == 247U);
    CHECK(reply[3] == 0x00U);
    CHECK(reply[4] == 0xF7U);
    scenario_end();
}

/** @brief A failing commit answers EX 04 and latches b1 into the fault word. */
static void test_commit_failure_latches(void)
{
    uint8_t reply[64];
    uint16_t reply_len;
    uint8_t frame[8];
    uint16_t frame_len;
    uint8_t pdu_set[5]   = {0x06U, 0x00U, 0x80U, 0x00U, 0xC8U}; /* 200 */
    uint8_t pdu_fault[5] = {0x04U, 0x00U, 0x44U, 0x00U, 0x01U}; /* reg 68 */

    scenario_begin();
    param_stub_set_save(PARAM_ERR_WRITE, PARAM_ERR_WRITE);

    build_frame(frame, &frame_len, 1U, pdu_set, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();

    {
        static const uint8_t head[] = {0x01U, 0x86U, 0x04U};
        uint8_t full[5];
        uint16_t crc;

        memcpy(full, head, sizeof(head));
        crc     = crc16(full, (uint16_t)sizeof(head));
        full[3] = (uint8_t)(crc & 0xFFU);
        full[4] = (uint8_t)(crc >> 8);

        reply_len = tx_take(reply, sizeof(reply));
        CHECK(reply_len == 5U);
        CHECK(memcmp(reply, full, 5U) == 0);
    }
    CHECK(ao.image.addr_image == REG_MAP_DEV_ADDR_MIN); /* not adopted */
    CHECK(ao.image.b1 != 0U);

    /* The fault word exposes the latch through the input area; b0 is also
     * set because no CJC heartbeat has arrived yet in this scenario. */
    build_frame(frame, &frame_len, 1U, pdu_fault, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    reply_len = tx_take(reply, sizeof(reply));
    CHECK(reply_len == 7U);
    CHECK(reply[0] == 1U);
    CHECK(reply[1] == 0x04U);
    CHECK(reply[2] == 0x02U);
    CHECK(modbus_slave_get_u16(&reply[3])
          == (uint16_t)(REG_MAP_FAULT_PARAM_WRITE | REG_MAP_FAULT_CJC));
    scenario_end();
}

/** @brief A broken frame is dropped by CRC, a broadcast write stays silent. */
static void test_silence_paths(void)
{
    uint8_t reply[64];
    uint8_t frame[8];
    uint16_t frame_len;
    uint8_t pdu[5] = {0x06U, 0x00U, 0x80U, 0x00U, 0x96U}; /* 150 */

    scenario_begin();

    /* Corrupted CRC: no reply, reception reopens. */
    build_frame(frame, &frame_len, 1U, pdu, 5U);
    frame[frame_len - 1U] ^= 0x55U;
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    CHECK(tx_take(reply, sizeof(reply)) == 0U);

    /* Broadcast: the write executes, the line stays silent. */
    build_frame(frame, &frame_len, 0U, pdu, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    CHECK(tx_take(reply, sizeof(reply)) == 0U);
    CHECK(param_stub.save_addr_calls == 1U);
    CHECK(ao.image.addr_image == 150U);
    scenario_end();
}

/** @brief Boot failures surface as latched fault bits, not defaults. */
static void test_boot_failures_latch(void)
{
    param_boot_result_t boot;

    memset(&boot, 0, sizeof(boot));
    boot.db_ok    = 0U;                   /* FlashDB init failed: b3, no configuration published */
    boot.dev_addr = REG_MAP_DEV_ADDR_MIN; /* param.c guarantees the default */

    scenario_begin_ex(&boot);

    CHECK(ao.image.fdb_init_failed != 0U);
    rec_collect();
    CHECK(published_count == 0U);

    {
        uint8_t reply[64];
        uint16_t reply_len;
        uint8_t frame[8];
        uint16_t frame_len;
        uint8_t pdu_fault[5] = {0x04U, 0x00U, 0x44U, 0x00U, 0x01U};

        build_frame(frame, &frame_len, 1U, pdu_fault, 5U);
        feed_frame(frame, frame_len);
        drain();
        pump_tx();
        reply_len = tx_take(reply, sizeof(reply));
        CHECK(reply_len == 7U);
        CHECK(modbus_slave_get_u16(&reply[3])
              == (uint16_t)(REG_MAP_FAULT_FDB_INIT | REG_MAP_FAULT_CJC));
    }
    scenario_end();
}

/** @brief MEASURE_SIG telemetry reaches the input register mirror. */
static void test_measure_mirror(void)
{
    uint8_t reply[64];
    uint16_t reply_len;
    uint8_t frame[8];
    uint16_t frame_len;
    uint8_t pdu[5] = {0x04U, 0x00U, 0x08U, 0x00U, 0x03U}; /* ch2: value, status */
    measure_evt_t* e;

    scenario_begin();

    e         = Q_NEW(measure_evt_t, MEASURE_SIG);
    e->ch     = 1U;
    e->value  = 42.5f;
    e->status = (uint16_t)REG_MAP_STATUS_OK;
    QACTIVE_PUBLISH(&e->super, (void*)0);
    drain();

    CHECK(ao.image.value[1] == 42.5f);
    CHECK(ao.image.status[1] == (uint16_t)REG_MAP_STATUS_OK);

    build_frame(frame, &frame_len, 1U, pdu, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    reply_len = tx_take(reply, sizeof(reply));
    CHECK(reply_len == 11U); /* addr, FC04, count=6, 3 registers, CRC */

    {
        uint32_t bits;
        float value;

        bits = ((uint32_t)modbus_slave_get_u16(&reply[3]) << 16)
               | (uint32_t)modbus_slave_get_u16(&reply[5]);
        memcpy(&value, &bits, sizeof(value));
        CHECK(value == 42.5f);
        CHECK(modbus_slave_get_u16(&reply[7]) == (uint16_t)REG_MAP_STATUS_OK);
    }
    scenario_end();
}

/*----------------------------------------------------------------------------*/

int main(void)
{
    QF_init();
    QF_poolInit(pool_a, sizeof(pool_a), sizeof(pool_a[0]));
    QF_poolInit(pool_b, sizeof(pool_b), sizeof(pool_b[0]));

    test_boot_load_publishes();
    test_fc16_write_pipeline();
    test_invalid_write_rejected();
    test_address_change();
    test_commit_failure_latches();
    test_silence_paths();
    test_boot_failures_latch();
    test_measure_mirror();

    printf("test_modbus_ao: %d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
