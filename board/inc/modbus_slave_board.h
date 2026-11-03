#ifndef MODBUS_SLAVE_BOARD_H
#define MODBUS_SLAVE_BOARD_H

/**
 * @file modbus_slave_board.h
 * @brief AT32F421 board bindings for the modbus_slave RTU slave core.
 *
 * This product carries a single RS485 channel.  The board layer owns the one
 * core instance and implements the line and timer capabilities of
 * modbus_slave_config_t against the WorkBench-configured peripherals (the
 * critical capability runs on the board's nestable critical section):
 *
 * | Resource | Assignment                                      |
 * |----------|-------------------------------------------------|
 * | USART    | USART1: TX = PA9, RX = PA10 (mux 1)             |
 * | RS485 DE | PA11, push-pull output, high = drive bus        |
 * | T3.5 / TX watchdog | TMR14, 10 us/tick (PSC = 1199)      |
 * | NVIC     | USART1 = 5, TMR14 = 5, SysTick = 15             |
 * | Buffers  | RX 256 B + TX 256 B                             |
 *
 * The product runs at 9600..115200 baud.  At the fixed 10 us tick the 16-bit
 * period spans 655.35 ms, which covers T3.5 (1.75..4.011 ms) and the worst
 * case TX watchdog (9600 baud, 256-byte reply: ~303 ms).
 *
 * USART1 and TMR14 intentionally share preemption priority 5: neither can
 * preempt the other, and both handlers stay short because the core only posts
 * events (SysTick at 15 is preempted by both).
 *
 * The WorkBench init must have run first: wk_gpio_config() leaves DE low
 * (bus released), wk_usart1_init() opens USART1 with the ERR interrupt only,
 * leaves the receiver disabled (RE = 0) and the TX/RX pins pulled up, and
 * wk_tmr14_init() only configures TMR14 without starting it.  RDBF/TDBE/TDC
 * and the receiver enable are driven by this module.  The USART1 and TMR14
 * interrupt handlers are implemented in modbus_slave_board.c; do not add them
 * to at32f421_int.c or board.c as well.
 *
 * The constructor only builds the core; it does not touch the hardware or
 * start it.  The application attaches its event port
 * (modbus_slave_set_event_port()) and then starts the core with
 * modbus_slave_start().  Two rules belong to the application:
 *
 * - the on_frame_received event is the one that must reach
 *   modbus_slave_advance(), from the application's own context;
 * - the slave address is answered by callbacks.address, which is part of the
 *   callback struct (the core stores no address).
 *
 * The critical capability is the board's nestable interrupt mask
 * (board.h's board_critical_enter()/board_critical_exit() macros).  The count
 * behind them is shared with the QP critical section, so the core's calls
 * nest inside QP's own critical sections and the other way round.
 */

#include <stdint.h>

#include "modbus_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Construct the slave instance.
 *
 * Hands the core its 256 B RX and TX buffers from static storage.  The
 * callbacks and identity structs are copied, so the caller only has to keep
 * identity.additional alive as long as the instance.  No hardware is touched
 * and the core stays stopped; the application attaches its event port and
 * calls modbus_slave_start() later.
 *
 * @param baudrate Bus baudrate, 9600..115200 for this product (T3.5 and the
 *        TX watchdog derive from it).
 * @param callbacks The four data-space callbacks, the address callback and
 *        their shared ctx.
 * @param identity Report-Slave-ID data (0x11).
 * @return MODBUS_SLAVE_OK, or the failing ctor status (MODBUS_SLAVE_ERR_ARG
 *         for bad callbacks/identity or a zero baudrate).
 */
modbus_slave_status_t modbus_slave_board_ctor(
    uint32_t baudrate,
    const modbus_slave_callbacks_t* callbacks,
    const modbus_slave_identity_t* identity);

/** @brief Return the number of RS485 channels for this build (always 1). */
uint8_t modbus_slave_board_count(void);

/** @brief Get the core object, for the application. */
modbus_slave_t* modbus_slave_board_get(void);

#ifdef __cplusplus
}
#endif

#endif
