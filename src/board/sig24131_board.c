#include "sig24131_board.h"

#include "wk_system.h"

#define SIG24131_BOARD_TICK_PERIOD_US      1000U
#define SIG24131_BOARD_SPI_TRANSFER_TIMEOUT_TICKS 10U

typedef struct {
    gpio_type* cs_gpio_port;
    uint16_t cs_pin;
} sig24131_board_cs_t;

/* Optional channels are compiled out so a four-device board only needs to
 * define the CTRL_nCS_1..4 pin macros. */
static const sig24131_board_cs_t sig24131_board_cs_bindings[SIG24131_BOARD_INSTANCE_COUNT] = {
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

static sig24131_t sig24131_board_devices[SIG24131_BOARD_INSTANCE_COUNT];

static sig24131_status_t sig24131_board_wait_flag(uint32_t flag)
{
    uint32_t start_tick;

    start_tick = wk_tick_get();
    while (spi_i2s_flag_get(SPI1, flag) == RESET) {
        if ((uint32_t)(wk_tick_get() - start_tick) >=
            SIG24131_BOARD_SPI_TRANSFER_TIMEOUT_TICKS) {
            return SIG24131_ERR_IO;
        }
    }
    return SIG24131_OK;
}

static sig24131_status_t sig24131_board_spi_transfer(void* ctx,
                                                     const uint8_t* tx,
                                                     uint8_t* rx,
                                                     size_t len)
{
    size_t index;
    sig24131_status_t status;

    (void)ctx;
    if ((tx == NULL || rx == NULL) && len != 0U) {
        return SIG24131_ERR_ARG;
    }

    for (index = 0U; index < len; ++index) {
        status = sig24131_board_wait_flag(SPI_I2S_TDBE_FLAG);
        if (status != SIG24131_OK) {
            return status;
        }

        spi_i2s_data_transmit(SPI1, tx[index]);

        status = sig24131_board_wait_flag(SPI_I2S_RDBF_FLAG);
        if (status != SIG24131_OK) {
            return status;
        }

        rx[index] = (uint8_t)spi_i2s_data_receive(SPI1);
    }

    return SIG24131_OK;
}

static sig24131_status_t sig24131_board_cs_assert(void* ctx)
{
    const sig24131_board_cs_t* cs;

    cs = (const sig24131_board_cs_t*)ctx;
    if (cs == NULL || cs->cs_gpio_port == NULL) {
        return SIG24131_ERR_ARG;
    }
    gpio_bits_reset(cs->cs_gpio_port, cs->cs_pin);
    return SIG24131_OK;
}

static sig24131_status_t sig24131_board_cs_release(void* ctx)
{
    const sig24131_board_cs_t* cs;

    cs = (const sig24131_board_cs_t*)ctx;
    if (cs == NULL || cs->cs_gpio_port == NULL) {
        return SIG24131_ERR_ARG;
    }
    gpio_bits_set(cs->cs_gpio_port, cs->cs_pin);
    return SIG24131_OK;
}

static sig24131_status_t sig24131_board_delay_us(void* ctx,
                                                 uint32_t us)
{
    uint32_t delay_ticks;
    uint32_t start_tick;

    if (ctx == NULL) {
        return SIG24131_ERR_ARG;
    }

    delay_ticks = us / SIG24131_BOARD_TICK_PERIOD_US;
    if ((us % SIG24131_BOARD_TICK_PERIOD_US) != 0U) {
        ++delay_ticks;
    }

    start_tick = wk_tick_get();
    while ((uint32_t)(wk_tick_get() - start_tick) < delay_ticks) {
    }
    return SIG24131_OK;
}

sig24131_status_t sig24131_board_init(void)
{
    uint8_t index;
    sig24131_status_t status;
    sig24131_port_t port;

    for (index = 0U; index < SIG24131_BOARD_INSTANCE_COUNT; ++index) {
        port.ctx          = (void*)&sig24131_board_cs_bindings[index];
        port.spi_transfer = sig24131_board_spi_transfer;
        port.cs_assert    = sig24131_board_cs_assert;
        port.cs_release   = sig24131_board_cs_release;
        port.delay_us     = sig24131_board_delay_us;

        status = sig24131_init(&sig24131_board_devices[index], &port);
        if (status != SIG24131_OK) {
            return status;
        }

        status = sig24131_board_cs_release(port.ctx);
        if (status != SIG24131_OK) {
            return status;
        }
    }

    return SIG24131_OK;
}

sig24131_status_t sig24131_board_reset(void)
{
    uint8_t index;
    sig24131_status_t status;

    for (index = 0U; index < SIG24131_BOARD_INSTANCE_COUNT; ++index) {
        status = sig24131_reset(&sig24131_board_devices[index]);
        if (status != SIG24131_OK) {
            return status;
        }
    }
    return SIG24131_OK;
}

uint8_t sig24131_board_count(void)
{
    return (uint8_t)SIG24131_BOARD_INSTANCE_COUNT;
}

sig24131_t* sig24131_board_get_device(uint8_t channel)
{
    if (channel == 0U || channel > SIG24131_BOARD_INSTANCE_COUNT) {
        return NULL;
    }
    return &sig24131_board_devices[channel - 1U];
}
