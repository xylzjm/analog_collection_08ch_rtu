#include "i2c_soft_board.h"

#include "at32f421_wk_config.h"
#include "wk_system.h"

#include "board.h"

static i2c_soft_t i2c_bus;

/* Open-drain pins: writing 1 releases the line, the library never drives high. */
static void sda_write(void* ctx, uint8_t level)
{
    (void)ctx;
    gpio_bits_write(I2C_SDA_GPIO_PORT, I2C_SDA_PIN,
                    (level != 0U) ? TRUE : FALSE);
}

static void scl_write(void* ctx, uint8_t level)
{
    (void)ctx;
    gpio_bits_write(I2C_SCL_GPIO_PORT, I2C_SCL_PIN,
                    (level != 0U) ? TRUE : FALSE);
}

/* Read the pin, not the output latch: a released line is held high by the pull-up. */
static uint8_t sda_read(void* ctx)
{
    (void)ctx;
    return (gpio_input_data_bit_read(I2C_SDA_GPIO_PORT, I2C_SDA_PIN) != RESET)
               ? 1U
               : 0U;
}

static uint8_t scl_read(void* ctx)
{
    (void)ctx;
    return (gpio_input_data_bit_read(I2C_SCL_GPIO_PORT, I2C_SCL_PIN) != RESET)
               ? 1U
               : 0U;
}

/* TMR6-based, so it keeps working inside the library's masked edge groups. */
static void delay_us(void* ctx, uint32_t us)
{
    (void)ctx;
    wk_delay_us(us);
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

i2c_soft_status_t i2c_soft_board_init(void)
{
    i2c_soft_config_t config;
    i2c_soft_status_t status;

    config.port.ctx       = NULL;
    config.port.sda_write = sda_write;
    config.port.scl_write = scl_write;
    config.port.sda_read  = sda_read;
    config.port.scl_read  = scl_read;
    config.port.delay_us  = delay_us;
    config.critical.ctx   = NULL;
    config.critical.enter = critical_enter;
    config.critical.exit  = critical_exit;
    /* 0 selects the library default of 5 us: standard mode has the margin. */
    config.half_period_us = 0U;

    status = i2c_soft_ctor(&i2c_bus, &config);
    if ( status != I2C_SOFT_OK ) {
        return status;
    }

    return i2c_soft_init(&i2c_bus);
}

i2c_soft_t* i2c_soft_board_get(void)
{
    return &i2c_bus;
}
