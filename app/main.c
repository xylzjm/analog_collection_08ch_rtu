#include <stdio.h>

#include "SEGGER_RTT.h"
#include "board.h"
#include "elog.h"
#include "qpc.h"

#include "analog_input_board.h"
#include "app.h"
#include "channel_ao.h"
#include "cold_junction_ao.h"
#include "modbus_ao.h"
#include "modbus_slave_board.h"
#include "tmp75_board.h"

/** @brief 将 EasyLogger 断言转接到项目错误处理函数。 */
static void elog_assert_handler(char const* expr, char const* func, size_t line)
{
    (void)expr;
    (void)func;
    Q_onError("EasyLogger", (int_t)line);
}

/** @brief 在任何日志之前初始化 SEGGER RTT 与 EasyLogger。 */
static void log_init(void)
{
    uint8_t level;

    /* RTT 必须先于 EasyLogger 和 printf 兜底路径可用。 */
    SEGGER_RTT_Init();

    if ( elog_init() != ELOG_NO_ERR ) {
        printf("log: elog_init failed\r\n");
        return;
    }

    elog_assert_set_hook(elog_assert_handler);

    /* 每个级别都需要 ELOG_FMT_LVL；否则 elog_find_lvl() 会断言。 */
    for ( level = ELOG_LVL_ASSERT; level < ELOG_LVL_TOTAL_NUM; ++level ) {
        elog_set_fmt(level, ELOG_FMT_LVL | ELOG_FMT_TAG);
    }
    elog_start();
}

/*----------------------------------------------------------------------------*/
/* 组合根                                                                     */
/*----------------------------------------------------------------------------*/

static channel_ao_t channels[CH_CONFIG_CHANNEL_COUNT];
static cold_junction_ao_t cold_junction;
static modbus_ao_t modbus;

static QEvtPtr ch_queue[CH_CONFIG_CHANNEL_COUNT][CHANNEL_AO_QUEUE_LEN];
static QEvtPtr cjc_queue[COLD_JUNCTION_AO_QUEUE_LEN];
static QEvtPtr modbus_queue[MODBUS_AO_QUEUE_LEN];

static QSubscrList subscrSto[MAX_SIG];

static QF_MPOOL_EL(measure_evt_t) pool_a[APP_POOL_A_BLOCK_NUM];
static QF_MPOOL_EL(config_set_evt_t) pool_b[APP_POOL_B_BLOCK_NUM];

int main(void)
{
    uint8_t i;

    log_init();
    board_init();

    QF_init();
    QActive_psInit(subscrSto, MAX_SIG);
    QF_poolInit(pool_a, sizeof(pool_a), sizeof(pool_a[0]));
    QF_poolInit(pool_b, sizeof(pool_b), sizeof(pool_b[0]));

    /* 平台半配置；AO 在构造函数中构建从站核心并打开参数数据库。 */
    modbus_ao_ctor(&modbus, modbus_slave_board_get(),
                   modbus_slave_board_config());

    for ( i = 0U; i < CH_CONFIG_CHANNEL_COUNT; ++i ) {
        channel_ao_ctor(&channels[i], (uint8_t)i,
                        analog_input_board_get((uint8_t)(i + 1U)));
    }
    cold_junction_ao_ctor(&cold_junction, tmp75_board_get_device());

    /*
     * 启动顺序：先通道，再 AoModbus（其启动加载向各通道发布
     * CONFIG_SET_SIG），最后 AoColdJunction，使其首条 CJC_SIG 能到达
     * 每个订阅者。每个活动对象都在自己的状态机初始转换中订阅（QP/C 示例
     * 风格；QActive_start() 在 QASM_INIT 之前注册该 AO）。
     */
    for ( i = 0U; i < CH_CONFIG_CHANNEL_COUNT; ++i ) {
        QActive_start(Q_ACTIVE_UPCAST(&channels[i]),
                      Q_PRIO((uint8_t)(APP_PRIO_CH_FIRST + i), 0U),
                      ch_queue[i], CHANNEL_AO_QUEUE_LEN,
                      (void*)0, 0U,
                      (void*)0);
    }

    QActive_start(Q_ACTIVE_UPCAST(&modbus),
                  Q_PRIO(APP_PRIO_MODBUS, 0U),
                  modbus_queue, MODBUS_AO_QUEUE_LEN,
                  (void*)0, 0U,
                  (void*)0);

    QActive_start(Q_ACTIVE_UPCAST(&cold_junction),
                  Q_PRIO(APP_PRIO_CJC, 0U),
                  cjc_queue, COLD_JUNCTION_AO_QUEUE_LEN,
                  (void*)0, 0U,
                  (void*)0);

    return QF_run();
}
