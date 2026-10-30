#ifndef MODBUS_SLAVE_BOARD_H
#define MODBUS_SLAVE_BOARD_H

/**
 * @file modbus_slave_board.h
 * @brief AT32F421 board bindings for the modbus_slave RTU slave stack.
 *
 * This product carries a single RS485 channel.  The board layer owns the one
 * core instance and implements modbus_slave_port_t against the WorkBench-
 * configured peripherals:
 *
 * | Resource | Assignment                                  |
 * |----------|---------------------------------------------|
 * | USART    | USART1: TX = PA9, RX = PA10 (mux 1)         |
 * | RS485 DE | PA11, push-pull output, high = drive bus    |
 * | T3.5 / TX watchdog | TMR14, 10 us/tick (PSC = 1199)   |
 * | NVIC     | USART1 = 5, TMR14 = 5, SysTick = 15         |
 * | Buffers  | RX 256 B + TX 256 B                         |
 *
 * USART1 and TMR14 intentionally share preemption priority 5: neither can
 * preempt the other, and both handlers stay short because the backend only
 * posts events (SysTick at 15 is preempted by both).
 *
 * The WorkBench init must have run first: wk_gpio_config() leaves DE low
 * (bus released), wk_usart1_init() opens USART1 with the ERR interrupt only,
 * leaves the receiver disabled (RE = 0) and the TX/RX pins pulled up, and
 * wk_tmr14_init() only configures TMR14 without starting it.  RDBF/TDBE/TDC
 * and the receiver enable are driven by this module.  The USART1 and TMR14
 * interrupt handlers are implemented in modbus_slave_board.c; do not add them
 * to at32f421_int.c or board.c as well.
 *
 * Pure C: this module depends on modbus_slave.h and the AT32 peripheral
 * library only — no QP or RTOS types.
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
 * identity.additional alive as long as the instance.  No hardware is touched;
 * the backend starts the core later with modbus_slave_start().
 *
 * @param address Slave address, 1..247.
 * @param baudrate Bus baudrate, > 0 (T3.5 and the TX watchdog derive from it).
 * @param callbacks The four data-space callbacks plus their shared ctx.
 * @param identity Report-Slave-ID data (0x11).
 * @return MODBUS_SLAVE_OK, or the failing ctor status (MODBUS_SLAVE_ERR_ARG
 *         for bad callbacks/identity or an invalid address/baudrate,
 *         MODBUS_SLAVE_ERR_STATE when the instance is already running,
 *         MODBUS_SLAVE_ERR_IO when the port could not be validated).
 */
modbus_slave_status_t modbus_slave_board_ctor(
    uint8_t address,
    uint32_t baudrate,
    const modbus_slave_callbacks_t* callbacks,
    const modbus_slave_identity_t* identity);

/** @brief Return the number of RS485 channels for this build (always 1). */
uint8_t modbus_slave_board_count(void);

/** @brief Get the core object, for the backend. */
modbus_slave_t* modbus_slave_board_get(void);

#ifdef __cplusplus
}
#endif

#endif
