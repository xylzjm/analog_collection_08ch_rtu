#ifndef TMP75_BOARD_H
#define TMP75_BOARD_H

#include <stdint.h>

#include "tmp75.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 7-bit slave address of the single TMP75; A2..A0 are tied low. */
#define TMP75_BOARD_ADDRESS TMP75_ADDRESS_0 /* 0x48 */

/**
 * @brief Build the board's TMP75 driver object.
 *
 * @return TMP75_OK, or the failing constructor status.
 * @note Construction runs no transaction and does not require the bus;
 *       i2c_soft_board_init() must have succeeded before the first
 *       transaction. Transactions on the shared bus stay the caller's to
 *       serialize.
 */
tmp75_status_t tmp75_board_ctor(void);

/** @brief Return the board's TMP75 driver object. */
tmp75_t* tmp75_board_get_device(void);

#ifdef __cplusplus
}
#endif

#endif
