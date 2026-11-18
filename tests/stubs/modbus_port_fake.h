#ifndef MODBUS_PORT_FAKE_H_
#define MODBUS_PORT_FAKE_H_

#include <stdint.h>

#include "modbus_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Largest reply the fake line records (identity FC can be longer). */
#define MODBUS_PORT_FAKE_TX_MAX 280U

/** @brief Fake RS485 line, one-shot timer and nesting critical section. */
typedef struct {
    /* line */
    uint8_t rx_enabled;  /**< Last direction() rx argument. */
    uint8_t tx_enabled;  /**< Last direction() tx argument. */
    uint16_t tx_len;     /**< Bytes pushed through put_byte(). */
    uint8_t tx_bytes[MODBUS_PORT_FAKE_TX_MAX]; /**< Reply capture. */
    /* timer */
    uint32_t last_timeout_us; /**< Last arm() argument. */
    uint16_t arm_calls;    /**< arm() invocations. */
    uint16_t cancel_calls; /**< cancel() invocations. */
    uint8_t armed;         /**< 1 after arm(), 0 after cancel(). */
    /* critical section */
    uint16_t enter_calls;  /**< enter() invocations. */
    uint16_t exit_calls;   /**< exit() invocations. */
    int nest;              /**< Nesting counter; 0 outside any section. */
} modbus_port_fake_t;

/** @brief Clear the script and the capture. */
void modbus_port_fake_init(modbus_port_fake_t* f);

/** @brief Wire the fake line callbacks into @p out. */
void modbus_port_fake_line(modbus_port_fake_t* f,
                           modbus_slave_port_line_t* out);

/** @brief Wire the fake timer callbacks into @p out. */
void modbus_port_fake_timer(modbus_port_fake_t* f,
                            modbus_slave_port_timer_t* out);

/** @brief Wire the fake nesting critical section into @p out. */
void modbus_port_fake_critical(modbus_port_fake_t* f,
                               modbus_slave_port_critical_t* out);

#ifdef __cplusplus
}
#endif

#endif
