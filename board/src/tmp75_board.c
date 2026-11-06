#include "tmp75_board.h"

#include "i2c_soft.h"
#include "i2c_soft_board.h"

static tmp75_t device;

/* The driver asks for one frame; the bus offers the write and read-write shapes. */
static tmp75_status_t transfer(void* ctx,
                               uint8_t address,
                               const uint8_t* tx,
                               size_t tx_len,
                               uint8_t* rx,
                               size_t rx_len)
{
    i2c_soft_t* bus = (i2c_soft_t*)ctx;
    i2c_soft_status_t status;

    if ( rx_len == 0U ) {
        status = i2c_soft_write(bus, address, tx, tx_len);
    } else {
        status = i2c_soft_write_read(bus, address, tx, tx_len, rx, rx_len);
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
