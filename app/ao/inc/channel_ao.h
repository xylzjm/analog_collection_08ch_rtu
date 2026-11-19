#ifndef CHANNEL_AO_H
#define CHANNEL_AO_H

/**
 * @file channel_ao.h
 * @brief Channel acquisition active object (one instance per channel).
 *
 * Owns the whole acquisition life cycle of one channel: configuration
 * application, sampling, calibration, the wire break detection loop and the
 * channel status. See docs/ao/channel_ao.md.
 */

#include "analog_input.h"
#include "qpc.h"

#include "ch_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Configuration-apply result reporter; may be NULL. */
typedef void (*channel_ao_applied_fn)(void* ctx,
                                      uint8_t ch,
                                      uint8_t ok);

/** @brief Queue depth (boot burst plus tick events plus margin). */
#define CHANNEL_AO_QUEUE_LEN 13U

/** @brief Wire break check period in ticks (3 s). */
#define CHANNEL_AO_BREAK_WINDOW_MS 3000U

/** @brief AoChannel active object. */
typedef struct {
    QActive super;                    /**< Active object base. */
    QTimeEvt te_sample;               /**< Sampling period timer. */
    QTimeEvt te_settle;               /**< Settle / check deadline timer. */
    QTimeEvt te_break;                /**< Wire break window timer. */
    analog_input_t* input;            /**< Board channel object (borrowed). */
    channel_ao_applied_fn on_applied; /**< Apply-result reporter; may be NULL. */
    void* applied_ctx;                /**< Context handed to on_applied. */
    ch_config_t cfg;                  /**< Effective configuration. */
    ch_config_t pending;              /**< Configuration awaiting APPLY_SIG. */
    float cjc_temp;                   /**< Cached cold junction temperature. */
    float last_value;                 /**< Latest published value (frozen on faults). */
    uint32_t settle_ms;               /**< Delay for the next Settling entry. */
    uint8_t cjc_valid;                /**< Cached cold junction validity. */
    uint8_t wire;                     /**< analog_input_wire_status_t of the last check. */
    uint8_t ch;                       /**< Zero-based channel index (0..COUNT-1); the
                                           same base as the event payloads and the
                                           mirror/KV storage. */
} channel_ao_t;

/**
 * @brief Construct one channel active object.
 *
 * @param me Storage owned by the caller.
 * @param ch Zero-based channel index (0..CH_CONFIG_CHANNEL_COUNT-1).
 * @param input Board channel object to own; borrowed, must outlive this AO.
 * @param on_applied Apply-result reporter; NULL disables reporting.
 * @param applied_ctx Context handed to @p on_applied.
 */
void channel_ao_ctor(channel_ao_t* me,
                     uint8_t ch,
                     analog_input_t* input,
                     channel_ao_applied_fn on_applied,
                     void* applied_ctx);

#ifdef __cplusplus
}
#endif

#endif
