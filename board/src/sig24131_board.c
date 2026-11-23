#include "sig24131_board.h"
#include "board.h"

#include "at32f421_wk_config.h"
#include "wk_system.h"

/** @brief SPI 标志超时，单位为板级节拍毫秒。 */
#define SPI_TRANSFER_TIMEOUT_TICKS 10U

/** @brief 一个 SIG24131 器件的片选绑定。 */
typedef struct {
    gpio_type* cs_gpio_port;
    uint16_t cs_pin;
} cs_binding_t;

/* 可选通道随 SIG24131_BOARD_INSTANCE_COUNT 一同编译剔除。 */
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

    start_tick = board_tick_get();
    while ( spi_i2s_flag_get(SPI1, flag) == RESET ) {
        if ( (uint32_t)(board_tick_get() - start_tick)
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

sig24131_status_t sig24131_board_ctor(uint8_t channel)
{
    sig24131_port_t port;
    sig24131_status_t status;

    if ( channel >= SIG24131_BOARD_INSTANCE_COUNT ) {
        return SIG24131_ERR_ARG;
    }

    port.ctx          = (void*)&cs_bindings[channel];
    port.spi_transfer = spi_transfer;
    port.cs_assert    = cs_assert;
    port.cs_release   = cs_release;

    status = sig24131_ctor(&devices[channel], &port);
    if ( status != SIG24131_OK ) {
        return status;
    }

    return cs_release(port.ctx);
}

sig24131_status_t sig24131_board_ctor_all(void)
{
    sig24131_status_t status;
    uint8_t channel;

    for ( channel = 0U; channel < SIG24131_BOARD_INSTANCE_COUNT; ++channel ) {
        status = sig24131_board_ctor(channel);
        if ( status != SIG24131_OK ) {
            return status;
        }
    }

    return SIG24131_OK;
}

sig24131_status_t sig24131_board_reset(uint8_t channel)
{
    if ( channel >= SIG24131_BOARD_INSTANCE_COUNT ) {
        return SIG24131_ERR_ARG;
    }

    return sig24131_reset(&devices[channel]);
}

sig24131_status_t sig24131_board_reset_all(void)
{
    sig24131_status_t status;
    sig24131_status_t result = SIG24131_OK;
    uint8_t channel;

    for ( channel = 0U; channel < SIG24131_BOARD_INSTANCE_COUNT; ++channel ) {
        status = sig24131_board_reset(channel);
        if ( status != SIG24131_OK && result == SIG24131_OK ) {
            result = status;
        }
    }

    /* 先发出所有复位，然后为每个器件统一等待一次。 */
    wk_delay_ms(SIG24131_RESET_DELAY_MS);
    return result;
}

sig24131_t* sig24131_board_get_device(uint8_t channel)
{
    if ( channel >= SIG24131_BOARD_INSTANCE_COUNT ) {
        return NULL;
    }
    return &devices[channel];
}
