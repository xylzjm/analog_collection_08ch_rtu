#ifndef SIG24131_BOARD_H
#define SIG24131_BOARD_H

/**
 * @file sig24131_board.h
 * @brief AT32F421 board bindings for SIG24131 ADC instances.
 *
 * The board layer owns one independent driver instance per populated device.
 * All devices share the SPI1 bus and use a separate GPIO pin as chip select.
 */

#include <stdint.h>

#include "at32f421_wk_config.h"
#include "sig24131.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The board can carry at most eight devices.  A four-device hardware variant
 * only needs to define SIG24131_BOARD_INSTANCE_COUNT=4 in its build settings. */
#define SIG24131_BOARD_MAX_INSTANCES 8U

#ifndef SIG24131_BOARD_INSTANCE_COUNT
#ifdef SIG24131_BOARD_COUNT
#define SIG24131_BOARD_INSTANCE_COUNT SIG24131_BOARD_COUNT
#else
#define SIG24131_BOARD_INSTANCE_COUNT SIG24131_BOARD_MAX_INSTANCES
#endif
#endif

#if ( SIG24131_BOARD_INSTANCE_COUNT < 1U ) \
    || (SIG24131_BOARD_INSTANCE_COUNT > SIG24131_BOARD_MAX_INSTANCES)
#error "SIG24131_BOARD_INSTANCE_COUNT must be between 1 and 8"
#endif

/**
 * @brief Construct the driver instance of one channel.
 *
 * Binds the instance to the chip select of that channel and leaves the
 * active-low chip select deasserted.  The SPI1 peripheral and the GPIO outputs
 * must be initialized by the workbench first.  The device is not touched, so
 * sig24131_board_reset_all() has to follow before the first register access.
 *
 * @param channel One-based channel number, 1..sig24131_board_count().
 * @return SIG24131_OK, or SIG24131_ERR_ARG when the channel is outside the
 *         populated range.
 */
sig24131_status_t sig24131_board_ctor(uint8_t channel);

/** @brief Construct the driver instance of every populated channel. */
sig24131_status_t sig24131_board_ctor_all(void);

/**
 * @brief Reset one device.
 *
 * The device needs SIG24131_RESET_DELAY_MS before it may be accessed again, and
 * this function does not wait.  A caller that resets several channels inserts
 * the wait once after the last one; sig24131_board_reset_all() does exactly
 * that.
 *
 * @param channel One-based channel number, 1..sig24131_board_count().
 * @return SIG24131_OK, or SIG24131_ERR_ARG when the channel is outside the
 *         populated range.
 */
sig24131_status_t sig24131_board_reset(uint8_t channel);

/**
 * @brief Reset every populated SIG24131 device and wait for them to settle.
 *
 * The reset commands go out back to back and one shared SIG24131_RESET_DELAY_MS
 * wait follows, so the cost is one settling time instead of one per device.
 * Returns the first error but still resets as many devices as it can.
 */
sig24131_status_t sig24131_board_reset_all(void);

/** @brief Return the number of populated instances for this build. */
uint8_t sig24131_board_count(void);

/** @brief Get the driver object for one channel. Channel numbers are one-based. */
sig24131_t* sig24131_board_get_device(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif
