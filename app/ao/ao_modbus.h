#ifndef AO_MODBUS_H
#define AO_MODBUS_H

/**
 * @file ao_modbus.h
 * @brief Modbus RTU slave active object (protocol + register mirror).
 *
 * Terminates the Modbus protocol, owns the register mirrors, the device
 * fault word latches and the param component (synchronous commit inside the
 * write callback). See docs/ao/ao_modbus.md.
 */

#include "qpc.h"

#include "ch_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Queue depth (telemetry round plus frame plus margin). */
#define AO_MODBUS_QUEUE_LEN 16U

/** @brief AoModbus active object. */
typedef struct {
    QActive super;    /**< Active object base. */
    QEvt frameEvt;    /**< Static SIG_FRAME event. */
    QEvt txDoneEvt;   /**< Static SIG_TX_DONE event. */
    QEvt txTimeoutEvt; /**< Static SIG_TX_TIMEOUT event. */
    ch_config_t cfg_image[CH_CONFIG_CHANNEL_COUNT]; /**< Effective configs. */
    float       value[CH_CONFIG_CHANNEL_COUNT];     /**< Latest values. */
    uint16_t    status[CH_CONFIG_CHANNEL_COUNT];    /**< Channel status codes. */
    float    cjc_temp;    /**< Latest cold junction temperature. */
    uint16_t addr_image;  /**< Effective slave address. */
    uint8_t  cjc_valid;   /**< Cold junction validity (b0 source). */
    uint8_t  b1;          /**< Latch: parameter write failure. */
    uint8_t  load_failed[CH_CONFIG_CHANNEL_COUNT]; /**< Per-channel load failure. */
    uint8_t  addr_load_failed; /**< dev/addr load failure (b2 source). */
    uint8_t  fdb_init_failed;  /**< Latch: FlashDB init failure (b3). */
    uint16_t tx_done_cnt;    /**< Diagnostic: replies sent. */
    uint16_t tx_timeout_cnt; /**< Diagnostic: TX watchdog events. */
} ao_modbus_t;

/**
 * @brief Construct the Modbus active object and its slave instance.
 *
 * Builds the data-space callbacks and identity, constructs the board slave
 * and attaches the ISR event port.
 *
 * @param me Storage owned by the caller.
 */
void ao_modbus_ctor(ao_modbus_t* me);

#ifdef __cplusplus
}
#endif

#endif
