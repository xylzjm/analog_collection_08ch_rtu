#ifndef MODBUS_AO_H
#define MODBUS_AO_H

/**
 * @file modbus_ao.h
 * @brief Modbus RTU slave active object (protocol + register mirror).
 *
 * Terminates the Modbus protocol, owns the register mirrors, the device
 * fault word latches and the param component (synchronous commit inside the
 * write callback). See docs/ao/modbus_ao.md.
 */

#include "qpc.h"

#include "ch_config.h"

#include "modbus_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Queue depth (telemetry round plus frame plus margin). */
#define MODBUS_AO_QUEUE_LEN 16U

/** @brief AoModbus active object. */
typedef struct {
    QActive super;                                  /**< Active object base. */
    modbus_slave_t* slave;                          /**< Slave instance, attached after construction. */
    modbus_slave_callbacks_t cb;                    /**< Data-space callbacks bound to this AO's mirror. */
    modbus_slave_port_event_t ev;                   /**< ISR event port routed into this AO. */
    ch_config_t cfg_image[CH_CONFIG_CHANNEL_COUNT]; /**< Effective configs. */
    float value[CH_CONFIG_CHANNEL_COUNT];           /**< Latest values. */
    uint16_t status[CH_CONFIG_CHANNEL_COUNT];       /**< Channel status codes. */
    float cjc_temp;                                 /**< Latest cold junction temperature. */
    uint16_t addr_image;                            /**< Effective slave address. */
    uint8_t cjc_valid;                              /**< Cold junction validity (b0 source). */
    uint8_t b1;                                     /**< Latch: parameter write failure. */
    uint8_t load_failed[CH_CONFIG_CHANNEL_COUNT];   /**< Per-channel load failure. */
    uint8_t addr_load_failed;                       /**< dev/addr load failure (b2 source). */
    uint8_t fdb_init_failed;                        /**< Latch: FlashDB init failure (b3). */
    uint16_t tx_done_cnt;                           /**< Diagnostic: replies sent. */
    uint16_t tx_timeout_cnt;                        /**< Diagnostic: TX watchdog events. */
} modbus_ao_t;

/**
 * @brief Construct the Modbus active object (no slave attached yet).
 *
 * @param me Storage owned by the caller.
 */
void modbus_ao_ctor(modbus_ao_t* me);

/**
 * @brief Data-space callbacks of this AO, for slave construction.
 *
 * The callbacks operate this AO's register mirror; their ctx is @p me. The
 * composition root passes the result to modbus_slave_board_ctor() (target) or
 * modbus_slave_ctor() (host tests).
 *
 * @param me Active object whose mirror the callbacks serve.
 * @return Callback table bound to @p me.
 */
const modbus_slave_callbacks_t* modbus_ao_callbacks(const modbus_ao_t* me);

/**
 * @brief Slave identity reported by function code 0x11.
 *
 * @return Identity table; no per-instance state.
 */
const modbus_slave_identity_t* modbus_ao_identity(void);

/**
 * @brief Attach a constructed slave and route its ISR events into this AO.
 *
 * @param me Storage owned by the caller.
 * @param slave Slave instance built from modbus_ao_callbacks(); must not be
 *        started yet, because the event port attach requires the stopped
 *        state.
 */
void modbus_ao_attach(modbus_ao_t* me,
                      modbus_slave_t* slave);

#ifdef __cplusplus
}
#endif

#endif
