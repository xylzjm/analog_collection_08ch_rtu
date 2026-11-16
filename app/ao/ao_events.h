#ifndef AO_EVENTS_H
#define AO_EVENTS_H

/**
 * @file ao_events.h
 * @brief Application signal numbers, dynamic event payloads and pool sizing.
 *
 * Signal set and payload layouts follow docs/qp-application-design.md
 * section 4 (event contract). Static events (frame/tx/tick/apply/sample/
 * settle/break) carry no parameters and live with their owners.
 */

#include "qpc.h"

#include "ch_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Application signals. */
typedef enum {
    SIG_CONFIG_SET = Q_USER_SIG, /**< {ch, cfg}: effective configuration. */
    SIG_APPLY,                   /**< Reconfiguration re-entry (static). */
    SIG_MEASURE,                 /**< {ch, value, status}: one sample. */
    SIG_CJC,                     /**< {temp_c, valid}: cold junction heartbeat. */
    SIG_SAMPLE,                  /**< Sampling tick (time event). */
    SIG_SETTLE,                  /**< Settle/check deadline (time event). */
    SIG_BREAK_WINDOW,            /**< 3 s wire break window (time event). */
    SIG_TICK,                    /**< Cold junction 1 Hz tick (time event). */
    SIG_FRAME,                   /**< Modbus frame captured (static). */
    SIG_TX_DONE,                 /**< Modbus reply sent (static). */
    SIG_TX_TIMEOUT,              /**< Modbus TX watchdog fired (static). */
    MAX_SIG                      /**< Number of signals (psInit bound). */
} app_signal_t;

/** @brief SIG_CONFIG_SET payload (pool B). */
typedef struct {
    QEvt        super; /**< Event header. */
    uint8_t     ch;    /**< One-based channel number. */
    ch_config_t cfg;   /**< New effective configuration. */
} config_set_evt_t;

/** @brief SIG_MEASURE payload (pool A). */
typedef struct {
    QEvt     super;  /**< Event header. */
    uint8_t  ch;     /**< One-based channel number. */
    float    value;  /**< Calibrated engineering value (frozen on faults). */
    uint16_t status; /**< Channel status code (reg_map_status_t). */
} measure_evt_t;

/** @brief SIG_CJC payload (pool A). */
typedef struct {
    QEvt    super;   /**< Event header. */
    float   temp_c;  /**< Latest valid cold junction temperature. */
    uint8_t valid;   /**< Non-zero when temp_c may be used. */
} cjc_evt_t;

/*----------------------------------------------------------------------------*/
/* Event pool sizing (docs/qp-application-design.md section 7)                */
/*----------------------------------------------------------------------------*/

/** @brief Pool A block size: SIG_MEASURE and SIG_CJC events. */
#define APP_POOL_A_EVT_SIZE 32U

/** @brief Pool A block count. */
#define APP_POOL_A_BLOCK_NUM 16U

/** @brief Pool B block size: SIG_CONFIG_SET events. */
#define APP_POOL_B_EVT_SIZE 40U

/** @brief Pool B block count. */
#define APP_POOL_B_BLOCK_NUM 16U

/*----------------------------------------------------------------------------*/

/* Event payloads must fit their pool blocks. */
Q_ASSERT_STATIC(sizeof(config_set_evt_t) <= APP_POOL_B_EVT_SIZE);
Q_ASSERT_STATIC(sizeof(measure_evt_t) <= APP_POOL_A_EVT_SIZE);
Q_ASSERT_STATIC(sizeof(cjc_evt_t) <= APP_POOL_A_EVT_SIZE);

#ifdef __cplusplus
}
#endif

#endif
