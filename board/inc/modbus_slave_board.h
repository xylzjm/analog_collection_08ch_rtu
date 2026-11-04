#ifndef MODBUS_SLAVE_BOARD_H
#define MODBUS_SLAVE_BOARD_H

#include <stdint.h>

#include "modbus_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Construct the slave instance.
 *
 * @param baudrate Bus baudrate.
 * @param callbacks Data-space callbacks and shared context.
 * @param identity Report-Slave-ID data.
 * @return MODBUS_SLAVE_OK, or the failing constructor status.
 * @note Does not touch hardware or start the core; the application attaches
 *       the event port and calls modbus_slave_start().
 */
modbus_slave_status_t modbus_slave_board_ctor(
    uint32_t baudrate,
    const modbus_slave_callbacks_t* callbacks,
    const modbus_slave_identity_t* identity);

/** @brief Return the number of RS485 channels in this build. */
uint8_t modbus_slave_board_count(void);

/** @brief Return the board's Modbus slave instance. */
modbus_slave_t* modbus_slave_board_get(void);

#ifdef __cplusplus
}
#endif

#endif
