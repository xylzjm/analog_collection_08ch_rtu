#ifndef AO_EVENTS_H
#define AO_EVENTS_H

/**
 * @file ao_events.h
 * @brief Application signal numbers, dynamic event payloads and pool sizing.
 *
 * Signal set and payload layouts follow docs/qp-application-design.md
 * section 4 (event contract). Static events carry no parameters:
 * parameterless ones are file-scope `QEvt const` (QEVT_INITIALIZER), time
 * events are QTimeEvt members of their AO (QP/C example form).
 */

#include "qpc.h"

#include "ch_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Application signals. */
typedef enum {
    CONFIG_SET_SIG = Q_USER_SIG, /**< {ch, cfg}: effective configuration. */
    APPLY_SIG,                   /**< Reconfiguration re-entry (static). */
    MEASURE_SIG,                 /**< {ch, value, status}: one sample. */
    CJC_SIG,                     /**< {temp_c, valid}: cold junction heartbeat. */
    SAMPLE_SIG,                  /**< Sampling tick (time event). */
    SETTLE_SIG,                  /**< Settle/check deadline (time event). */
    BREAK_WINDOW_SIG,            /**< 3 s wire break window (time event). */
    TICK_SIG,                    /**< Cold junction 1 Hz tick (time event). */
    FRAME_SIG,                   /**< Modbus frame captured (static). */
    TX_DONE_SIG,                 /**< Modbus reply sent (static). */
    TX_TIMEOUT_SIG,              /**< Modbus TX watchdog fired (static). */
    MAX_SIG                      /**< Number of signals (psInit bound). */
} app_signal_t;

/** @brief CONFIG_SET_SIG payload (pool B). */
typedef struct {
    QEvt        super; /**< Event header. */
    uint8_t     ch;    /**< One-based channel number. */
    ch_config_t cfg;   /**< New effective configuration. */
} config_set_evt_t;

/** @brief MEASURE_SIG payload (pool A). */
typedef struct {
    QEvt     super;  /**< Event header. */
    uint8_t  ch;     /**< One-based channel number. */
    float    value;  /**< Calibrated engineering value (frozen on faults). */
    uint16_t status; /**< Channel status code (reg_map_status_t). */
} measure_evt_t;

/** @brief CJC_SIG payload (pool A). */
typedef struct {
    QEvt    super;   /**< Event header. */
    float   temp_c;  /**< Latest valid cold junction temperature. */
    uint8_t valid;   /**< Non-zero when temp_c may be used. */
} cjc_evt_t;

/*----------------------------------------------------------------------------*/
/* Event pool sizing (docs/qp-application-design.md section 7)                */
/*----------------------------------------------------------------------------*/

/** @brief Pool A block count. */
#define APP_POOL_A_BLOCK_NUM 16U

/** @brief Pool B block count. */
#define APP_POOL_B_BLOCK_NUM 16U

/* Pool A holds MEASURE_SIG and CJC_SIG; measure_evt_t must be the larger. */
Q_ASSERT_STATIC(sizeof(cjc_evt_t) <= sizeof(measure_evt_t));

#ifdef __cplusplus
}
#endif

#endif
