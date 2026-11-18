/**
 * @file modbus_port_fake.c
 * @brief Fake RS485 line, one-shot timer and nesting critical section.
 *
 * The slave core under test is the real modbus_slave library; only the
 * board is faked. The line capture collects the reply bytes the core
 * pushes out, the timer records every arm() (T3.5 and the TX watchdog
 * both land here) and the critical section counts enter/exit pairs so
 * balance can be asserted after every scenario.
 */

#include <stddef.h>

#include "modbus_port_fake.h"

static modbus_slave_status_t line_direction(void* ctx,
                                            uint8_t rx,
                                            uint8_t tx)
{
    modbus_port_fake_t* f = (modbus_port_fake_t*)ctx;

    /* The core must leave the direction it does not select switched off. */
    if ( (rx != 0U) && (tx != 0U) ) {
        return MODBUS_SLAVE_ERR_ARG;
    }
    f->rx_enabled = rx;
    f->tx_enabled = tx;
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t line_put_byte(void* ctx,
                                           uint8_t byte)
{
    modbus_port_fake_t* f = (modbus_port_fake_t*)ctx;

    if ( f->tx_len >= MODBUS_PORT_FAKE_TX_MAX ) {
        return MODBUS_SLAVE_ERR_IO;
    }
    f->tx_bytes[f->tx_len] = byte;
    ++f->tx_len;
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t line_tx_done(void* ctx)
{
    (void)ctx;
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t timer_arm(void* ctx,
                                       uint32_t timeout_us)
{
    modbus_port_fake_t* f = (modbus_port_fake_t*)ctx;

    ++f->arm_calls;
    f->last_timeout_us = timeout_us;
    f->armed = 1U;
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t timer_cancel(void* ctx)
{
    modbus_port_fake_t* f = (modbus_port_fake_t*)ctx;

    ++f->cancel_calls;
    f->armed = 0U;
    return MODBUS_SLAVE_OK;
}

static void critical_enter(void* ctx)
{
    modbus_port_fake_t* f = (modbus_port_fake_t*)ctx;

    ++f->enter_calls;
    ++f->nest;
}

static void critical_exit(void* ctx)
{
    modbus_port_fake_t* f = (modbus_port_fake_t*)ctx;

    ++f->exit_calls;
    --f->nest;
}

void modbus_port_fake_init(modbus_port_fake_t* f)
{
    f->rx_enabled      = 0U;
    f->tx_enabled      = 0U;
    f->tx_len          = 0U;
    f->last_timeout_us = 0U;
    f->arm_calls       = 0U;
    f->cancel_calls    = 0U;
    f->armed           = 0U;
    f->enter_calls     = 0U;
    f->exit_calls      = 0U;
    f->nest            = 0;
}

void modbus_port_fake_line(modbus_port_fake_t* f,
                           modbus_slave_port_line_t* out)
{
    out->ctx      = f;
    out->direction = &line_direction;
    out->put_byte  = &line_put_byte;
    out->tx_done   = &line_tx_done;
}

void modbus_port_fake_timer(modbus_port_fake_t* f,
                            modbus_slave_port_timer_t* out)
{
    out->ctx    = f;
    out->arm    = &timer_arm;
    out->cancel = &timer_cancel;
}

void modbus_port_fake_critical(modbus_port_fake_t* f,
                               modbus_slave_port_critical_t* out)
{
    out->ctx  = f;
    out->enter = &critical_enter;
    out->exit  = &critical_exit;
}
