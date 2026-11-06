#ifndef I2C_SOFT_BOARD_H
#define I2C_SOFT_BOARD_H

#include <stdint.h>

#include "i2c_soft.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the board's bit-banged I2C bus and check the two lines.
 *
 * @return I2C_SOFT_OK, or the failing constructor or bus-check status.
 * @note Requires wk_gpio_config(), wk_timebase_init() and the board critical
 *       section to be ready. Task context only, never an ISR: the single
 *       instance is not thread-safe, so callers serialize every transaction.
 *       i2c_soft_init() must succeed before the first transaction.
 */
i2c_soft_status_t i2c_soft_board_init(void);

/** @brief Return the board's soft I2C bus instance. */
i2c_soft_t* i2c_soft_board_get(void);

#ifdef __cplusplus
}
#endif

#endif
