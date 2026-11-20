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

/**
 * @brief The one board position list (8-slot backplane layout).
 *
 * Every layer derives its per-position rows from this expansion: the
 * register map span table and the KV default table (which keeps the
 * 8-position key set even on SKUs that populate fewer channels). There
 * must be no second hand-maintained position list.
 */
#define BOARD_POSITIONS(X) X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7)

/** @brief Number of board positions listed by BOARD_POSITIONS. */
#define BOARD_POSITION_COUNT 8U

/**
 * @brief Populated channel positions (1..BOARD_POSITION_COUNT).
 *
 * The SKU knob is the preprocessor define SIG24131_BOARD_COUNT=N; without
 * it every position is populated.
 */
#ifndef BOARD_CHANNEL_COUNT
#ifdef SIG24131_BOARD_COUNT
#define BOARD_CHANNEL_COUNT SIG24131_BOARD_COUNT
#else
#define BOARD_CHANNEL_COUNT BOARD_POSITION_COUNT
#endif
#endif

#if ( BOARD_CHANNEL_COUNT < 1U ) \
    || ( BOARD_CHANNEL_COUNT > BOARD_POSITION_COUNT )
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
