#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/**
 * @file board_config.h
 * @brief Board topology and bus facts; the single home of compile-time knobs.
 *
 * Self-contained: no other includes, no code, safe from any context.
 * board.h re-exports this file; the board device headers and the application
 * channel model derive their own semantic names from BOARD_CHANNEL_COUNT.
 */

/** @brief Channel positions the backplane physically provides. */
#define BOARD_CHANNEL_POSITIONS_MAX 8U

/**
 * @brief Populated channel positions (1..BOARD_CHANNEL_POSITIONS_MAX).
 *
 * The SKU knob is the preprocessor define SIG24131_BOARD_COUNT=N; without
 * it every position is populated.
 */
#ifndef BOARD_CHANNEL_COUNT
#ifdef SIG24131_BOARD_COUNT
#define BOARD_CHANNEL_COUNT SIG24131_BOARD_COUNT
#else
#define BOARD_CHANNEL_COUNT BOARD_CHANNEL_POSITIONS_MAX
#endif
#endif

#if ( BOARD_CHANNEL_COUNT < 1U ) \
    || ( BOARD_CHANNEL_COUNT > BOARD_CHANNEL_POSITIONS_MAX )
#error "BOARD_CHANNEL_COUNT must be between 1 and 8"
#endif

/**
 * @brief Modbus RTU bus baud rate, as the core's timing math assumes it.
 *
 * modbus_slave_board.c feeds it to the slave core (T3.5 and the TX
 * watchdog derive from it); the wizard-generated USART1 init in
 * wk_usart.c must stay in step with this value.
 */
#define BOARD_MODBUS_BAUDRATE 9600U

#endif
