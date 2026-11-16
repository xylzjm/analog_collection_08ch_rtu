#include <stdio.h>

#include "board.h"
#include "qpc.h"
#include "SEGGER_RTT.h"
#include "elog.h"

#include "ao_channel.h"
#include "ao_cold_junction.h"
#include "ao_events.h"
#include "ao_modbus.h"

/** @brief Route EasyLogger assertions to the project error handler. */
static void elog_assert_handler(char const* expr,
                                char const* func,
                                size_t line)
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

static ao_channel_t l_channel[CH_CONFIG_CHANNEL_COUNT];
static ao_cold_junction_t l_cold_junction;
static ao_modbus_t l_modbus;

static QEvtPtr l_ch_queue[CH_CONFIG_CHANNEL_COUNT][AO_CHANNEL_QUEUE_LEN];
static QEvtPtr l_cjc_queue[AO_COLD_JUNCTION_QUEUE_LEN];
static QEvtPtr l_modbus_queue[AO_MODBUS_QUEUE_LEN];

static QSubscrList l_subscrSto[MAX_SIG];

/* Pool blocks must stay aligned for the event headers. */
static uint32_t l_pool_a[(APP_POOL_A_EVT_SIZE * APP_POOL_A_BLOCK_NUM) / 4U];
static uint32_t l_pool_b[(APP_POOL_B_EVT_SIZE * APP_POOL_B_BLOCK_NUM) / 4U];

int main(void)
{
    uint8_t i;

    log_init();
    board_init();

    QF_init();
    QActive_psInit(l_subscrSto, MAX_SIG);
    QF_poolInit(l_pool_a, sizeof(l_pool_a), APP_POOL_A_EVT_SIZE);
    QF_poolInit(l_pool_b, sizeof(l_pool_b), APP_POOL_B_EVT_SIZE);

    for ( i = 0U; i < CH_CONFIG_CHANNEL_COUNT; ++i ) {
        ao_channel_ctor(&l_channel[i], (uint8_t)(i + 1U));
    }
    ao_cold_junction_ctor(&l_cold_junction);
    ao_modbus_ctor(&l_modbus);

    /*
     * Subscribers before publishers: every channel is started and subscribed
     * before AoModbus runs its boot load, which publishes SIG_CONFIG_SET.
     * QActive_subscribe() requires the AO to be registered already, so each
     * subscription follows its own start.
     */
    for ( i = 0U; i < CH_CONFIG_CHANNEL_COUNT; ++i ) {
        QActive_start(&l_channel[i].super, Q_PRIO((uint8_t)(2U + i), 0U),
                      l_ch_queue[i], AO_CHANNEL_QUEUE_LEN,
                      (void*)0, 0U, (void*)0);
        QActive_subscribe(&l_channel[i].super, SIG_CONFIG_SET);
        QActive_subscribe(&l_channel[i].super, SIG_CJC);
    }

    QActive_start(&l_cold_junction.super, Q_PRIO(10U, 0U),
                  l_cjc_queue, AO_COLD_JUNCTION_QUEUE_LEN,
                  (void*)0, 0U, (void*)0);

    QActive_start(&l_modbus.super, Q_PRIO(11U, 0U),
                  l_modbus_queue, AO_MODBUS_QUEUE_LEN,
                  (void*)0, 0U, (void*)0);
    QActive_subscribe(&l_modbus.super, SIG_MEASURE);
    QActive_subscribe(&l_modbus.super, SIG_CJC);

    return QF_run();
}
