#include "tmp75_board.h"

#include "i2c_soft.h"
#include "i2c_soft_board.h"

static tmp75_t device;

/* 驱动只请求一帧；总线提供写与写-读两种形态。 */
static i2c_soft_status_t transfer_once(i2c_soft_t* bus,
                                       uint8_t address,
                                       const uint8_t* tx,
                                       size_t tx_len,
                                       uint8_t* rx,
                                       size_t rx_len)
{
    if ( rx_len == 0U ) {
        return i2c_soft_write(bus, address, tx, tx_len);
    }
    return i2c_soft_write_read(bus, address, tx, tx_len, rx, rx_len);
}

/* 这里释放卡住的 SDA（从站在字节中途复位）并重试该帧一次；NACK 则跳过恢复，因为器件只是不存在。 */
static tmp75_status_t transfer(void* ctx,
                               uint8_t address,
                               const uint8_t* tx,
                               size_t tx_len,
                               uint8_t* rx,
                               size_t rx_len)
{
    i2c_soft_t* bus = (i2c_soft_t*)ctx;
    i2c_soft_status_t status;

    status = transfer_once(bus, address, tx, tx_len, rx, rx_len);
    if ( status == I2C_SOFT_ERR_BUS ) {
        (void)i2c_soft_bus_recover(bus);
        status = transfer_once(bus, address, tx, tx_len, rx, rx_len);
    }
    return (status == I2C_SOFT_OK) ? TMP75_OK : TMP75_ERR_IO;
}

tmp75_status_t tmp75_board_ctor(void)
{
    tmp75_port_t port;

    port.ctx      = (void*)i2c_soft_board_get();
    port.transfer = transfer;

    return tmp75_ctor(&device, &port, TMP75_BOARD_ADDRESS);
}

tmp75_t* tmp75_board_get_device(void)
{
    return &device;
}
