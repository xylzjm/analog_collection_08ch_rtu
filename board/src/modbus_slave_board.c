#include "modbus_slave_board.h"

#include "at32f421_wk_config.h"

#include "board.h"

/* TMR14 is configured by wk_tmr14_init(): PSC 1199 gives 10 us per tick at
 * APB1 = 120 MHz, one-cycle mode is on and the counter is left stopped.  The
 * 16-bit counter spans 655.35 ms, enough for T3.5 (1.75..4.011 ms over the
 * supported 9600..115200 baud) and for the TX watchdog at 9600 baud with a
 * 256-byte reply (~303 ms).  This module only reloads the period on each arm. */
#define MB_TMR_TICK_US   10U
#define MB_TMR_MAX_TICKS 65535U

#define MB_BUFFER_SIZE   256U

/* One context per hardware capability, as the core's three port structs ask. */
typedef struct {
    usart_type* uart;
    gpio_type* de_port;
    uint16_t de_pin;
} line_ctx_t;

typedef struct {
    tmr_type* timer;
} timer_ctx_t;

/* The single RS485 channel of this product: USART1, DE on PA11, TMR14 as the
 * T3.5 / TX-watchdog timer. */
static const line_ctx_t line_ctx = {
    USART1, USART_DE_GPIO_PORT, USART_DE_PIN
};

static const timer_ctx_t timer_ctx = {
    TMR14
};

static uint8_t rx_buffer[MB_BUFFER_SIZE];
static uint8_t tx_buffer[MB_BUFFER_SIZE];

static modbus_slave_t slave;

/* RS485 line (modbus_slave_port_line_t) ------------------------------- */

static modbus_slave_status_t line_direction(void* ctx,
                                            uint8_t rx,
                                            uint8_t tx)
{
    const line_ctx_t* b = (const line_ctx_t*)ctx;

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

static modbus_slave_status_t line_put_byte(void* ctx, uint8_t byte)
{
    const line_ctx_t* b = (const line_ctx_t*)ctx;

    usart_data_transmit(b->uart, (uint16_t)byte);
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t line_tx_done(void* ctx)
{
    const line_ctx_t* b = (const line_ctx_t*)ctx;

    /* The core has loaded the last byte: stop requesting TXE and let TC
     * close the frame (DE is released from the TC path only). */
    usart_interrupt_enable(b->uart, USART_TDBE_INT, FALSE);
    return MODBUS_SLAVE_OK;
}

/* One-shot timer (modbus_slave_port_timer_t) -------------------------- */

static modbus_slave_status_t timer_arm(void* ctx, uint32_t timeout_us)
{
    const timer_ctx_t* t = (const timer_ctx_t*)ctx;
    uint32_t ticks;

    /* Round the microsecond timeout up to whole 10 us ticks and clamp it
     * into the 16-bit range (the core's timeouts fit: max ~303 ms at
     * 9600 baud with a 256-byte reply). */
    ticks = (timeout_us + MB_TMR_TICK_US - 1U) / MB_TMR_TICK_US;
    if ( ticks == 0U ) {
        ticks = 1U;
    }
    if ( ticks > MB_TMR_MAX_TICKS ) {
        ticks = MB_TMR_MAX_TICKS;
    }

    /* wk_tmr14_init() already set direction, DIV1, PSC, period buffering,
     * one-cycle mode and the OVF source, and left the counter stopped.  Only
     * the period changes per arm: mask the IRQ, stop the counter, load the
     * new period, restart from zero, clear the stale OVF and re-arm. */
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

/* Critical section (modbus_slave_port_critical_t) --------------------- */

/* The core's critical capability is a nesting interrupt mask: enter() raises
 * a count, exit() decrements it and re-enables interrupts at zero, and the
 * count belongs to the board.  The board keeps exactly one such count
 * (board_crit_nest) behind board.h's board_critical_enter()/
 * board_critical_exit() macros, and the QP critical section expands the very
 * same macros, so the core's mask is the mask of the framework's.  Sharing
 * the count is what makes the two nest together: a core call made from inside
 * a QP critical section (or the other way round) leaves the interrupts masked
 * until the outermost exit, and the framework's rule that the outermost entry
 * finds the interrupts enabled still holds. */
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

/* Construction / access ----------------------------------------------- */

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

/* Interrupt handlers --------------------------------------------------- */

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
