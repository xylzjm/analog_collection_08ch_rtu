/**
 * @file test_modbus_ao.c
 * @brief AoModbus 写流水线与镜像的宿主测试。
 *
 * 从站核心是真实的 modbus_slave 库，经由假线路/定时器/临界区端口驱动；持久化
 * 代码运行在内存 FlashDB 假件之上。帧逐字节送入 modbus_slave_rx_byte()，由
 * modbus_slave_timer_expired() 捕获（它通过事件端口投递 FRAME_SIG），最后由
 * 真实 AO 对 advance() 的派发收尾。期望应答以字面字节配合独立的按位 CRC16
 * 明确写出，而不是由被测代码生成。
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qpc.h"

#include "app.h"
#include "fdb_fake.h"
#include "host_support.h"
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

/** @brief 核心所需的缓冲区；调用方拥有，内容归核心所有。 */
static uint8_t rx_buffer[256];
static uint8_t tx_buffer[256];

static modbus_ao_t ao;
static modbus_slave_t slave;
static modbus_port_fake_t port;
static QEvtPtr ao_queue[MODBUS_AO_QUEUE_LEN];
static QSubscrList subscr[MAX_SIG];
static QF_MPOOL_EL(measure_evt_t) pool_a[APP_POOL_A_BLOCK_NUM];
static QF_MPOOL_EL(config_set_evt_t) pool_b[APP_POOL_B_BLOCK_NUM];

/* CONFIG_SET_SIG 的发布观察者。 */
static QActive recorder;
static QEvtPtr rec_queue[32];
static uint8_t ao_started;
static uint8_t rec_started;

/** @brief 上一个场景按顺序发布的配置。 */
static struct {
    uint8_t ch;
    ch_config_t cfg;
} published[CH_CONFIG_CHANNEL_COUNT + 2U];
static uint16_t published_count;

/** @brief 场景自身的启动加载所发布的配置。 */
static uint16_t boot_publish_count;

/*----------------------------------------------------------------------------*/
/* 测试夹具                                                                   */
/*----------------------------------------------------------------------------*/

/** @brief 独立的按位 Modbus CRC16，总线上低字节在前。 */
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
    /* T3.5 到期：帧被捕获，on_frame_received 投递 FRAME_SIG。 */
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

/** @brief 把 TXE 数据流泵送完毕，然后触发 TC 中断。 */
static void pump_tx(void)
{
    uint16_t i;

    for ( i = 0U; i < 300U; ++i ) {
        (void)modbus_slave_tx_ready(&slave);
    }
    (void)modbus_slave_tx_complete(&slave);
    drain(); /* TX_DONE_SIG 落入队列 */
}

/** @brief 取出应答字节并复位线路记录器。 */
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

/** @brief 排空记录器并收集 CONFIG_SET 负载。 */
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

/** @brief 拆除世界，并准备全新的假介质和观察者。 */
static void world_begin(uint8_t db_fail)
{
    if ( ao_started != 0U ) {
        QActive_unregister_(Q_ACTIVE_UPCAST(&ao));
    }
    if ( rec_started != 0U ) {
        QActive_unregister_(&recorder);
    }
    memset(subscr, 0, sizeof(subscr));
    QActive_psInit(subscr, MAX_SIG);

    fdb_fake_reset();
    fdb_fake.fail_init = db_fail;
    modbus_port_fake_init(&port);
    published_count = 0U;

    /* 观察者必须在启动加载发布之前就绪。 */
    QActive_ctor(&recorder, Q_STATE_CAST(&rec_handler));
    QActive_start(&recorder, Q_PRIO(30U, 0U),
                  rec_queue, 32U, (void*)0, 0U, (void*)0);
    rec_started = 1U;
    QActive_subscribe(&recorder, CONFIG_SET_SIG);
}

/**
 * @brief 编排一次启动加载失败：长度错误的配置正文。
 *
 * @param ch 键中带有错误正文的通道零基索引。
 */
static void world_preload_cfg_bad(uint8_t ch)
{
    uint8_t bad[4] = {0};
    char key[FDB_FAKE_MAX_KEY];

    (void)snprintf(key, sizeof(key), "cfg/ch%u", (unsigned)ch);
    fdb_fake_put(key, bad, sizeof(bad));
}

/** @brief 编排一次启动加载失败：存储的从站地址非法。 */
static void world_preload_addr(uint16_t addr)
{
    fdb_fake_put("dev/addr", &addr, sizeof(addr));
}

/**
 * @brief 编排一份健康存储：每行默认记录都带出厂正文。
 *
 * 部分存储绝不会被回填，因此破坏或省略某个键的场景必须预置它仍期望加载的
 * 健康记录。
 *
 * @param skip_ch 不预置其键的通道零基索引（255 表示没有）：空洞是缺失，
 *        绝不是默认值。
 */
static void world_preload_defaults(uint8_t skip_ch)
{
    static const ch_config_t cfg = {
        (uint8_t)CH_TYPE_TC,   /**< 类型 */
        (uint8_t)CH_TC_TYPE_K, /**< 热电偶类型 */
        2U,                    /**< rate_code：10 Hz */
        0.0f,                  /**< 线性下限 */
        0.0f,                  /**< 线性上限 */
        0.0f,                  /**< 偏移 */
        1.0f,                  /**< 增益 */
    };
    uint16_t addr = REG_MAP_DEFAULT_DEV_ADDR;
    uint8_t ch;

    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        char key[FDB_FAKE_MAX_KEY];

        if ( ch == skip_ch ) {
            continue;
        }
        (void)snprintf(key, sizeof(key), "cfg/ch%u", (unsigned)ch);
        fdb_fake_put(key, &cfg, sizeof(cfg));
    }
    fdb_fake_put("dev/addr", &addr, sizeof(addr));
}

/** @brief 用从站核心构造 AO，并启动 AO。 */
static void world_start(void)
{
    modbus_slave_config_t cfg = {0};

    modbus_port_fake_config(&port, &cfg,
                            rx_buffer, (uint16_t)sizeof(rx_buffer),
                            tx_buffer, (uint16_t)sizeof(tx_buffer));

    modbus_ao_ctor(&ao, &slave, &cfg);

    QActive_start(Q_ACTIVE_UPCAST(&ao), Q_PRIO(APP_PRIO_MODBUS, 0U),
                  ao_queue, MODBUS_AO_QUEUE_LEN, (void*)0, 0U, (void*)0);
    ao_started = 1U;

    /* Serving 入口：start 打开接收并设置 T3.5。 */
    CHECK(port.rx_enabled != 0U);
    CHECK(port.tx_enabled == 0U);

    /*
     * start() 之后核心会一直丢弃数据，直到首次 T3.5 到期；真实的 TMR14 负责
     * 送达它。没有它，接收到的字节都会被丢弃。
     */
    (void)modbus_slave_timer_expired(&slave);
    rec_collect();
    boot_publish_count = published_count;
}

/**
 * @brief 构建从站，并在全新存储之上启动 AO。
 *
 * @param db_fail 非零表示编排一次 KV init 失败（b3 路径）。
 */
static void scenario_begin_ex(uint8_t db_fail)
{
    world_begin(db_fail);
    world_start();
}

static void scenario_begin(void)
{
    scenario_begin_ex(0U);
}

/** @brief 把假介质上一次写入的正文解码为一份配置。 */
static ch_config_t last_cfg(void)
{
    ch_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    if ( fdb_fake.last_len <= sizeof(cfg) ) {
        memcpy(&cfg, fdb_fake.last_value, fdb_fake.last_len);
    }
    return cfg;
}

/** @brief 场景结束不变量：队列已排空，临界区收支平衡。 */
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
/* 场景                                                                       */
/*----------------------------------------------------------------------------*/

/** @brief 启动加载把每份可用的通道配置各发布一次。 */
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
    CHECK(ao.config.addr == REG_MAP_DEFAULT_DEV_ADDR);
    CHECK(ao.config.addr_ok == 1U);
    {
        uint8_t ch;

        for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
            CHECK(published[ch].ch == ch);
            CHECK(published[ch].cfg.type == (uint8_t)CH_TYPE_TC);
            CHECK(published[ch].cfg.rate_code == 2U);
        }
    }

    /* 镜像用已加载的配置应答读取。 */
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
    CHECK(ao.diag.tx_done_cnt == 1U);
    scenario_end();
}

/** @brief 整块 FC16 写入会固化、更新镜像并重新发布。 */
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
    pdu[2] = 0x00U; /* 起始 0 */
    pdu[3] = 0x00U;
    pdu[4] = 0x0AU; /* 10 个寄存器 */
    pdu[5] = 20U;   /* 字节数 */
    for ( i = 0U; i < 20U; ++i ) {
        pdu[6U + i] = block[i];
    }
    build_frame(frame, &frame_len, 1U, pdu, 26U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();

    /* EX 00 应答：addr、FC16、起始、数量。 */
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

    /* 固化发生了一次，镜像与发布与之相符。 */
    CHECK(fdb_fake.set_calls == 1U);
    CHECK(strcmp(fdb_fake.last_key, "cfg/ch0") == 0);
    {
        ch_config_t saved = last_cfg();

        CHECK(saved.type == (uint8_t)CH_TYPE_4_20MA);
        CHECK(saved.rate_code == 9U);
        CHECK(saved.linear_max == 20.0f);
    }
    CHECK(ao.config.cfg[0].type == (uint8_t)CH_TYPE_4_20MA);
    CHECK(ao.config.cfg[0].gain == 2.0f);
    CHECK(ao.config.cfg_ok[0] == 1U);
    CHECK(ao.config.applied[0] == 0U); /* 已固化，尚未应用 */
    rec_collect();
    CHECK(published_count == (uint16_t)(boot_publish_count + 1U));
    CHECK(published[boot_publish_count].ch == 0U);
    CHECK(published[boot_publish_count].cfg.type == (uint8_t)CH_TYPE_4_20MA);
    CHECK(published[boot_publish_count].cfg.rate_code == 9U);

    /* 通道接受事件就是接受路径（CFG_APPLIED_SIG）。 */
    {
        cfg_applied_evt_t* e = Q_NEW(cfg_applied_evt_t, CFG_APPLIED_SIG);

        e->ch = 0U;
        QACTIVE_POST(Q_ACTIVE_UPCAST(&ao), &e->super, (void*)0);
        drain();
    }
    CHECK(ao.config.applied[0] == 1U);

    /* 一旦应用，同值重写就是真正的空操作。 */
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    (void)tx_take(reply, sizeof(reply)); /* EX 00 回显 */
    CHECK(fdb_fake.set_calls == 1U);
    rec_collect();
    CHECK(published_count == (uint16_t)(boot_publish_count + 1U));
    scenario_end();
}

/** @brief 被拒绝的写入应答 EX 03，且既不碰镜像也不碰 flash。 */
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

    bad.type       = 0x63U; /* 没有这种输入类型 */
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

    CHECK(fdb_fake.set_calls == 0U);
    CHECK(ao.config.cfg[0].type == (uint8_t)CH_TYPE_TC);
    rec_collect();
    CHECK(published_count == boot_publish_count);
    scenario_end();
}

/** @brief 地址变更：镜像跟随，旧地址静默，新地址可用。 */
static void test_address_change(void)
{
    uint8_t reply[64];
    uint16_t reply_len;
    uint8_t frame[8];
    uint16_t frame_len;
    uint8_t pdu_set[5]  = {0x06U, 0x00U, 0x80U, 0x00U, 0xF7U};
    uint8_t pdu_read[5] = {0x03U, 0x00U, 0x80U, 0x00U, 0x01U};

    scenario_begin();

    /* 改为 247：EX 00 回显，之后镜像与核心一致。 */
    build_frame(frame, &frame_len, 1U, pdu_set, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    reply_len = tx_take(reply, sizeof(reply));
    CHECK(reply_len == 8U);
    CHECK(fdb_fake.set_calls == 1U);
    CHECK(strcmp(fdb_fake.last_key, "dev/addr") == 0);
    CHECK(ao.config.addr == 247U);
    CHECK(modbus_slave_get_address(&slave) == 247U);

    /* 旧地址不再应答。 */
    build_frame(frame, &frame_len, 1U, pdu_read, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    CHECK(tx_take(reply, sizeof(reply)) == 0U);

    /* 新地址得到服务。 */
    build_frame(frame, &frame_len, 247U, pdu_read, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    reply_len = tx_take(reply, sizeof(reply));
    CHECK(reply_len == 7U); /* addr、FC03、计数、寄存器、CRC */
    CHECK(reply[0] == 247U);
    CHECK(reply[3] == 0x00U);
    CHECK(reply[4] == 0xF7U);
    scenario_end();
}

/** @brief 固化失败应答 EX 04，并把 b1 锁存进故障字。 */
static void test_commit_failure_latches(void)
{
    uint8_t reply[64];
    uint16_t reply_len;
    uint8_t frame[8];
    uint16_t frame_len;
    uint8_t pdu_set[5]   = {0x06U, 0x00U, 0x80U, 0x00U, 0xC8U}; /* 200 */
    uint8_t pdu_fault[5] = {0x04U, 0x00U, 0x44U, 0x00U, 0x01U}; /* 寄存器 68 */

    scenario_begin();
    fdb_fake.corrupt_read = 1U;

    build_frame(frame, &frame_len, 1U, pdu_set, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    fdb_fake.corrupt_read = 0U;

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
    CHECK(ao.config.addr == REG_MAP_DEFAULT_DEV_ADDR); /* 未采纳 */
    CHECK(ao.store.commit_failed != 0U);

    /* 故障字通过输入区暴露该锁存；由于本场景中还没有 CJC 心跳到达，
     * b0 也被置位。 */
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

/** @brief 损坏的帧被 CRC 丢弃，广播写入保持静默。 */
static void test_silence_paths(void)
{
    uint8_t reply[64];
    uint8_t frame[8];
    uint16_t frame_len;
    uint8_t pdu[5] = {0x06U, 0x00U, 0x80U, 0x00U, 0x96U}; /* 150 */

    scenario_begin();

    /* CRC 损坏：无应答，接收重新打开。 */
    build_frame(frame, &frame_len, 1U, pdu, 5U);
    frame[frame_len - 1U] ^= 0x55U;
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    CHECK(tx_take(reply, sizeof(reply)) == 0U);

    /* 广播：写入执行，线路保持静默。 */
    build_frame(frame, &frame_len, 0U, pdu, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    CHECK(tx_take(reply, sizeof(reply)) == 0U);
    CHECK(fdb_fake.set_calls == 1U);
    CHECK(strcmp(fdb_fake.last_key, "dev/addr") == 0);
    CHECK(ao.config.addr == 150U);
    scenario_end();
}

/** @brief 启动失败以锁存的故障位显现，而不是默认值。 */
static void test_boot_failures_latch(void)
{
    /* KV init 失败：b3 被锁存，且不发布任何配置。 */
    scenario_begin_ex(1U);

    CHECK(ao.store.db_ok == 0U);
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

/** @brief 长度错误的键只让该通道失败（b2，而不是默认值）。 */
static void test_boot_load_failure(void)
{
    uint8_t ch;

    world_begin(0U);
    world_preload_defaults(255U); /* 存储已有内容：不回填 */
    world_preload_cfg_bad(2U);
    world_start();

    /* 只有健康的通道被发布和采纳。 */
    CHECK(boot_publish_count == (uint16_t)(CH_CONFIG_CHANNEL_COUNT - 1U));
    CHECK(ao.config.cfg_ok[2] == 0U);
    CHECK(ao.config.cfg_ok[0] == 1U);
    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        if ( ch != 2U ) {
            CHECK(ao.config.cfg[ch].type == (uint8_t)CH_TYPE_TC);
            CHECK(ao.config.cfg[ch].rate_code == 2U);
        }
    }

    /* 故障字显示加载失败（b0：尚无心跳）。 */
    {
        const modbus_slave_callbacks_t* cb = &slave.callbacks;
        uint8_t reply[2];

        CHECK(cb->input_registers(cb->ctx, REG_MAP_SYS_BASE + 4U, 1U, reply,
                                  MODBUS_SLAVE_REG_READ)
              == MODBUS_SLAVE_EX_NONE);
        CHECK(modbus_slave_get_u16(reply)
              == (uint16_t)(REG_MAP_FAULT_PARAM_LOAD | REG_MAP_FAULT_CJC));
    }
    scenario_end();
}

/** @brief 存储地址非法时回退到 1，并报告该失败。 */
static void test_bad_address(void)
{
    world_begin(0U);
    world_preload_defaults(255U); /* 存储已有内容：不回填 */
    world_preload_addr(0U);
    world_start();

    CHECK(boot_publish_count == CH_CONFIG_CHANNEL_COUNT);
    CHECK(ao.config.addr == REG_MAP_DEFAULT_DEV_ADDR);
    CHECK(ao.config.addr_ok == 0U);
    {
        const modbus_slave_callbacks_t* cb = &slave.callbacks;
        uint8_t reply[2];

        CHECK(cb->input_registers(cb->ctx, REG_MAP_SYS_BASE + 4U, 1U, reply,
                                  MODBUS_SLAVE_REG_READ)
              == MODBUS_SLAVE_EX_NONE);
        CHECK(modbus_slave_get_u16(reply)
              == (uint16_t)(REG_MAP_FAULT_PARAM_LOAD | REG_MAP_FAULT_CJC));
    }
    scenario_end();

    /* 下一次启动重新健康。 */
    scenario_begin();
    CHECK(ao.config.addr == REG_MAP_DEFAULT_DEV_ADDR);
    CHECK(ao.config.addr_ok == 1U);
    scenario_end();
}

/** @brief 有内容的存储中缺失某个键只让其通道失败（b2）。 */
static void test_boot_missing_key(void)
{
    uint8_t ch;

    world_begin(0U);
    world_preload_defaults(5U); /* 除 cfg/ch5 外的每一行：空洞即缺失 */
    world_start();

    /* 只有 ch5 的 b2：有内容的存储不会回填任何东西。 */
    CHECK(boot_publish_count == (uint16_t)(CH_CONFIG_CHANNEL_COUNT - 1U));
    CHECK(ao.config.cfg_ok[5] == 0U);
    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        if ( ch != 5U ) {
            CHECK(ao.config.cfg_ok[ch] == 1U);
        }
    }

    /* 故障字显示加载失败（b0：尚无心跳）。 */
    {
        const modbus_slave_callbacks_t* cb = &slave.callbacks;
        uint8_t reply[2];

        CHECK(cb->input_registers(cb->ctx, REG_MAP_SYS_BASE + 4U, 1U, reply,
                                  MODBUS_SLAVE_REG_READ)
              == MODBUS_SLAVE_EX_NONE);
        CHECK(modbus_slave_get_u16(reply)
              == (uint16_t)(REG_MAP_FAULT_PARAM_LOAD | REG_MAP_FAULT_CJC));
    }
    scenario_end();
}

/**
 * @brief 超长正文以加载长度错误失败，而不是被接受。
 *
 * 即使复制的副本被截断到 20 字节的配置，加载器也必须看到完整的存储长度
 *（24 != 20）。
 */
static void test_boot_oversize_body(void)
{
    uint8_t big[24];
    uint8_t ch;

    memset(big, 0x5A, sizeof(big));

    world_begin(0U);
    world_preload_defaults(3U);
    fdb_fake_put("cfg/ch3", big, sizeof(big));
    world_start();

    CHECK(boot_publish_count == (uint16_t)(CH_CONFIG_CHANNEL_COUNT - 1U));
    CHECK(ao.config.cfg_ok[3] == 0U);
    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        if ( ch != 3U ) {
            CHECK(ao.config.cfg_ok[ch] == 1U);
        }
    }
    scenario_end();
}

/** @brief MEASURE_SIG 遥测到达输入寄存器镜像。 */
static void test_measure_mirror(void)
{
    uint8_t reply[64];
    uint16_t reply_len;
    uint8_t frame[8];
    uint16_t frame_len;
    uint8_t pdu[5] = {0x04U, 0x00U, 0x08U, 0x00U, 0x03U}; /* ch2：值、状态 */
    measure_evt_t* e;

    scenario_begin();

    e         = Q_NEW(measure_evt_t, MEASURE_SIG);
    e->ch     = 1U;
    e->value  = 42.5f;
    e->status = (uint16_t)CH_STATUS_OK;
    QACTIVE_PUBLISH(&e->super, (void*)0);
    drain();

    CHECK(ao.telemetry.value[1] == 42.5f);
    CHECK(ao.telemetry.status[1] == (uint16_t)CH_STATUS_OK);

    build_frame(frame, &frame_len, 1U, pdu, 5U);
    feed_frame(frame, frame_len);
    drain();
    pump_tx();
    reply_len = tx_take(reply, sizeof(reply));
    CHECK(reply_len == 11U); /* addr、FC04、count=6、3 个寄存器、CRC */

    {
        uint32_t bits;
        float value;

        bits = ((uint32_t)modbus_slave_get_u16(&reply[3]) << 16)
               | (uint32_t)modbus_slave_get_u16(&reply[5]);
        memcpy(&value, &bits, sizeof(value));
        CHECK(value == 42.5f);
        CHECK(modbus_slave_get_u16(&reply[7]) == (uint16_t)CH_STATUS_OK);
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
    test_boot_load_failure();
    test_bad_address();
    test_boot_missing_key();
    test_boot_oversize_body();
    test_measure_mirror();

    printf("test_modbus_ao: %d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
