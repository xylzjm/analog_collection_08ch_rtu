#ifndef MODBUS_SLAVE_BOARD_H
#define MODBUS_SLAVE_BOARD_H

#include "modbus_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Platform half of the slave construction config (ports, buffers,
 *        baudrate). The callbacks and identity fields are left zeroed and are
 *        filled by the Modbus AO.
 * @return Pointer to a file-static const config.
 */
const modbus_slave_config_t* modbus_slave_board_config(void);

/** @brief Return the board's Modbus slave instance. */
modbus_slave_t* modbus_slave_board_get(void);

#ifdef __cplusplus
}
#endif

#endif
