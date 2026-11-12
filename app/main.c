#include <stdio.h>

#include "board.h"
#include "qpc.h"
#include "SEGGER_RTT.h"
#include "elog.h"

#include "app_evt.h"
#include "cal_data.h"
#include "common_app.h"
#include "modbus_app.h"
#include "modbus_app_regs.h"
#include "param_app.h"
#include "storage_app.h"
#include "storage_flash.h"

/** @brief Event pools sized for the payload types that use them. */
static cal_save_evt_t cal_pool[8];
static meas_evt_t meas_pool[64];
static cfg_evt_t cfg_pool[16];

static QSubscrList subscr[APP_SIG_MAX];

static QEvtPtr modbus_queue[32];
static QEvtPtr storage_queue[8];

static modbus_app modbus_obj;
static storage_app storage_obj;

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

int main(void)
{
    bool load_fault;

    log_init();

    QF_init();
    QActive_psInit(subscr, APP_SIG_MAX);
    QF_poolInit(cal_pool, (uint_fast32_t)sizeof(cal_pool),
                (uint_fast16_t)sizeof(cal_save_evt_t));
    QF_poolInit(meas_pool, (uint_fast32_t)sizeof(meas_pool),
                (uint_fast16_t)sizeof(meas_evt_t));
    QF_poolInit(cfg_pool, (uint_fast32_t)sizeof(cfg_pool),
                (uint_fast16_t)sizeof(cfg_evt_t));

    board_init();

    param_app_init();
    cal_data_init();
    load_fault = storage_load();
    cal_data_apply_all();

    modbus_app_ctor(&modbus_obj,
                    load_fault ? MODBUS_APP_DEV_FAULT_LOAD : 0U);
    storage_app_ctor(&storage_obj);

    QActive_start(&modbus_obj.super, Q_PRIO(20, 0), modbus_queue,
                  (uint_fast16_t)(sizeof(modbus_queue) / sizeof(modbus_queue[0])),
                  NULL, 0U, NULL);
    /* channel_app[0..7] and cold_junction_app stay stubs for this stage. */
    QActive_start(&storage_obj.super, Q_PRIO(9, 0), storage_queue,
                  (uint_fast16_t)(sizeof(storage_queue) / sizeof(storage_queue[0])),
                  NULL, 0U, NULL);

    QF_run();
}
