#ifndef AO_CHANNEL_H
#define AO_CHANNEL_H

/**
 * @file ao_channel.h
 * @brief Channel acquisition active object (one instance per channel).
 *
 * Owns the whole acquisition life cycle of one channel: configuration
 * application, sampling, calibration, the wire break detection loop and the
 * channel status. See docs/ao/ao_channel.md.
 */

#include "analog_input.h"
#include "qpc.h"

#include "ch_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Queue depth (boot burst plus tick events plus margin). */
#define AO_CHANNEL_QUEUE_LEN 13U

/** @brief Wire break check period in ticks (3 s). */
#define AO_CHANNEL_BREAK_WINDOW_MS 3000U

/** @brief AoChannel active object. */
typedef struct {
    QActive  super;     /**< Active object base. */
    QTimeEvt te_sample; /**< Sampling period timer. */
    QTimeEvt te_settle; /**< Settle / check deadline timer. */
    QTimeEvt te_break;  /**< Wire break window timer. */
    QEvt     applyEvt;  /**< Static SIG_APPLY event (reconfiguration). */
    analog_input_t* input;   /**< Board channel object (borrowed). */
    ch_config_t     cfg;     /**< Effective configuration. */
    ch_config_t     pending; /**< Configuration awaiting SIG_APPLY. */
    float    cjc_temp;    /**< Cached cold junction temperature. */
    float    last_value;  /**< Latest published value (frozen on faults). */
    uint32_t settle_ms;   /**< Delay for the next Settling entry. */
    uint8_t  cjc_valid;   /**< Cached cold junction validity. */
    uint8_t  wire;        /**< analog_input_wire_status_t of the last check. */
    uint8_t  ch;          /**< One-based board channel. */
} ao_channel_t;

/**
 * @brief Construct one channel active object.
 *
 * @param me Storage owned by the caller.
 * @param ch One-based board channel (1..8).
 */
void ao_channel_ctor(ao_channel_t* me,
                     uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif
