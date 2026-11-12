#ifndef MODBUS_SLAVE_BOARD_STUB_H
#define MODBUS_SLAVE_BOARD_STUB_H

#include <stdbool.h>

#include "modbus_slave.h"

/** @brief Callbacks registered by modbus_app_ctor(). */
const modbus_slave_callbacks_t* stub_modbus_callbacks(void);

/** @brief True when the stub slave is running. */
bool stub_modbus_running(void);

/** @brief Identity copied by modbus_slave_board_ctor(). */
const modbus_slave_identity_t* stub_modbus_identity(void);

#endif
