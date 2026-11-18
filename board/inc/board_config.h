#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/**
 * @file board_config.h
 * @brief Board topology facts; the single home of the SKU channel-count knob.
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

#endif
