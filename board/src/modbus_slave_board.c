#include "modbus_slave_board.h"

#include "at32f421_wk_config.h"

/* TMR14 is configured by wk_tmr14_init(): PSC 1199 gives 10 us per tick at
 * APB1 = 120 MHz, one-cycle mode is on and the counter is left stopped. The
 * 16-bit counter spans 655.36 ms — enough for T3.5 (1.75..4.011 ms) and the
 * TX watchdog (9600 baud, 256 B: ~303 ms). This module only reloads the
 * period on each arm. */
#define MB_TMR_TICK_US   10U
#define MB_TMR_MAX_TICKS 65535U

#define MB_BUFFER_SIZE   256U

typedef struct {
    usart_type* uart;
    gpio_type* de_port;
    uint16_t de_pin;
    tmr_type* timer;
    uint8_t rx_buffer[MB_BUFFER_SIZE];
    uint8_t tx_buffer[MB_BUFFER_SIZE];
} board_ctx_t;

/* The single RS485 channel of this product: USART1, DE on PA11, TMR14 as the
 * T3.5 / TX-watchdog timer. */
static board_ctx_t context = {
    USART1, USART_DE_GPIO_PORT, USART_DE_PIN, TMR14, {0}, {0}
};

static modbus_slave_t slave;

/* Hardware port (modbus_slave_port_t) ------------------------------ */

static modbus_slave_status_t board_serial_enable(void* ctx,
                                                 uint8_t rx,
                                                 uint8_t tx)
{
    board_ctx_t* b = (board_ctx_t*)ctx;

    /* The port contract only asks for (1,0), (0,1) or (0,0). Both at once
     * would make the DE and interrupt ordering ambiguous. */
    if ( rx != 0U && tx != 0U ) {
        return MODBUS_SLAVE_ERR_ARG;
    }

    if ( rx != 0U ) {
        /* Receive: mute the transmitter interrupts, disarm TC, open the
         * receiver and release DE. TC was the last bit out, so releasing the
         * driver here is safe. */
        usart_interrupt_enable(b->uart, USART_TDBE_INT, FALSE);
        usart_interrupt_enable(b->uart, USART_TDC_INT, FALSE);
        usart_flag_clear(b->uart, USART_TDC_FLAG);
        usart_receiver_enable(b->uart, TRUE);
        usart_interrupt_enable(b->uart, USART_RDBF_INT, TRUE);
        gpio_bits_reset(b->de_port, b->de_pin);
    } else if ( tx != 0U ) {
        /* Transmit: keep the receiver off, assert DE before arming TXE (the
         * first TXE can fire as soon as its enable bit lands, and the first
         * byte must not leave with the RS485 driver still disabled), then
         * clear TC and enable TXE+TC. */
        usart_interrupt_enable(b->uart, USART_RDBF_INT, FALSE);
        usart_receiver_enable(b->uart, FALSE);
        gpio_bits_set(b->de_port, b->de_pin);
        usart_flag_clear(b->uart, USART_TDC_FLAG);
        usart_interrupt_enable(b->uart, USART_TDC_INT, TRUE);
        usart_interrupt_enable(b->uart, USART_TDBE_INT, TRUE);
    } else {
        /* (0,0): both directions off, bus released. */
        usart_interrupt_enable(b->uart, USART_RDBF_INT, FALSE);
        usart_receiver_enable(b->uart, FALSE);
        usart_interrupt_enable(b->uart, USART_TDBE_INT, FALSE);
        usart_interrupt_enable(b->uart, USART_TDC_INT, FALSE);
        usart_flag_clear(b->uart, USART_TDC_FLAG);
        gpio_bits_reset(b->de_port, b->de_pin);
    }

    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t board_put_byte(void* ctx, uint8_t byte)
{
    board_ctx_t* b = (board_ctx_t*)ctx;

    usart_data_transmit(b->uart, (uint16_t)byte);
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t board_tx_done(void* ctx)
{
    board_ctx_t* b = (board_ctx_t*)ctx;

    /* The core has loaded the last byte: stop requesting TXE and let TC
     * close the frame (DE is released from the TC path only). */
    usart_interrupt_enable(b->uart, USART_TDBE_INT, FALSE);
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t board_timer_enable(void* ctx, uint32_t timeout_us)
{
    board_ctx_t* b = (board_ctx_t*)ctx;
    uint32_t ticks;

    /* Round the microsecond timeout up to whole 10 us ticks and clamp it
     * into the 16-bit range (the core's timeouts fit: max ~303 ms). */
    ticks = (timeout_us + MB_TMR_TICK_US - 1U) / MB_TMR_TICK_US;
    if ( ticks == 0U ) {
        ticks = 1U;
    }
    if ( ticks > MB_TMR_MAX_TICKS ) {
        ticks = MB_TMR_MAX_TICKS;
    }

    /* wk_tmr14_init() already set direction, DIV1, PSC, period buffering,
     * one-cycle mode and the OVF source, and left the counter stopped. Only
     * the period changes per arm: stop the counter, load the new period,
     * restart from zero, clear the stale OVF and arm the interrupt. */
    tmr_counter_enable(b->timer, FALSE);
    tmr_period_value_set(b->timer, ticks - 1U);
    tmr_counter_value_set(b->timer, 0U);
    tmr_flag_clear(b->timer, TMR_OVF_FLAG);
    tmr_interrupt_enable(b->timer, TMR_OVF_INT, TRUE);
    tmr_counter_enable(b->timer, TRUE);
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t board_timer_disable(void* ctx)
{
    board_ctx_t* b = (board_ctx_t*)ctx;

    tmr_counter_enable(b->timer, FALSE);
    tmr_interrupt_enable(b->timer, TMR_OVF_INT, FALSE);
    tmr_flag_clear(b->timer, TMR_OVF_FLAG);
    return MODBUS_SLAVE_OK;
}

/* QV runs on PRIMASK, and QF_USE_BASEPRI is off, so the nestable token is
 * the PRIMASK value itself (the library contract forbids the non-saving
 * QF_CRIT_ENTRY/EXIT pair). */
static uint32_t board_critical_enter(void* ctx)
{
    uint32_t primask = __get_PRIMASK();

    (void)ctx;
    __disable_irq();
    return primask;
}

static void board_critical_exit(void* ctx, uint32_t state)
{
    (void)ctx;
    __set_PRIMASK(state);
}

/* Construction / access ------------------------------------------------- */

modbus_slave_status_t modbus_slave_board_ctor(
    uint8_t address,
    uint32_t baudrate,
    const modbus_slave_callbacks_t* callbacks,
    const modbus_slave_identity_t* identity)
{
    modbus_slave_config_t config;

    if ( callbacks == NULL || identity == NULL ) {
        return MODBUS_SLAVE_ERR_ARG;
    }

    config.address            = address;
    config.baudrate           = baudrate;
    config.rx_buffer          = context.rx_buffer;
    config.rx_capacity        = (uint16_t)MB_BUFFER_SIZE;
    config.tx_buffer          = context.tx_buffer;
    config.tx_capacity        = (uint16_t)MB_BUFFER_SIZE;
    config.port.ctx           = (void*)&context;
    config.port.serial_enable = board_serial_enable;
    config.port.put_byte      = board_put_byte;
    config.port.tx_done       = board_tx_done;
    config.port.timer_enable  = board_timer_enable;
    config.port.timer_disable = board_timer_disable;
    config.port.critical_enter = board_critical_enter;
    config.port.critical_exit  = board_critical_exit;
    config.callbacks          = *callbacks;
    config.identity           = *identity;

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

/* Interrupt handlers ---------------------------------------------------- */

/* The WorkBench NVIC leaves the handler names to the user (last digit 0 in
 * the ATWP), and at32f421_int.c carries only the template stubs, so this
 * file is the single definition site — as is board.c's SysTick_Handler. */
void USART1_IRQHandler(void)
{
    /* Order matters: RDBF first, because clearing the error flags below
     * reads sts+dt and would otherwise eat the received byte. */
    if ( usart_interrupt_flag_get(USART1, USART_RDBF_FLAG) != RESET ) {
        uint16_t data = usart_data_receive(USART1);

        (void)modbus_slave_rx_byte(&slave, (uint8_t)data);
    }

    if ( usart_interrupt_flag_get(USART1, USART_TDBE_FLAG) != RESET ) {
        /* The core mutes TXE itself (tx_done) once the last byte is loaded
         * or nothing is left to send; TC then closes the frame. */
        (void)modbus_slave_tx_ready(&slave);
    }

    if ( usart_interrupt_flag_get(USART1, USART_TDC_FLAG) != RESET ) {
        usart_flag_clear(USART1, USART_TDC_FLAG);
        (void)modbus_slave_tx_complete(&slave);
    }

    /* ERR is a level interrupt: leaving NERR/ROERR/FERR/PERR set would
     * re-enter this handler forever. usart_flag_clear() reads sts and then
     * dt, so only the flags that are actually set are collected and
     * cleared; an unset flag must not touch the data register. */
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
