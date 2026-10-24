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

#if (SIG24131_BOARD_INSTANCE_COUNT < 1U) || \
    (SIG24131_BOARD_INSTANCE_COUNT > SIG24131_BOARD_MAX_INSTANCES)
#error "SIG24131_BOARD_INSTANCE_COUNT must be between 1 and 8"
#endif

/**
 * @brief Initialize all board instances on SPI1.
 *
 * The SPI1 peripheral and GPIO outputs must be initialized by the workbench
 * before calling this function.  The function binds each driver instance and
 * leaves all active-low chip selects deasserted.
 */
sig24131_status_t sig24131_board_init(void);

/** @brief Reset every populated SIG24131 device. */
sig24131_status_t sig24131_board_reset(void);

/** @brief Return the number of populated instances for this build. */
uint8_t sig24131_board_count(void);

/** @brief Get the driver object for one channel. Channel numbers are one-based. */
sig24131_t* sig24131_board_get_device(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif
