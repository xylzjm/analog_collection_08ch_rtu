#include "sig24131_stub.h"

#include <string.h>

#include "at32f421_wk_config.h"
#include "sig24131.h"
#include "sig24131_board.h"

int32_t stub_sig24131_sample;

void stub_sig24131_reset(void)
{
    stub_sig24131_sample = 0L;
}

void sig24131_default_config(sig24131_config_t* config)
{
    memset(config, 0, sizeof(*config));
}

sig24131_status_t sig24131_write_config(sig24131_t* device,
                                        const sig24131_config_t* config)
{
    (void)device;
    (void)config;
    return SIG24131_OK;
}

sig24131_status_t sig24131_read_data(sig24131_t* device, int32_t* data)
{
    (void)device;
    *data = stub_sig24131_sample;
    return SIG24131_OK;
}

sig24131_status_t sig24131_data_rate_from_millihz(
    uint32_t rate_millihz,
    sig24131_data_rate_t* data_rate)
{
    (void)rate_millihz;
    *data_rate = SIG24131_RATE_10;
    return SIG24131_OK;
}

sig24131_t* sig24131_board_get_device(uint8_t channel)
{
    (void)channel;
    return NULL;
}

void gpio_bits_set(gpio_type* port, uint16_t pin)
{
    (void)port;
    (void)pin;
}

void gpio_bits_reset(gpio_type* port, uint16_t pin)
{
    (void)port;
    (void)pin;
}
