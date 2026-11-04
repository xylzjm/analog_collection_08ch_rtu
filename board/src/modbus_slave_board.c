#include "modbus_slave_board.h"

#include "at32f421_wk_config.h"

#include "board.h"

/** @brief TMR14 resolution and 16-bit period limit. */
#define MB_TMR_TICK_US   10U
#define MB_TMR_MAX_TICKS 65535U

/** @brief Modbus RX/TX buffer size. */
#define MB_BUFFER_SIZE   256U

/** @brief RS485 line binding. */
typedef struct {
    usart_type* uart;
    gpio_type* de_port;
    uint16_t de_pin;
} line_ctx_t;

/** @brief T3.5 / TX watchdog timer binding. */
typedef struct {
    tmr_type* timer;
} timer_ctx_t;

static const line_ctx_t line_ctx = {
    USART1, USART_DE_GPIO_PORT, USART_DE_PIN
};

static const timer_ctx_t timer_ctx = {
    TMR14
};

static uint8_t rx_buffer[MB_BUFFER_SIZE];
static uint8_t tx_buffer[MB_BUFFER_SIZE];

static modbus_slave_t slave;

static modbus_slave_status_t line_direction(void* ctx,
                                            uint8_t rx,
                                            uint8_t tx)
{
    const line_ctx_t* b = (const line_ctx_t*)ctx;

    if ( rx != 0U && tx != 0U ) {
        return MODBUS_SLAVE_ERR_ARG;
    }

    if ( rx != 0U ) {
        /* Mute TX first, then release DE: TC is the last bit out. */
        usart_interrupt_enable(b->uart, USART_TDBE_INT, FALSE);
        usart_interrupt_enable(b->uart, USART_TDC_INT, FALSE);
        usart_flag_clear(b->uart, USART_TDC_FLAG);
        usart_receiver_enable(b->uart, TRUE);
        usart_interrupt_enable(b->uart, USART_RDBF_INT, TRUE);
        gpio_bits_reset(b->de_port, b->de_pin);
    } else if ( tx != 0U ) {
        /* Assert DE before enabling TXE so the first byte is not driven with DE low. */
        usart_interrupt_enable(b->uart, USART_RDBF_INT, FALSE);
        usart_receiver_enable(b->uart, FALSE);
        gpio_bits_set(b->de_port, b->de_pin);
        usart_flag_clear(b->uart, USART_TDC_FLAG);
        usart_interrupt_enable(b->uart, USART_TDC_INT, TRUE);
        usart_interrupt_enable(b->uart, USART_TDBE_INT, TRUE);
    } else {

        usart_interrupt_enable(b->uart, USART_RDBF_INT, FALSE);
        usart_receiver_enable(b->uart, FALSE);
        usart_interrupt_enable(b->uart, USART_TDBE_INT, FALSE);
        usart_interrupt_enable(b->uart, USART_TDC_INT, FALSE);
        usart_flag_clear(b->uart, USART_TDC_FLAG);
        gpio_bits_reset(b->de_port, b->de_pin);
    }

    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t line_put_byte(void* ctx, uint8_t byte)
{
    const line_ctx_t* b = (const line_ctx_t*)ctx;

    usart_data_transmit(b->uart, (uint16_t)byte);
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t line_tx_done(void* ctx)
{
    const line_ctx_t* b = (const line_ctx_t*)ctx;

    usart_interrupt_enable(b->uart, USART_TDBE_INT, FALSE);
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t timer_arm(void* ctx, uint32_t timeout_us)
{
    const timer_ctx_t* t = (const timer_ctx_t*)ctx;
    uint32_t ticks;

    /* Clamp to the 16-bit period; the core's timeouts are expected to fit. */
    ticks = (timeout_us + MB_TMR_TICK_US - 1U) / MB_TMR_TICK_US;
    if ( ticks == 0U ) {
        ticks = 1U;
    }
    if ( ticks > MB_TMR_MAX_TICKS ) {
        ticks = MB_TMR_MAX_TICKS;
    }

    tmr_interrupt_enable(t->timer, TMR_OVF_INT, FALSE);
    tmr_counter_enable(t->timer, FALSE);
    tmr_period_value_set(t->timer, ticks - 1U);
    tmr_counter_value_set(t->timer, 0U);
    tmr_flag_clear(t->timer, TMR_OVF_FLAG);
    tmr_interrupt_enable(t->timer, TMR_OVF_INT, TRUE);
    tmr_counter_enable(t->timer, TRUE);
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t timer_cancel(void* ctx)
{
    const timer_ctx_t* t = (const timer_ctx_t*)ctx;

    tmr_counter_enable(t->timer, FALSE);
    tmr_interrupt_enable(t->timer, TMR_OVF_INT, FALSE);
    tmr_flag_clear(t->timer, TMR_OVF_FLAG);
    return MODBUS_SLAVE_OK;
}

/* Shares the nesting count with QP; see board.h. */
static void critical_enter(void* ctx)
{
    (void)ctx;
    board_critical_enter();
}

static void critical_exit(void* ctx)
{
    (void)ctx;
    board_critical_exit();
}

modbus_slave_status_t modbus_slave_board_ctor(
    uint32_t baudrate,
    const modbus_slave_callbacks_t* callbacks,
    const modbus_slave_identity_t* identity)
{
    modbus_slave_config_t config;

    if ( callbacks == NULL || identity == NULL ) {
        return MODBUS_SLAVE_ERR_ARG;
    }

    config.baudrate            = baudrate;
    config.rx_buffer           = rx_buffer;
    config.rx_capacity         = (uint16_t)MB_BUFFER_SIZE;
    config.tx_buffer           = tx_buffer;
    config.tx_capacity         = (uint16_t)MB_BUFFER_SIZE;
    config.line.ctx            = (void*)&line_ctx;
    config.line.direction      = line_direction;
    config.line.put_byte       = line_put_byte;
    config.line.tx_done        = line_tx_done;
    config.timer.ctx           = (void*)&timer_ctx;
    config.timer.arm           = timer_arm;
    config.timer.cancel        = timer_cancel;
    config.critical.ctx        = NULL;
    config.critical.enter      = critical_enter;
    config.critical.exit       = critical_exit;
    config.callbacks           = *callbacks;
    config.identity            = *identity;

    return modbus_slave_ctor(&slave, &config);
}

uint8_t modbus_slave_board_count(void)
{
    return 1U;
}

modbus_slave_t* modbus_slave_board_get(void)
{
    return &slave;
}

void USART1_IRQHandler(void)
{
    /* RDBF first: clearing error flags reads sts+dt and would eat the byte. */
    if ( usart_interrupt_flag_get(USART1, USART_RDBF_FLAG) != RESET ) {
        uint16_t data = usart_data_receive(USART1);

        (void)modbus_slave_rx_byte(&slave, (uint8_t)data);
    }

    if ( usart_interrupt_flag_get(USART1, USART_TDBE_FLAG) != RESET ) {

        (void)modbus_slave_tx_ready(&slave);
    }

    if ( usart_interrupt_flag_get(USART1, USART_TDC_FLAG) != RESET ) {
        usart_flag_clear(USART1, USART_TDC_FLAG);
        (void)modbus_slave_tx_complete(&slave);
    }

    {
        uint32_t errors = 0U;

        if ( usart_flag_get(USART1, USART_NERR_FLAG) != RESET ) {
            errors |= USART_NERR_FLAG;
        }
        if ( usart_flag_get(USART1, USART_ROERR_FLAG) != RESET ) {
            errors |= USART_ROERR_FLAG;
        }
        if ( usart_flag_get(USART1, USART_FERR_FLAG) != RESET ) {
            errors |= USART_FERR_FLAG;
        }
        if ( usart_flag_get(USART1, USART_PERR_FLAG) != RESET ) {
            errors |= USART_PERR_FLAG;
        }
        if ( errors != 0U ) {
            usart_flag_clear(USART1, errors);
        }
    }
}

void TMR14_GLOBAL_IRQHandler(void)
{
    if ( tmr_flag_get(TMR14, TMR_OVF_FLAG) != RESET ) {
        tmr_flag_clear(TMR14, TMR_OVF_FLAG);
        (void)modbus_slave_timer_expired(&slave);
    }
}
