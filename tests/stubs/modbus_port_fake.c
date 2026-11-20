/**
 * @file modbus_port_fake.c
 * @brief 假 RS485 线路、单次定时器与可嵌套临界区。
 *
 * 被测从站核心是真实的 modbus_slave 库；只有板级被伪造。线路捕获收集核心推
 * 出的应答字节，定时器只做功能模拟（T3.5 和发送看门狗都落在这里），临界区统计
 * 进入/退出配对，因此每个场景之后都能断言收支平衡。
 */

#include <stddef.h>

#include "board_config.h"

#include "modbus_port_fake.h"

static modbus_slave_status_t line_direction(void* ctx,
                                            uint8_t rx,
                                            uint8_t tx)
{
    modbus_port_fake_t* f = (modbus_port_fake_t*)ctx;

    /* 核心必须把它未选中的方向保持关闭。 */
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
    (void)ctx;
    (void)timeout_us;
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t timer_cancel(void* ctx)
{
    (void)ctx;
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
    f->rx_enabled  = 0U;
    f->tx_enabled  = 0U;
    f->tx_len      = 0U;
    f->enter_calls = 0U;
    f->exit_calls  = 0U;
    f->nest        = 0;
}

void modbus_port_fake_line(modbus_port_fake_t* f,
                           modbus_slave_port_line_t* out)
{
    out->ctx       = f;
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
    out->ctx   = f;
    out->enter = &critical_enter;
    out->exit  = &critical_exit;
}

void modbus_port_fake_config(modbus_port_fake_t* f,
                             modbus_slave_config_t* cfg,
                             uint8_t* rx, uint16_t rx_cap,
                             uint8_t* tx, uint16_t tx_cap)
{
    cfg->baudrate    = BOARD_MODBUS_BAUDRATE;
    cfg->rx_buffer   = rx;
    cfg->rx_capacity = rx_cap;
    cfg->tx_buffer   = tx;
    cfg->tx_capacity = tx_cap;
    modbus_port_fake_line(f, &cfg->line);
    modbus_port_fake_timer(f, &cfg->timer);
    modbus_port_fake_critical(f, &cfg->critical);
}
