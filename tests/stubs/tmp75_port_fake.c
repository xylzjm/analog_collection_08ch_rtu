/**
 * @file tmp75_port_fake.c
 * @brief 宿主上供真实 TMP75 驱动使用的可编排 I2C 传输。
 *
 * 被测驱动是真实的 tmp75.c；只有总线被伪造。温度寄存器格式是两个数据字节高
 * 12 位中的 12 位二进制补码码值（每计数对应 0.0625 degC）。
 */

#include "tmp75_port_fake.h"

static tmp75_status_t transfer(void* ctx,
                               uint8_t address,
                               const uint8_t* tx,
                               size_t tx_len,
                               uint8_t* rx,
                               size_t rx_len)
{
    tmp75_port_fake_t* f = (tmp75_port_fake_t*)ctx;

    (void)address;
    (void)tx;
    (void)tx_len;

    ++f->calls;
    if ( f->status != TMP75_OK ) {
        return f->status;
    }
    if ( rx_len >= 2U ) {
        rx[0] = f->rx_frame[0];
        rx[1] = f->rx_frame[1];
    }
    return TMP75_OK;
}

void tmp75_port_fake_init(tmp75_port_fake_t* f)
{
    f->status = TMP75_OK;
    f->calls  = 0U;
    tmp75_port_fake_set_temperature(f, 25.0f);
}

void tmp75_port_fake_set_temperature(tmp75_port_fake_t* f,
                                     float temp_c)
{
    uint16_t code = (uint16_t)(int16_t)(temp_c * 16.0f) << 4;

    f->rx_frame[0] = (uint8_t)(code >> 8);
    f->rx_frame[1] = (uint8_t)(code & 0xFFU);
}

void tmp75_port_fake_fail(tmp75_port_fake_t* f)
{
    f->status = TMP75_ERR_IO;
}

void tmp75_port_fake_port(tmp75_port_fake_t* f,
                          tmp75_port_t* out)
{
    out->ctx      = f;
    out->transfer = &transfer;
}
