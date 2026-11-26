/**
 * @file test_boot.c
 * @brief 组合根启动顺序的宿主集成测试。
 *
 * 运行全部十个活动对象（8 个 AoChannel、AoColdJunction、AoModbus），在设备边界
 * 放置假件，并复刻 main.c 的启动顺序：通道先启动（各自在初始迁移中订阅），然后
 * AoModbus（其启动加载发布 CONFIG_SET_SIG），最后 AoColdJunction（其首个 CJC_SIG
 * 心跳）。断言验证 docs/qp-application-design.md 第 8 节的不变量：启动发布到达
 * 每个已订阅的通道，没有队列被抽干，遥测端到端流入 Modbus 镜像。通道的接受结果
 * 以 CFG_APPLIED_SIG 回传。
 *
 * 每个进程一个场景：QP 注册表和时间事件的拆除机制并不是为在单次运行内拆掉世界
 * 再重建而设计的，因此 main() 接受场景编号（1 或 2），脚本为每个场景启动一个
 * 进程。
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qpc.h"

#include "analog_input_fake.h"
#include "app.h"
#include "channel_ao.h"
#include "cold_junction_ao.h"
#include "fdb_fake.h"
#include "host_support.h"
#include "modbus_ao.h"
#include "modbus_port_fake.h"
#include "reg_map.h"
#include "tmp75_port_fake.h"

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

static channel_ao_t channels[BOARD_CHANNEL_COUNT];
static analog_input_fake_t inputs[BOARD_CHANNEL_COUNT];
static cold_junction_ao_t cold_junction;
static modbus_ao_t modbus;
static tmp75_t cjc_dev;
static tmp75_port_fake_t cjc_port;

static uint8_t rx_buffer[256];
static uint8_t tx_buffer[256];
static modbus_slave_t slave;
static modbus_port_fake_t port;

static QEvtPtr ch_queue[BOARD_CHANNEL_COUNT][CHANNEL_AO_QUEUE_LEN];
static QEvtPtr cjc_queue[COLD_JUNCTION_AO_QUEUE_LEN];
static QEvtPtr modbus_queue[MODBUS_AO_QUEUE_LEN];
static QSubscrList subscr[MAX_SIG];
static QF_MPOOL_EL(measure_evt_t) pool_a[APP_POOL_A_BLOCK_NUM];
static QF_MPOOL_EL(config_set_evt_t) pool_b[APP_POOL_B_BLOCK_NUM];

/*----------------------------------------------------------------------------*/
/* 测试夹具                                                                   */
/*----------------------------------------------------------------------------*/

/** @brief 所有已启动 AO 的队列，用于轮询排空。 */
static QActive* all_aos[BOARD_CHANNEL_COUNT + 2U];
static uint8_t all_count;

static void tick(uint32_t n)
{
    while ( n != 0U ) {
        QTIMEEVT_TICK_X(0U, (void*)0);
        ++g_test_tick;
        --n;
    }
}

static void drain_all(void)
{
    for ( ;; ) {
        uint8_t i;
        uint8_t dispatched = 0U;

        for ( i = 0U; i < all_count; ++i ) {
            if ( !QEQueue_isEmpty(&all_aos[i]->eQueue) ) {
                QEvt const* e = QActive_get_(all_aos[i]);

                QASM_DISPATCH(all_aos[i], e, 0U);
                QF_gc(e);
                dispatched = 1U;
            }
        }
        if ( dispatched == 0U ) {
            break;
        }
    }
}

/**
 * @brief 像 QV 循环那样，在持续排空的同时推进虚拟时间。
 *
 * 只批量推进节拍而不排空会溢出深度为 16 的 AoModbus 队列；目标内核则在每个
 * RTC 步进之后派发。
 */
static void run_ticks(uint32_t n)
{
    while ( n != 0U ) {
        tick(1U);
        drain_all();
        --n;
    }
}

/** @brief 通过 Modbus 数据空间映射读取器件故障字。 */
static uint16_t read_fault_word(void)
{
    const modbus_slave_callbacks_t* cb = &slave.callbacks;
    uint8_t reply[2];

    CHECK(cb->input_registers(cb->ctx, REG_MAP_SYS_BASE + 4U, 1U, reply,
                              MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_NONE);
    return modbus_slave_get_u16(reply);
}

/**
 * @brief 在设备边界使用假件，复刻 main.c 的启动顺序。
 *
 * @param fail_ch 加载失败通道的零基索引（255 表示没有）。
 */
static void boot_scenario(uint8_t fail_ch)
{
    uint8_t i;
    modbus_slave_config_t cfg = {0};
    tmp75_port_t cjc_port_if;

    memset(subscr, 0, sizeof(subscr));
    QActive_psInit(subscr, MAX_SIG);

    fdb_fake_reset();
    if ( fail_ch < BOARD_CHANNEL_COUNT ) {
        /* 部分存储绝不会被回填：用出厂正文预置每一行默认记录，再把其中一条正文
         * 破坏——真实加载器必须恰好拒绝那个键。 */
        static const ch_config_t cfg = {
            (uint8_t)CH_TYPE_TC,   /**< 类型 */
            (uint8_t)CH_TC_TYPE_K, /**< 热电偶类型 */
            2U,                    /**< rate_code：10 Hz */
            0.0f,                  /**< 线性下限 */
            0.0f,                  /**< 线性上限 */
            0.0f,                  /**< 偏移 */
            1.0f,                  /**< 增益 */
        };
        uint8_t bad[4] = {0};
        uint16_t addr  = REG_MAP_DEFAULT_DEV_ADDR;
        uint8_t ch;
        char key[FDB_FAKE_MAX_KEY];

        for ( ch = 0U; ch < BOARD_CHANNEL_COUNT; ++ch ) {
            (void)snprintf(key, sizeof(key), "cfg/ch%u", (unsigned)ch);
            fdb_fake_put(key, &cfg, sizeof(cfg));
        }
        fdb_fake_put("dev/addr", &addr, sizeof(addr));
        (void)snprintf(key, sizeof(key), "cfg/ch%u", (unsigned)fail_ch);
        fdb_fake_put(key, bad, sizeof(bad));
    }
    modbus_port_fake_init(&port);
    tmp75_port_fake_init(&cjc_port);
    tmp75_port_fake_port(&cjc_port, &cjc_port_if);
    CHECK(tmp75_ctor(&cjc_dev, &cjc_port_if, TMP75_ADDRESS_0) == TMP75_OK);

    /* 假件端口的平台部分；AO 填充回调/标识并构建核心。 */
    modbus_port_fake_config(&port, &cfg,
                            rx_buffer, (uint16_t)sizeof(rx_buffer),
                            tx_buffer, (uint16_t)sizeof(tx_buffer));
    modbus_ao_ctor(&modbus, &slave, &cfg);

    /* 通道优先：每个 AO 都在初始迁移中订阅。 */
    for ( i = 0U; i < BOARD_CHANNEL_COUNT; ++i ) {
        analog_input_fake_init(&inputs[i]);
        channel_ao_ctor(&channels[i], (uint8_t)i, &inputs[i].base);
        QActive_start(Q_ACTIVE_UPCAST(&channels[i]),
                      Q_PRIO((uint8_t)(APP_PRIO_CH_FIRST + i), 0U),
                      ch_queue[i], CHANNEL_AO_QUEUE_LEN, (void*)0, 0U, (void*)0);
        all_aos[i] = Q_ACTIVE_UPCAST(&channels[i]);
    }
    all_count = BOARD_CHANNEL_COUNT;

    /* AoModbus：其启动加载向通道发布 CONFIG_SET_SIG。 */
    QActive_start(Q_ACTIVE_UPCAST(&modbus), Q_PRIO(APP_PRIO_MODBUS, 0U),
                  modbus_queue, MODBUS_AO_QUEUE_LEN, (void*)0, 0U, (void*)0);
    all_aos[all_count] = Q_ACTIVE_UPCAST(&modbus);
    ++all_count;

    /* AoColdJunction 最后：其首次心跳到达每个订阅者。 */
    cold_junction_ao_ctor(&cold_junction, &cjc_dev);
    QActive_start(Q_ACTIVE_UPCAST(&cold_junction), Q_PRIO(APP_PRIO_CJC, 0U),
                  cjc_queue, COLD_JUNCTION_AO_QUEUE_LEN, (void*)0, 0U, (void*)0);
    all_aos[all_count] = Q_ACTIVE_UPCAST(&cold_junction);
    ++all_count;
}

/*----------------------------------------------------------------------------*/
/* 场景                                                                       */
/*----------------------------------------------------------------------------*/

/** @brief 完整启动：发布到达每个订阅者，遥测流动。 */
static void test_full_boot(void)
{
    uint8_t ch;
    uint8_t i;

    boot_scenario(255U);

    /* 每个通道都能看到每一次启动发布（接收方按 ch 过滤）以及首次 CJC 心跳：
     * 突发峰值时占用 13 个队列槽中的 9 个。 */
    for ( ch = 0U; ch < BOARD_CHANNEL_COUNT; ++ch ) {
        CHECK(QEQueue_getUse(&channels[ch].super.eQueue)
              == (uint16_t)(BOARD_CHANNEL_COUNT + 1U));
    }

    /* 突发量符合 docs/qp-application-design.md 第 7 节的容量预算：
     * 深度 13 覆盖 8 次发布 + 1 次心跳 + 余量。 */
    for ( i = 0U; i < BOARD_CHANNEL_COUNT; ++i ) {
        CHECK(QActive_getQueueMin((uint_fast8_t)(APP_PRIO_CH_FIRST + i)) >= 3U);
    }
    CHECK(QActive_getQueueMin(APP_PRIO_MODBUS) >= 8U);
    CHECK(QActive_getQueueMin(APP_PRIO_CJC) >= 1U);

    /* 先让每个通道稳定，再采样：10 Hz 每个值需要 100 ms。 */
    run_ticks(1200U);

    for ( ch = 0U; ch < BOARD_CHANNEL_COUNT; ++ch ) {
        CHECK(channels[ch].cjc_valid != 0U);
        CHECK(analog_input_fake_count(&inputs[ch],
                                      (uint8_t)ANALOG_INPUT_FAKE_READ_SAMPLE)
              >= 5U);
    }

    /* 镜像保存了每个通道的遥测；每份配置的接受结果以 CFG_APPLIED_SIG 到达。 */
    for ( ch = 0U; ch < BOARD_CHANNEL_COUNT; ++ch ) {
        CHECK(modbus.telemetry.value[ch] == 25.0f);
        CHECK(modbus.telemetry.status[ch] == (uint16_t)CH_STATUS_OK);
        CHECK(modbus.config.applied[ch] == 1U);
    }
    CHECK(modbus.telemetry.cjc_valid != 0U);
    CHECK(modbus.telemetry.cjc_temp == 25.0f);
    CHECK(modbus.store.db_ok == 1U);
    CHECK(modbus.config.addr_ok == 1U);
    CHECK(read_fault_word() == 0U);

    /* 事件池完全回收：两个池的每个块都已归还。 */
    drain_all();
    for ( i = 0U; i < all_count; ++i ) {
        CHECK(QEQueue_isEmpty(&all_aos[i]->eQueue));
    }
}

/** @brief 参数加载失败的通道永远不会开始测量。 */
static void test_failed_channel_stays_down(void)
{
    uint8_t ch;

    boot_scenario(2U); /* 通道 3（零基 2）加载失败 */

    run_ticks(1200U);

    CHECK(modbus.config.cfg_ok[2] == 0U);
    CHECK(modbus.telemetry.status[2] == (uint16_t)CH_STATUS_UNCONFIGURED);
    CHECK(modbus.config.applied[2] == 0U);
    CHECK(analog_input_fake_count(&inputs[2],
                                  (uint8_t)ANALOG_INPUT_FAKE_CONFIGURE)
          == 0U);

    /* 加载失败被锁存进故障字（b2）；冷端心跳有效，因此 b0 清零。 */
    CHECK(read_fault_word() == (uint16_t)REG_MAP_FAULT_PARAM_LOAD);

    /* 其余每个通道仍在运行。 */
    for ( ch = 0U; ch < BOARD_CHANNEL_COUNT; ++ch ) {
        if ( ch != 2U ) {
            CHECK(modbus.telemetry.value[ch] == 25.0f);
            CHECK(modbus.config.applied[ch] == 1U);
        }
    }
}

/*----------------------------------------------------------------------------*/

int main(int argc, char* argv[])
{
    QF_init();
    QF_poolInit(pool_a, sizeof(pool_a), sizeof(pool_a[0]));
    QF_poolInit(pool_b, sizeof(pool_b), sizeof(pool_b[0]));

    if ( (argc == 2) && (argv[1][0] == '2') ) {
        test_failed_channel_stays_down();
    } else if ( (argc == 2) && (argv[1][0] == '1') ) {
        test_full_boot();
    } else {
        printf("usage: test_boot <scenario>  (1 full boot, 2 failed channel)\n");
        return 2;
    }

    printf("test_boot: %d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
