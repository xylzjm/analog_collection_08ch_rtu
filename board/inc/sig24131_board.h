#ifndef SIG24131_BOARD_H
#define SIG24131_BOARD_H

#include <stdint.h>

#include "at32f421_wk_config.h"
#include "sig24131.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of SIG24131 devices on the board. */
#define SIG24131_BOARD_MAX_INSTANCES 8U

/** @brief Number of populated SIG24131 devices. */
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
 * @param channel One-based channel number.
 * @return SIG24131_OK, or SIG24131_ERR_ARG for an invalid channel.
 * @note SPI1 and the chip-select GPIOs must already be initialized.
 */
sig24131_status_t sig24131_board_ctor(uint8_t channel);

/** @brief Construct the driver instance of every populated channel. */
sig24131_status_t sig24131_board_ctor_all(void);

/**
 * @brief Reset one device without waiting for it to settle.
 *
 * @param channel One-based channel number.
 * @return SIG24131_OK, or SIG24131_ERR_ARG for an invalid channel.
 * @note Wait SIG24131_RESET_DELAY_MS before the next register access.
 */
sig24131_status_t sig24131_board_reset(uint8_t channel);

/** @brief Reset every device and wait once for all of them to settle. */
sig24131_status_t sig24131_board_reset_all(void);

/** @brief Return the number of populated SIG24131 instances. */
uint8_t sig24131_board_count(void);

/** @brief Return the driver object of one channel. */
sig24131_t* sig24131_board_get_device(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif
