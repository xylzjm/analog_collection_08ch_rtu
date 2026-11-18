#ifndef MODBUS_AO_H
#define MODBUS_AO_H

#include "qpc.h"

#include "modbus_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Queue depth (telemetry round plus frame plus margin). */
#define MODBUS_AO_QUEUE_LEN 16U

/** @brief AoModbus active object: protocol routing and publishes. */
typedef struct {
    QActive super;                /**< Active object base. */
    modbus_image_t image;         /**< Typed mirror of the Modbus data space. */
    modbus_slave_t* slave;        /**< Slave instance, attached after construction. */
    modbus_slave_port_event_t ev; /**< ISR event port routed into this AO. */
    uint16_t tx_done_cnt;         /**< Diagnostic: replies sent. */
    uint16_t tx_timeout_cnt;      /**< Diagnostic: TX watchdog events. */
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
 * Delegates to modbus_image_callbacks(); the callbacks operate this AO's
 * mirror, their ctx is the image. The composition root passes the result
 * to modbus_slave_board_ctor() (target) or modbus_slave_ctor() (host
 * tests).
 *
 * @param me Active object whose mirror the callbacks serve.
 * @return Callback table bound to the mirror.
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
