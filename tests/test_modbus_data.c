/**
 * @file test_modbus_data.c
 * @brief 写事务与持久化的回调层测试。
 *
 * 在假线路/定时器/临界区端口之上构建真实（未启动）的从站核心，并用内存
 * FlashDB 假件直接驱动它的数据空间回调——不经帧，也不派发被测 AO——因此整套
 * 事务纪律完全按核心调用它的方式运行：校验、固化（通过 fdb_fake 介质上的真实
 * 持久化代码，带回读检查）、再镜像、再发布。发布结果由记录器 AO 观察。启动加载
 *（初始迁移）与 CFG_APPLIED_SIG 处理器位于 test_modbus_ao.c 的事件层。
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
/* FlashDB 假介质                                                             */
/*----------------------------------------------------------------------------*/

/** @brief 假介质契约检查所用的出厂夹具正文。 */
static const uint8_t fixture_body[4] = {0x11U, 0x22U, 0x33U, 0x44U};

/* 与 modbus_ao.c 相同的厂商边界强制转换：正文保持 const。 */
static struct fdb_default_kv_node fixture_nodes[2] = {
    {(char*)"cfg/ch0", (void*)fixture_body, sizeof(fixture_body)},
    {(char*)"dev/addr", (void*)fixture_body, sizeof(fixture_body)},
};

/* 一条零长度记录：厂商把 value_len == 0 读作 C 字符串。 */
static struct fdb_default_kv_node str_nodes[1] = {
    {(char*)"str/k", (void*)"abc", 0},
};

/** @brief FlashDB 数据库名（仅用于诊断），与 modbus_ao.c 的命名一致。 */
#define PARAM_DB_NAME "param"

/**
 * @brief 固定 fdb_fake 介质的契约。
 *
 * 该假件支撑着本套件中的每一项持久化检查，因此它的忠实性规则固定在其主要
 * 使用者旁边：fdb_blob_make() 不改动 saved；get 报告完整存储长度（缺失为 0）
 * 并复制 min(len, size)；set 在遵从 fail_write 之前先计数并记录；init 只向
 * 空介质填充默认表，把零长度行读作 C 字符串，且不算一次 set；空洞绝不会被
 * 回填；关闭的数据库按厂商的 db_init_ok 守卫应答；NULL 正文删除键。
 */
static void test_fake_medium(void)
{
    struct fdb_blob blob;
    struct fdb_default_kv fixture;
    struct fdb_default_kv str_fixture;
    uint8_t preset[2] = {0xA5U, 0x5AU};
    uint8_t body8[8]  = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
    uint8_t dst[6];
    uint8_t buf[8] = {0};
    size_t n;

    fixture.kvs     = fixture_nodes;
    fixture.num     = sizeof(fixture_nodes) / sizeof(fixture_nodes[0]);
    str_fixture.kvs = str_nodes;
    str_fixture.num = sizeof(str_nodes) / sizeof(str_nodes[0]);

    /* fdb_blob_make 只设置 buf/size；saved 保持不变。 */
    blob.saved.len = 0x5A5AU;
    CHECK(fdb_blob_make(&blob, buf, sizeof(buf)) == &blob);
    CHECK(blob.buf == buf);
    CHECK(blob.size == sizeof(buf));
    CHECK(blob.saved.len == 0x5A5AU);

    /* 空介质会填充整张默认表（出厂启动），且填充不算一次 set。 */
    fdb_fake_reset();
    CHECK(fdb_kvdb_init(NULL, PARAM_DB_NAME, BOARD_PARAM_PARTITION, &fixture, NULL) == FDB_NO_ERR);
    CHECK(fdb_fake.init_calls == 1U);
    CHECK(fdb_fake.set_calls == 0U);

    /* 从介质往返一次；saved.len 是完整存储长度。 */
    blob.saved.len = 0U;
    n              = fdb_kv_get_blob(NULL, "cfg/ch0", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK(n == sizeof(fixture_body));
    CHECK(blob.saved.len == sizeof(fixture_body));
    CHECK(memcmp(buf, fixture_body, sizeof(fixture_body)) == 0);
    blob.saved.len = 0U;
    n              = fdb_kv_get_blob(NULL, "dev/addr", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK(n == sizeof(fixture_body));

    /* 预置正文胜过默认值，第二次 init 既不覆盖也不回填（出厂启动规则）。 */
    CHECK(fdb_kv_set_blob(NULL, "cfg/ch0", fdb_blob_make(&blob, preset, sizeof(preset)))
          == FDB_NO_ERR);
    CHECK(fdb_fake.set_calls == 1U);
    CHECK(strcmp(fdb_fake.last_key, "cfg/ch0") == 0);
    CHECK(fdb_fake.last_len == sizeof(preset));
    CHECK(fdb_kvdb_init(NULL, PARAM_DB_NAME, BOARD_PARAM_PARTITION, &fixture, NULL) == FDB_NO_ERR);
    CHECK(fdb_fake.init_calls == 2U);
    CHECK(fdb_fake.set_calls == 1U);
    blob.saved.len = 0U;
    n              = fdb_kv_get_blob(NULL, "cfg/ch0", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK(n == sizeof(preset));
    CHECK(memcmp(buf, preset, sizeof(preset)) == 0);

    /* fail_write：尝试在被拒绝之前就已计数并记录——last_* 保存的是失败尝试的
     * 值——目标条目的旧正文保持不变。 */
    fdb_fake.fail_write = 1U;
    CHECK(fdb_kv_set_blob(NULL, "dev/addr", fdb_blob_make(&blob, body8, sizeof(body8)))
          == FDB_WRITE_ERR);
    fdb_fake.fail_write = 0U;
    CHECK(fdb_fake.set_calls == 2U);
    CHECK(strcmp(fdb_fake.last_key, "dev/addr") == 0);
    CHECK(fdb_fake.last_len == sizeof(body8));
    blob.saved.len = 0U;
    n              = fdb_kv_get_blob(NULL, "dev/addr", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK(n == sizeof(fixture_body));
    CHECK(memcmp(buf, fixture_body, sizeof(fixture_body)) == 0);

    /* 复制被截断，报告完整长度，缓冲区尾部不被改动。 */
    CHECK(fdb_kv_set_blob(NULL, "cfg/ch2", fdb_blob_make(&blob, body8, sizeof(body8)))
          == FDB_NO_ERR);
    memset(dst, 0xCC, sizeof(dst));
    blob.saved.len = 0U;
    n              = fdb_kv_get_blob(NULL, "cfg/ch2", fdb_blob_make(&blob, dst, 4U));
    CHECK(n == 4U);
    CHECK(blob.saved.len == sizeof(body8));
    CHECK((dst[4] == 0xCCU) && (dst[5] == 0xCCU));

    /* 缺失就是缺失：不复制任何内容，saved.len 为 0。 */
    memset(buf, 0x5A, sizeof(buf));
    blob.saved.len = 77U;
    n              = fdb_kv_get_blob(NULL, "absent", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK(n == 0U);
    CHECK(blob.saved.len == 0U);
    CHECK(buf[0] == 0x5AU);

    /* 空洞绝不会被回填：预置的一行在 init 后保留，缺失的默认行仍然缺失
     *（出厂启动规则）。 */
    fdb_fake_reset();
    fdb_fake_put("cfg/ch0", preset, sizeof(preset));
    CHECK(fdb_kvdb_init(NULL, PARAM_DB_NAME, BOARD_PARAM_PARTITION, &fixture, NULL) == FDB_NO_ERR);
    blob.saved.len = 0U;
    n              = fdb_kv_get_blob(NULL, "cfg/ch0", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK((n == sizeof(preset)) && (memcmp(buf, preset, sizeof(preset)) == 0));
    blob.saved.len = 0U;
    n              = fdb_kv_get_blob(NULL, "dev/addr", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK((n == 0U) && (blob.saved.len == 0U));

    /* 零长度默认行是 C 字符串（fdb_kvdb.c fdb_kv_set_default）：
     * 存储长度是 strlen(value)，而不是 0。 */
    fdb_fake_reset();
    (void)fdb_kvdb_init(NULL, PARAM_DB_NAME, BOARD_PARAM_PARTITION, &str_fixture, NULL);
    blob.saved.len = 0U;
    n              = fdb_kv_get_blob(NULL, "str/k", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK((n == 3U) && (memcmp(buf, "abc", 3U) == 0));

    /* 正文为 NULL 的 set() 删除该键（fdb_kvdb.c set_kv）。 */
    fdb_fake_put("cfg/ch4", preset, sizeof(preset));
    (void)fdb_kv_set_blob(NULL, "cfg/ch4", fdb_blob_make(&blob, NULL, 0U));
    blob.saved.len = 0U;
    n              = fdb_kv_get_blob(NULL, "cfg/ch4", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK(n == 0U);

    /* 关闭的数据库按厂商的 db_init_ok 守卫应答：get 报告缺失且不改动 saved.len，
     * set 在记录任何尝试之前返回 FDB_INIT_FAILED。 */
    fdb_fake_reset();
    fdb_fake.fail_init = 1U;
    CHECK(fdb_kvdb_init(NULL, PARAM_DB_NAME, BOARD_PARAM_PARTITION,
                        &fixture, NULL)
          == FDB_INIT_FAILED);
    blob.saved.len = 77U;
    n              = fdb_kv_get_blob(NULL, "cfg/ch0", fdb_blob_make(&blob, buf, sizeof(buf)));
    CHECK((n == 0U) && (blob.saved.len == 77U));
    CHECK(fdb_kv_set_blob(NULL, "cfg/ch0", fdb_blob_make(&blob, preset, sizeof(preset)))
          == FDB_INIT_FAILED);
    CHECK(fdb_fake.set_calls == 0U);
    fdb_fake.fail_init = 0U;
}

/*----------------------------------------------------------------------------*/
/* 测试夹具                                                                   */
/*----------------------------------------------------------------------------*/

static modbus_ao_t ao;
static modbus_slave_t slave_storage;
static modbus_port_fake_t port;
static uint8_t rx_buffer[256];
static uint8_t tx_buffer[256];

static QSubscrList subscr[MAX_SIG];
static QF_MPOOL_EL(measure_evt_t) pool_a[APP_POOL_A_BLOCK_NUM];
static QF_MPOOL_EL(config_set_evt_t) pool_b[APP_POOL_B_BLOCK_NUM];

/* CONFIG_SET_SIG 的发布观察者。 */
static QActive recorder;
static QEvtPtr rec_queue[32];
static uint8_t rec_started;

/** @brief 自 setup 以来按顺序发布的配置。 */
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

/**
 * @brief 重置世界，并在全新的假介质之上构造 AO。
 *
 * @param db_fail 非零表示编排一次数据库 init 失败（b3 路径）。
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

    /* 观察者必须在任何写入能够发布之前就绪。 */
    QActive_ctor(&recorder, Q_STATE_CAST(&rec_handler));
    QActive_start(&recorder, Q_PRIO(30U, 0U),
                  rec_queue, 32U, (void*)0, 0U, (void*)0);
    rec_started = 1U;
    QActive_subscribe(&recorder, CONFIG_SET_SIG);

    published_count = 0U;

    /* 真实但未启动的从站核心：由 wiring() 驱动它的回调。 */
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

/** @brief 核心自己的回调副本，已接到被测 AO 上。 */
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

/** @brief 把从 @p start 起 @p count 个寄存器写入一个块。 */
static modbus_slave_exception_t write_regs(const modbus_slave_callbacks_t* cb,
                                           uint16_t start,
                                           uint16_t count,
                                           const uint8_t* bytes)
{
    return cb->holding_registers(cb->ctx, start, count,
                                 (uint8_t*)bytes, MODBUS_SLAVE_REG_WRITE);
}

/** @brief 读取从 @p start 起的 @p count 个寄存器。 */
static modbus_slave_exception_t read_regs(const modbus_slave_callbacks_t* cb,
                                          uint16_t start,
                                          uint16_t count,
                                          uint8_t* bytes)
{
    return cb->holding_registers(cb->ctx, start, count,
                                 bytes, MODBUS_SLAVE_REG_READ);
}

/** @brief 按总线顺序构建一个通道配置块。 */
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

/** @brief 通过输入回调读取器件故障字。 */
static uint16_t read_fault(const modbus_slave_callbacks_t* cb)
{
    uint8_t reply[2];

    CHECK(cb->input_registers(cb->ctx, REG_MAP_SYS_BASE + 4U, 1U, reply,
                              MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_NONE);
    return modbus_slave_get_u16(reply);
}

/*----------------------------------------------------------------------------*/
/* 回调层的启动状态                                                           */
/*----------------------------------------------------------------------------*/

/** @brief 全新镜像用构造函数默认值应答。 */
static void test_init_state(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t one[2];

    setup_normal();
    cb = wiring();

    CHECK(fdb_fake.init_calls == 1U);
    CHECK(fdb_fake.set_calls == 0U); /* 填充不算一次固化 */
    CHECK(ao.store.db_ok == 1U);
    CHECK(ao.store.commit_failed == 0U);
    CHECK(ao.config.addr == REG_MAP_DEFAULT_DEV_ADDR);
    CHECK(cb->address(cb->ctx) == (uint8_t)REG_MAP_DEFAULT_DEV_ADDR);
    CHECK(ao.telemetry.status[0] == (uint16_t)CH_STATUS_UNCONFIGURED);
    CHECK(ao.config.applied[0] == 0U);
    CHECK(ao.config.cfg_ok[0] == 0U);

    /* 未映射的空间保持诚实。 */
    CHECK(cb->coils(cb->ctx, 0U, 1U, NULL, MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_ILLEGAL_FUNCTION);
    CHECK(cb->discrete_inputs(cb->ctx, 0U, 1U, NULL, MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_ILLEGAL_FUNCTION);
    CHECK(read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block) == MODBUS_SLAVE_EX_NONE);
    CHECK(modbus_slave_get_u16(&block[0]) == 0U);

    /* 输入寄存器是只读的。 */
    CHECK(cb->input_registers(cb->ctx, 0U, 1U, one, MODBUS_SLAVE_REG_WRITE)
          == MODBUS_SLAVE_EX_ILLEGAL_FUNCTION);
}

/*----------------------------------------------------------------------------*/
/* 校验矩阵（EX 03 路径）                                                     */
/*----------------------------------------------------------------------------*/

/** @brief 非法候选会拒绝整帧，且不触碰任何东西。 */
static void test_validation_matrix(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t before[REG_MAP_CH_CFG_LEN * 2U];
    uint16_t i;

    static const struct {
        uint8_t reg;   /* 要破坏的寄存器 */
        uint16_t word; /* 要植入的总线字 */
    } rejects[] = {
        {0U, 11U},     /* 没有对应输入类型的复合类型码 */
        {0U, 0xFFFFU}, /* 未知复合码 -> 无效哨兵值 */
        {0U, 0x63U},   /* 类型码 99：没有这种输入类型 */
        {1U, 13U},     /* 越过最后一档的速率码 */
        {1U, 0xFFFFU}, /* 没有对应档位的速率码 */
    };

    setup_normal();
    cb = wiring();

    /* 先写一个合法块，这样被拒绝时有镜像可保持不动。 */
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
    CHECK(fdb_fake.set_calls == 1U); /* 只有上面那一帧是合法的 */
    rec_collect();
    CHECK(published_count == 1U);

    /* 校准字段中的 NaN / Inf 同样会被拒绝。 */
    {
        static const struct {
            uint8_t reg;
            uint16_t hi;
            uint16_t lo;
        } bad_floats[] = {
            {2U, 0x7FC0U, 0x0000U}, /* NaN 线性下限 */
            {4U, 0x7F80U, 0x0000U}, /* +Inf 线性上限 */
            {6U, 0xFF80U, 0x0000U}, /* -Inf 偏移 */
            {8U, 0x7FC0U, 0x0000U}, /* NaN 增益 */
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

    /* 跨段范围是映射错误，而不是值错误。 */
    CHECK(write_regs(cb, 10U, 1U, before) == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
    CHECK(write_regs(cb, 0U, 11U, before) == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
    CHECK(cb->input_registers(cb->ctx, 72U, 1U, before, MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
}

/*----------------------------------------------------------------------------*/
/* 写事务                                                                     */
/*----------------------------------------------------------------------------*/

/** @brief 整块写入会固化、更新镜像、发布并回读。 */
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

    /* 固化发生了一次，KV 正文就是编码后的块。 */
    CHECK(fdb_fake.set_calls == 1U);
    CHECK(strcmp(fdb_fake.last_key, "cfg/ch0") == 0);
    CHECK(fdb_fake.last_len == sizeof(ch_config_t));
    saved = last_cfg();
    CHECK(saved.type == (uint8_t)CH_TYPE_4_20MA);
    CHECK(saved.rate_code == 9U);
    CHECK(saved.linear_max == 20.0f);
    CHECK(saved.gain == 2.0f);

    /* 镜像与变更检查基准随固化一起前移。 */
    CHECK(ao.config.cfg[0].type == (uint8_t)CH_TYPE_4_20MA);
    CHECK(ao.config.cfg[0].gain == 2.0f);
    CHECK(ao.config.cfg_ok[0] == 1U);
    CHECK(ao.config.applied[0] == 0U); /* 已持久化，尚未应用 */
    CHECK(ao.store.commit_failed == 0U);

    /* 一次发布，携带已固化的配置。 */
    rec_collect();
    CHECK(published_count == 1U);
    CHECK(published[0].ch == 0U);
    CHECK(published[0].cfg.type == (uint8_t)CH_TYPE_4_20MA);
    CHECK(published[0].cfg.rate_code == 9U);

    /* 下一次读取返回的正是写入的内容。 */
    CHECK(read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, echo) == MODBUS_SLAVE_EX_NONE);
    CHECK(memcmp(echo, block, sizeof(block)) == 0);

    /* 写事务同时也是归一化点：非线性配置的线性限值会被清零返回
     *（ch_config_t 文档化的契约）。 */
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

/** @brief 单寄存器写入通过读-改-写落地。 */
static void test_write_single_register(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t one[2];
    uint8_t echo[REG_MAP_CH_CFG_LEN * 2U];

    setup_normal();
    cb = wiring();

    /* 从 50 Hz 开始（相对任何值都是变更，因此会固化）。 */
    make_block((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 9U,
               0.0f, 0.0f, 0.0f, 1.0f, block);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block) == MODBUS_SLAVE_EX_NONE);

    /* 寄存器 1 是速率码：10 Hz（码 2）；其余全部保留。 */
    modbus_slave_put_u16(one, 2U);
    CHECK(write_regs(cb, 1U, 1U, one) == MODBUS_SLAVE_EX_NONE);
    CHECK(ao.config.cfg[0].rate_code == 2U);
    CHECK(ao.config.cfg[0].type == (uint8_t)CH_TYPE_TC);
    CHECK(ao.config.cfg[0].gain == 1.0f);

    CHECK(read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, echo) == MODBUS_SLAVE_EX_NONE);
    CHECK(modbus_slave_get_u16(&echo[2]) == 2U);
    rec_collect();
    CHECK(published_count == 2U);

    /* 写入类型会同时重映射 tc_type，并且仍会固化。 */
    modbus_slave_put_u16(one, 8U);
    CHECK(write_regs(cb, 0U, 1U, one) == MODBUS_SLAVE_EX_NONE);
    CHECK(ao.config.cfg[0].type == (uint8_t)CH_TYPE_PT100);
    CHECK(ao.config.cfg[0].tc_type == CH_TC_TYPE_NONE);
    CHECK(ao.config.cfg[0].rate_code == 2U);

    /* 单个非法寄存器就会拒绝整个候选。 */
    modbus_slave_put_u16(one, 13U);
    CHECK(write_regs(cb, 1U, 1U, one) == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);
    CHECK(ao.config.cfg[0].rate_code == 2U);
}

/** @brief 同值写入会短路存储，但会重新通知。 */
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

    /* 未应用时的同值：没有第二次写入，但通道会收到新的 CONFIG_SET，
     * 因此重写同一块即可重试失败的 apply。一旦通道以 CFG_APPLIED_SIG
     * 应答，同值就是真正的空操作（在 test_modbus_ao.c 中覆盖）。 */
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block) == MODBUS_SLAVE_EX_NONE);
    CHECK(fdb_fake.set_calls == 1U);
    rec_collect();
    CHECK(published_count == 2U);
    CHECK(published[1].ch == 0U);

    /* 同一短路逻辑同样守护地址寄存器。 */
    {
        uint8_t one[2];

        modbus_slave_put_u16(one, 1U);
        CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one) == MODBUS_SLAVE_EX_NONE);
        CHECK(fdb_fake.set_calls == 2U); /* 首次地址写入会固化 */
        CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one) == MODBUS_SLAVE_EX_NONE);
        CHECK(fdb_fake.set_calls == 2U); /* 同值：存储保持不变 */
    }
    rec_collect();
    CHECK(published_count == 2U); /* 地址写入从不发布 */
}

/** @brief 回读不正确就不固化：EX 04、b1，重试成功。 */
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

    /* 变更值若回读不正确就不会固化。 */
    make_block((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 10U,
               0.0f, 0.0f, 0.0f, 3.0f, next_block);
    fdb_fake.corrupt_read = 1U;
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, next_block)
          == MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE);
    fdb_fake.corrupt_read = 0U;
    CHECK(ao.store.commit_failed != 0U);
    CHECK(ao.config.cfg[0].rate_code == 9U); /* 变更检查基准未动 */
    read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block);
    CHECK(memcmp(block, before, sizeof(block)) == 0);
    rec_collect();
    CHECK(published_count == 1U);

    /* 该锁存可通过故障字看到（b2 是这一层固有的：没有启动加载时 cfg_ok
     * 保持 0，b0 需要 CJC 心跳）。 */
    CHECK((read_fault(cb) & REG_MAP_FAULT_PARAM_WRITE) != 0U);

    /* 重试不会被短路（变更检查基准从未移动）。 */
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, next_block) == MODBUS_SLAVE_EX_NONE);
    CHECK(fdb_fake.set_calls == 3U);
    CHECK(ao.store.commit_failed == 0U);
    CHECK((read_fault(cb) & REG_MAP_FAULT_PARAM_WRITE) == 0U);
    rec_collect();
    CHECK(published_count == 2U);

    /* 写入失败的行为等同于回读不正确。 */
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
/* 器件寄存器                                                                 */
/*----------------------------------------------------------------------------*/

/** @brief 地址写入会固化、校验边界，并像配置一样失败。 */
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
    CHECK(published_count == 0U); /* 地址写入从不发布 */

    /* 地址合法性矩阵（寄存器 128 写入校验）。 */
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

    /* 地址固化失败的行为等同于配置固化失败。 */
    modbus_slave_put_u16(one, 200U);
    fdb_fake.corrupt_read = 1U;
    CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one)
          == MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE);
    fdb_fake.corrupt_read = 0U;
    CHECK(ao.store.commit_failed != 0U);
    CHECK(ao.config.addr == 247U); /* 未采纳 */
    CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one) == MODBUS_SLAVE_EX_NONE);
    CHECK(ao.store.commit_failed == 0U);
    CHECK(ao.config.addr == 200U);
}

/*----------------------------------------------------------------------------*/
/* 存储生命周期与故障字                                                       */
/*----------------------------------------------------------------------------*/

/** @brief 数据库打开失败会安全失败（EX 04、b3，无 b2）。 */
static void test_init_failure(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t one[2];

    /* 数据库从未打开：b3 被锁存，没有任何内容被填充。 */
    setup(1U);
    cb = wiring();
    CHECK(fdb_fake.init_calls == 1U);
    CHECK(ao.store.db_ok == 0U);
    CHECK(read_fault(cb) == (uint16_t)(REG_MAP_FAULT_FDB_INIT | REG_MAP_FAULT_CJC));

    /* 每次固化都安全失败：EX 04、b1 被锁存，存储保持不变。 */
    make_block((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 9U,
               0.0f, 0.0f, 0.0f, 1.0f, block);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
          == MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE);
    CHECK(fdb_fake.set_calls == 0U);
    CHECK(ao.store.commit_failed != 0U);
    CHECK(read_fault(cb)
          == (uint16_t)(REG_MAP_FAULT_FDB_INIT | REG_MAP_FAULT_PARAM_WRITE
                        | REG_MAP_FAULT_CJC));
    CHECK(ao.store.db_ok == 0U);

    /* 地址固化的行为完全相同。 */
    modbus_slave_put_u16(one, 247U);
    CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one)
          == MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE);
    CHECK(read_fault(cb)
          == (uint16_t)(REG_MAP_FAULT_FDB_INIT | REG_MAP_FAULT_PARAM_WRITE
                        | REG_MAP_FAULT_CJC));
}

/**
 * @brief 遥测镜像通过输入回调编码。
 *
 * 镜像字段由事件层的 MEASURE_SIG/CJC_SIG 处理器写入；这一层直接写入它们以
 * 证明编码路径。
 */
static void test_telemetry_mirror(void)
{
    const modbus_slave_callbacks_t* cb;
    uint8_t reply[8];
    uint32_t bits;
    float value;

    setup_normal();
    cb = wiring();

    ao.telemetry.value[1]  = 42.5f;
    ao.telemetry.status[1] = (uint16_t)CH_STATUS_OK;
    ao.telemetry.cjc_temp  = 25.0f;
    ao.telemetry.cjc_valid = 1U;

    /* 通道 2 遥测块：寄存器 8..10。 */
    CHECK(cb->input_registers(cb->ctx, 8U, 3U, reply, MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_NONE);
    bits = ((uint32_t)modbus_slave_get_u16(&reply[0]) << 16)
           | (uint32_t)modbus_slave_get_u16(&reply[2]);
    memcpy(&value, &bits, sizeof(value));
    CHECK(value == 42.5f);
    CHECK(modbus_slave_get_u16(&reply[4]) == (uint16_t)CH_STATUS_OK);

    /* 冷端有效时，这一层的故障字本应是干净的：但 cfg_ok/addr_ok 仍为 0
     *（此处没有启动加载），所以 b2 会显现。 */
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
