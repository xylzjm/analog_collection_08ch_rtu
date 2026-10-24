#include "sig24131_board.h"

#include "wk_system.h"

#define SPI_TRANSFER_TIMEOUT_TICKS 10U

typedef struct {
    gpio_type* cs_gpio_port;
    uint16_t cs_pin;
} cs_binding_t;

/* Optional channels are compiled out so a four-device board only needs to
 * define the CTRL_nCS_1..4 pin macros. */
static const cs_binding_t cs_bindings[SIG24131_BOARD_INSTANCE_COUNT] = {
    {CTRL_nCS_1_GPIO_PORT, CTRL_nCS_1_PIN},
    {CTRL_nCS_2_GPIO_PORT, CTRL_nCS_2_PIN},
    {CTRL_nCS_3_GPIO_PORT, CTRL_nCS_3_PIN},
    {CTRL_nCS_4_GPIO_PORT, CTRL_nCS_4_PIN},
#if SIG24131_BOARD_INSTANCE_COUNT >= 5U
    {CTRL_nCS_5_GPIO_PORT, CTRL_nCS_5_PIN},
#endif
#if SIG24131_BOARD_INSTANCE_COUNT >= 6U
    {CTRL_nCS_6_GPIO_PORT, CTRL_nCS_6_PIN},
#endif
#if SIG24131_BOARD_INSTANCE_COUNT >= 7U
    {CTRL_nCS_7_GPIO_PORT, CTRL_nCS_7_PIN},
#endif
#if SIG24131_BOARD_INSTANCE_COUNT >= 8U
    {CTRL_nCS_8_GPIO_PORT, CTRL_nCS_8_PIN},
#endif
};

static sig24131_t devices[SIG24131_BOARD_INSTANCE_COUNT];

static sig24131_status_t wait_flag(uint32_t flag)
{
    uint32_t start_tick;

    start_tick = wk_tick_get();
    while ( spi_i2s_flag_get(SPI1, flag) == RESET ) {
        if ( (uint32_t)(wk_tick_get() - start_tick)
             >= SPI_TRANSFER_TIMEOUT_TICKS ) {
            return SIG24131_ERR_IO;
        }
    }
    return SIG24131_OK;
}

static sig24131_status_t spi_transfer(void* ctx,
                                      const uint8_t* tx,
                                      uint8_t* rx,
                                      size_t len)
{
    size_t index;
    sig24131_status_t status;

    (void)ctx;
    if ( (tx == NULL || rx == NULL) && len != 0U ) {
        return SIG24131_ERR_ARG;
    }

    for ( index = 0U; index < len; ++index ) {
        status = wait_flag(SPI_I2S_TDBE_FLAG);
        if ( status != SIG24131_OK ) {
            return status;
        }

        spi_i2s_data_transmit(SPI1, tx[index]);

        status = wait_flag(SPI_I2S_RDBF_FLAG);
        if ( status != SIG24131_OK ) {
            return status;
        }

        rx[index] = (uint8_t)spi_i2s_data_receive(SPI1);
    }

    return SIG24131_OK;
}

static sig24131_status_t cs_assert(void* ctx)
{
    const cs_binding_t* cs;

    cs = (const cs_binding_t*)ctx;
    if ( cs == NULL || cs->cs_gpio_port == NULL ) {
        return SIG24131_ERR_ARG;
    }
    gpio_bits_reset(cs->cs_gpio_port, cs->cs_pin);
    return SIG24131_OK;
}

static sig24131_status_t cs_release(void* ctx)
{
    const cs_binding_t* cs;

    cs = (const cs_binding_t*)ctx;
    if ( cs == NULL || cs->cs_gpio_port == NULL ) {
        return SIG24131_ERR_ARG;
    }
    gpio_bits_set(cs->cs_gpio_port, cs->cs_pin);
    return SIG24131_OK;
}

sig24131_status_t sig24131_board_ctor(void)
{
    uint8_t index;
    sig24131_status_t status;
    sig24131_port_t port;

    for ( index = 0U; index < SIG24131_BOARD_INSTANCE_COUNT; ++index ) {
        port.ctx          = (void*)&cs_bindings[index];
        port.spi_transfer = spi_transfer;
        port.cs_assert    = cs_assert;
        port.cs_release   = cs_release;

        status = sig24131_ctor(&devices[index], &port);
        if ( status != SIG24131_OK ) {
            return status;
        }

        status = cs_release(port.ctx);
        if ( status != SIG24131_OK ) {
            return status;
        }
    }

    return SIG24131_OK;
}

sig24131_status_t sig24131_board_reset(void)
{
    uint8_t index;
    sig24131_status_t status;
    sig24131_status_t result = SIG24131_OK;

    for ( index = 0U; index < SIG24131_BOARD_INSTANCE_COUNT; ++index ) {
        status = sig24131_reset(&devices[index]);
        if ( status != SIG24131_OK && result == SIG24131_OK ) {
            result = status;
        }
    }

    /* The commands above are only microseconds apart, so one wait after the last
     * one covers every device.  It runs even when a command failed, because the
     * devices that did accept theirs still need it. */
    wk_delay_ms(SIG24131_RESET_DELAY_MS);
    return result;
}

uint8_t sig24131_board_count(void)
{
    return (uint8_t)SIG24131_BOARD_INSTANCE_COUNT;
}

sig24131_t* sig24131_board_get_device(uint8_t channel)
{
    if ( channel == 0U || channel > SIG24131_BOARD_INSTANCE_COUNT ) {
        return NULL;
    }
    return &devices[channel - 1U];
}
