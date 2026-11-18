#ifndef MODBUS_IMAGE_H
#define MODBUS_IMAGE_H

#include <stdint.h>

#include "ch_config.h"

#include "modbus_slave.h"

#include "param.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file modbus_image.h
 * @brief Typed mirror of the Modbus data space and the write transaction.
 *
 * Passive component, no QP dependency, runs entirely in the AoModbus
 * execution context: the modbus core calls the data-space callbacks
 * directly with the image as their ctx. The write transaction holds the
 * project's atomicity discipline - validate, commit, then touch the
 * mirror (docs/component/modbus_image.md). A committed channel
 * configuration is reported through a plain callback so the active
 * object can turn it into a publish without this module knowing QP.
 */

/**
 * @brief Called after one channel configuration write commits.
 *
 * @param ctx Notifier context registered with the image.
 * @param ch Zero-based channel whose configuration is now in effect.
 * @param cfg The committed, already-mirrored configuration.
 */
typedef void (*modbus_image_cfg_commit_fn)(void* ctx,
                                           uint8_t ch,
                                           const ch_config_t* cfg);

/** @brief Typed mirror of the Modbus data space. */
typedef struct {
    ch_config_t cfg[CH_CONFIG_CHANNEL_COUNT];     /**< Effective configs. */
    float value[CH_CONFIG_CHANNEL_COUNT];         /**< Latest measured values. */
    uint16_t status[CH_CONFIG_CHANNEL_COUNT];     /**< Channel status codes. */
    float cjc_temp;                               /**< Latest cold junction temperature. */
    uint16_t addr_image;                          /**< Effective slave address. */
    uint8_t cjc_valid;                            /**< Cold junction validity (fault word b0). */
    uint8_t b1;                                   /**< Latch: parameter write failure. */
    uint8_t load_failed[CH_CONFIG_CHANNEL_COUNT]; /**< Per-channel load failure. */
    uint8_t addr_load_failed;                     /**< dev/addr load failure (b2 source). */
    uint8_t fdb_init_failed;                      /**< Latch: FlashDB init failure (b3 source). */
    modbus_image_cfg_commit_fn cfg_notifier;      /**< Commit reporter; may be NULL. */
    void* cfg_notifier_ctx;                       /**< Context handed to cfg_notifier. */
    modbus_slave_callbacks_t callbacks;           /**< Per-instance table bound to this image. */
} modbus_image_t;

/**
 * @brief Reset the mirror to its boot defaults.
 *
 * Channels report "unconfigured" (status 1) until their first
 * MEASURE_SIG; the address falls back to the lowest legal one.
 *
 * @param img Storage owned by the caller.
 */
void modbus_image_init(modbus_image_t* img);

/**
 * @brief Adopt a parameter boot load into the mirror and its latches.
 *
 * @param img Image to fill.
 * @param res Outcome of param_boot_load(); valid entries become the
 *        effective configurations, failures become latched fault bits.
 */
void modbus_image_boot(modbus_image_t* img,
                       const param_boot_result_t* res);

/**
 * @brief Data-space callbacks bound to this image, for slave construction.
 *
 * The four register callbacks and the address callback all use @p img as
 * their ctx.
 *
 * @param img Image the callbacks serve.
 * @return Callback table for modbus_slave_board_ctor() or
 *         modbus_slave_ctor().
 */
const modbus_slave_callbacks_t* modbus_image_callbacks(modbus_image_t* img);

/**
 * @brief Register the channel-configuration commit reporter.
 *
 * @param img Image the write transaction reports on.
 * @param fn Reporter; NULL disables reporting.
 * @param ctx Context handed back to @p fn.
 */
void modbus_image_set_cfg_notifier(modbus_image_t* img,
                                   modbus_image_cfg_commit_fn fn,
                                   void* ctx);

/**
 * @brief Record one measurement into the telemetry mirror.
 *
 * @param img Image to update.
 * @param ch Zero-based channel index, the same base as the event payload.
 * @param value Calibrated engineering value.
 * @param status Channel status code (reg_map_status_t).
 */
void modbus_image_set_measure(modbus_image_t* img,
                              uint8_t ch,
                              float value,
                              uint16_t status);
/**
 * @brief Record one cold junction heartbeat.
 *
 * @param img Image to update.
 * @param temp_c Latest cold junction temperature.
 * @param valid Non-zero when temp_c may be used.
 */
void modbus_image_set_cjc(modbus_image_t* img,
                          float temp_c,
                          uint8_t valid);

#ifdef __cplusplus
}
#endif

#endif
