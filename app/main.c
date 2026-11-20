#include <stdio.h>

#include "SEGGER_RTT.h"
#include "board.h"
#include "elog.h"
#include "qpc.h"

#include "analog_input_board.h"
#include "app.h"
#include "channel_ao.h"
#include "cold_junction_ao.h"
#include "kv_store_board.h"
#include "modbus_ao.h"
#include "modbus_slave_board.h"
#include "tmp75_board.h"

/** @brief Route EasyLogger assertions to the project error handler. */
static void elog_assert_handler(char const* expr, char const* func, size_t line)
{
    (void)expr;
    (void)func;
    Q_onError("EasyLogger", (int_t)line);
}

/** @brief Bring up SEGGER RTT and EasyLogger before any logging. */
static void log_init(void)
{
    uint8_t level;

    /* RTT must exist before EasyLogger and the printf fallback are used. */
    SEGGER_RTT_Init();

    if ( elog_init() != ELOG_NO_ERR ) {
        printf("log: elog_init failed\r\n");
        return;
    }

    elog_assert_set_hook(elog_assert_handler);

    /* Every level needs ELOG_FMT_LVL; elog_find_lvl() asserts otherwise. */
    for ( level = ELOG_LVL_ASSERT; level < ELOG_LVL_TOTAL_NUM; ++level ) {
        elog_set_fmt(level, ELOG_FMT_LVL | ELOG_FMT_TAG);
    }
    elog_start();
}

/*----------------------------------------------------------------------------*/
/* Composition root                                                           */
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

    /* The store handle and the platform half-config; the AO builds the
     * slave core and opens the database in its constructor. */
    kv_store_board_ctor();
    modbus_ao_ctor(&modbus, kv_store_board_get(),
                   modbus_slave_board_get(), modbus_slave_board_config());

    for ( i = 0U; i < CH_CONFIG_CHANNEL_COUNT; ++i ) {
        channel_ao_ctor(&channels[i], (uint8_t)i,
                        analog_input_board_get((uint8_t)(i + 1U)));
    }
    cold_junction_ao_ctor(&cold_junction, tmp75_board_get_device());

    /*
     * Start order: channels first, then AoModbus (its boot load publishes
     * CONFIG_SET_SIG to the channels), then AoColdJunction so its first CJC_SIG
     * reaches every subscriber. Every active object subscribes inside its own
     * state-machine initial transition (QP/C example style; QActive_start()
     * registers the AO before QASM_INIT).
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
