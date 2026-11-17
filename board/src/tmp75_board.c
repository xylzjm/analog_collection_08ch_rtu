#include "tmp75_board.h"

#include "i2c_soft.h"
#include "i2c_soft_board.h"

static tmp75_t device;

/* The driver asks for one frame; the bus offers the write and read-write shapes. */
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

/* A stuck SDA (slave reset mid-byte) is freed here and the frame retried once; NACK skips recovery because the device is simply absent. */
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
