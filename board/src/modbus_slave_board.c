#include "modbus_slave_board.h"

#include "at32f421_wk_config.h"

/* TMR14 runs from APB1 = 120 MHz: PSC 1199 gives 10 us per tick, so the
 * 16-bit counter spans 655.36 ms — enough for T3.5 (1.75..4.011 ms) and the
 * TX watchdog (9600 baud, 256 B: ~303 ms). */
#define MB_TMR_TICK_US   10U
#define MB_TMR_PSC       1199U
#define MB_TMR_MAX_TICKS 65535U

#define MB_BUFFER_SIZE   256U

typedef struct {
    usart_type* uart;
    gpio_type* de_port;
    uint16_t de_pin;
    tmr_type* timer;
    uint8_t rx_buffer[MB_BUFFER_SIZE];
    uint8_t tx_buffer[MB_BUFFER_SIZE];
    /** Set by put_byte(), consumed by the TDBE branch of USART1_IRQHandler. */
    volatile uint8_t tx_wrote;
} board_ctx_t;

/* The one RS485 channel of this product: USART1, DE on PA11, TMR14 as the
 * T3.5 / TX-watchdog timer. */
static board_ctx_t contexts[MODBUS_SLAVE_BOARD_COUNT] = {
    {USART1, USART_DE_GPIO_PORT, USART_DE_PIN, TMR14, {0}, {0}, 0},
};

static modbus_slave_t slaves[MODBUS_SLAVE_BOARD_COUNT];

/* Hardware port (modbus_slave_port_t) ------------------------------ */

static modbus_slave_status_t board_serial_enable(void* ctx,
                                                 uint8_t rx,
                                                 uint8_t tx)
{
    board_ctx_t* b = (board_ctx_t*)ctx;

    if ( b == NULL || b->uart == NULL || b->de_port == NULL
         || b->timer == NULL ) {
        return MODBUS_SLAVE_ERR_ARG;
    }

    /* The direction the call does not select is switched off unconditionally,
     * so every call satisfies the port contract ((0,0) closes both). */
    if ( rx != 0U ) {
        /* Mute the transmitter interrupts and disarm TC, then listen. */
        usart_interrupt_enable(b->uart, USART_TDBE_INT, FALSE);
        usart_interrupt_enable(b->uart, USART_TDC_INT, FALSE);
        usart_flag_clear(b->uart, USART_TDC_FLAG);
        usart_receiver_enable(b->uart, TRUE);
        usart_interrupt_enable(b->uart, USART_RDBF_INT, TRUE);
        /* TC was the last bit out, so releasing the driver is safe here. */
        gpio_bits_reset(b->de_port, b->de_pin);
    } else {
        usart_interrupt_enable(b->uart, USART_RDBF_INT, FALSE);
        usart_receiver_enable(b->uart, FALSE);
    }

    if ( tx != 0U ) {
        /* Assert DE before arming TXE: the first TXE interrupt can fire as
         * soon as its enable bit lands, and the first byte must not leave
         * with the RS485 driver still disabled. */
        gpio_bits_set(b->de_port, b->de_pin);
        usart_flag_clear(b->uart, USART_TDC_FLAG);
        usart_interrupt_enable(b->uart, USART_TDC_INT, TRUE);
        usart_interrupt_enable(b->uart, USART_TDBE_INT, TRUE);
    } else {
        usart_interrupt_enable(b->uart, USART_TDBE_INT, FALSE);
        usart_interrupt_enable(b->uart, USART_TDC_INT, FALSE);
        usart_flag_clear(b->uart, USART_TDC_FLAG);
        if ( rx == 0U ) {
            /* (0,0): both directions off, bus released. */
            gpio_bits_reset(b->de_port, b->de_pin);
        }
    }

    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t board_put_byte(void* ctx, uint8_t byte)
{
    board_ctx_t* b = (board_ctx_t*)ctx;

    if ( b == NULL || b->uart == NULL ) {
        return MODBUS_SLAVE_ERR_ARG;
    }

    b->tx_wrote = 1U;
    usart_data_transmit(b->uart, (uint16_t)byte);
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t board_timer_enable(void* ctx, uint32_t timeout_us)
{
    board_ctx_t* b = (board_ctx_t*)ctx;
    uint32_t ticks;

    if ( b == NULL || b->timer == NULL ) {
        return MODBUS_SLAVE_ERR_ARG;
    }

    /* Round the microsecond timeout up to whole 10 us ticks and clamp it
     * into the 16-bit range (the core's timeouts fit: max ~303 ms). */
    ticks = (timeout_us + MB_TMR_TICK_US - 1U) / MB_TMR_TICK_US;
    if ( ticks == 0U ) {
        ticks = 1U;
    }
    if ( ticks > MB_TMR_MAX_TICKS ) {
        ticks = MB_TMR_MAX_TICKS;
    }

    /* One-cycle mode auto-stops at the overflow, so every arm re-arms it.
     * The OVF flag is cleared before the interrupt is enabled, so the
     * software update of tmr_base_init() cannot fake an expiry. */
    tmr_counter_enable(b->timer, FALSE);
    tmr_one_cycle_mode_enable(b->timer, FALSE);
    tmr_base_init(b->timer, ticks - 1U, MB_TMR_PSC);
    tmr_counter_value_set(b->timer, 0U);
    tmr_flag_clear(b->timer, TMR_OVF_FLAG);
    tmr_one_cycle_mode_enable(b->timer, TRUE);
    tmr_interrupt_enable(b->timer, TMR_OVF_INT, TRUE);
    tmr_counter_enable(b->timer, TRUE);
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t board_timer_disable(void* ctx)
{
    board_ctx_t* b = (board_ctx_t*)ctx;

    if ( b == NULL || b->timer == NULL ) {
        return MODBUS_SLAVE_ERR_ARG;
    }

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
    uint8_t channel,
    uint8_t address,
    uint32_t baudrate,
    const modbus_slave_callbacks_t* callbacks,
    const modbus_slave_identity_t* identity)
{
    modbus_slave_config_t config;
    board_ctx_t* b;

    if ( channel == 0U || channel > MODBUS_SLAVE_BOARD_COUNT ) {
        return MODBUS_SLAVE_ERR_ARG;
    }
    if ( callbacks == NULL || identity == NULL ) {
        return MODBUS_SLAVE_ERR_ARG;
    }

    b = &contexts[channel - 1U];

    config.address            = address;
    config.baudrate           = baudrate;
    config.rx_buffer          = b->rx_buffer;
    config.rx_capacity        = (uint16_t)MB_BUFFER_SIZE;
    config.tx_buffer          = b->tx_buffer;
    config.tx_capacity        = (uint16_t)MB_BUFFER_SIZE;
    config.port.ctx           = (void*)b;
    config.port.serial_enable = board_serial_enable;
    config.port.put_byte      = board_put_byte;
    config.port.timer_enable  = board_timer_enable;
    config.port.timer_disable = board_timer_disable;
    config.port.critical_enter = board_critical_enter;
    config.port.critical_exit  = board_critical_exit;
    config.callbacks          = *callbacks;
    config.identity           = *identity;

    b->tx_wrote = 0U;

    return modbus_slave_ctor(&slaves[channel - 1U], &config);
}

uint8_t modbus_slave_board_count(void)
{
    return (uint8_t)MODBUS_SLAVE_BOARD_COUNT;
}

modbus_slave_t* modbus_slave_board_get(uint8_t channel)
{
    if ( channel == 0U || channel > MODBUS_SLAVE_BOARD_COUNT ) {
        return NULL;
    }
    return &slaves[channel - 1U];
}

/* ISR glue --------------------------------------------------------------- */

void modbus_slave_board_rx_irq(uint8_t channel, uint8_t byte)
{
    if ( channel == 0U || channel > MODBUS_SLAVE_BOARD_COUNT ) {
        return;
    }
    (void)modbus_slave_rx_byte(&slaves[channel - 1U], byte);
}

void modbus_slave_board_tx_ready_irq(uint8_t channel)
{
    if ( channel == 0U || channel > MODBUS_SLAVE_BOARD_COUNT ) {
        return;
    }
    (void)modbus_slave_tx_ready(&slaves[channel - 1U]);
}

void modbus_slave_board_tx_complete_irq(uint8_t channel)
{
    if ( channel == 0U || channel > MODBUS_SLAVE_BOARD_COUNT ) {
        return;
    }
    (void)modbus_slave_tx_complete(&slaves[channel - 1U]);
}

void modbus_slave_board_timer_irq(uint8_t channel)
{
    if ( channel == 0U || channel > MODBUS_SLAVE_BOARD_COUNT ) {
        return;
    }
    (void)modbus_slave_timer_expired(&slaves[channel - 1U]);
}

/* Interrupt handlers ------------------------------------------------------ */

/* The WorkBench NVIC leaves the handler names to the user (last digit 0 in
 * the ATWP), and at32f421_int.c carries only the template stubs, so this
 * file is the single definition site — as is board.c's SysTick_Handler. */
void USART1_IRQHandler(void)
{
    board_ctx_t* b = &contexts[0];

    /* Order matters: RDBF first, because clearing the error flags below
     * reads sts+dt and would otherwise eat the received byte. */
    if ( usart_interrupt_flag_get(USART1, USART_RDBF_FLAG) != RESET ) {
        uint16_t data = usart_data_receive(USART1);

        modbus_slave_board_rx_irq(1U, (uint8_t)data);
    }

    if ( usart_interrupt_flag_get(USART1, USART_TDBE_FLAG) != RESET ) {
        b->tx_wrote = 0U;
        modbus_slave_board_tx_ready_irq(1U);
        if ( b->tx_wrote == 0U ) {
            /* The last byte is already in the data register: TXE stays set
             * and would interrupt forever, so mute it and let TC finish the
             * frame (DE is released by the core from the TC path only). */
            usart_interrupt_enable(USART1, USART_TDBE_INT, FALSE);
        }
    }

    if ( usart_interrupt_flag_get(USART1, USART_TDC_FLAG) != RESET ) {
        usart_flag_clear(USART1, USART_TDC_FLAG);
        modbus_slave_board_tx_complete_irq(1U);
    }

    /* ERR is a level interrupt: leaving NERR/ROERR/FERR set would re-enter
     * this handler forever, so they are cleared unconditionally. */
    if ( usart_interrupt_flag_get(USART1, USART_NERR_FLAG) != RESET
         || usart_interrupt_flag_get(USART1, USART_ROERR_FLAG) != RESET
         || usart_interrupt_flag_get(USART1, USART_FERR_FLAG) != RESET ) {
        usart_flag_clear(USART1,
                         USART_NERR_FLAG | USART_ROERR_FLAG | USART_FERR_FLAG);
    }
    if ( usart_interrupt_flag_get(USART1, USART_PERR_FLAG) != RESET ) {
        usart_flag_clear(USART1, USART_PERR_FLAG);
    }
}

void TMR14_GLOBAL_IRQHandler(void)
{
    if ( tmr_flag_get(TMR14, TMR_OVF_FLAG) != RESET ) {
        tmr_flag_clear(TMR14, TMR_OVF_FLAG);
        modbus_slave_board_timer_irq(1U);
    }
}
