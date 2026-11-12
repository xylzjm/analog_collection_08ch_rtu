#include <assert.h>
#include <stdio.h>

#include "modbus_app_regs.h"
#include "modbus_cal_regs.h"

static void test_check_write(void)
{
    uint8_t buffer[8];

    modbus_app_regs_put_u16(&buffer[0], 3U);
    modbus_app_regs_put_u16(&buffer[2], 1U);
    assert(modbus_cal_regs_check_write(0x0F03U, 2U, buffer)
           == MODBUS_CAL_WRITE_OK);
    assert(modbus_cal_regs_check_write(0x0F03U, 3U, buffer)
           == MODBUS_CAL_WRITE_BAD_ADDRESS);

    modbus_app_regs_put_u16(&buffer[0], MODBUS_CAL_MAGIC1_VALUE);
    modbus_app_regs_put_u16(&buffer[2], MODBUS_CAL_MAGIC2_VALUE);
    assert(modbus_cal_regs_check_write(0x0F00U, 2U, buffer)
           == MODBUS_CAL_WRITE_OK);

    modbus_app_regs_put_u16(&buffer[0], MODBUS_CAL_CMD_EXIT);
    assert(modbus_cal_regs_check_write(0x0F02U, 1U, buffer)
           == MODBUS_CAL_WRITE_OK);
    modbus_app_regs_put_u16(&buffer[0], 0U);
    assert(modbus_cal_regs_check_write(0x0F02U, 1U, buffer)
           == MODBUS_CAL_WRITE_BAD_VALUE);
    assert(modbus_cal_regs_check_write(0x0F02U, 2U, buffer)
           == MODBUS_CAL_WRITE_BAD_VALUE);
    assert(modbus_cal_regs_check_write(0x0F01U, 2U, buffer)
           == MODBUS_CAL_WRITE_BAD_VALUE);

    assert(modbus_cal_regs_check_write(0x0F11U, 1U, buffer)
           == MODBUS_CAL_WRITE_BAD_ADDRESS);
    assert(modbus_cal_regs_check_write(0x0EFFU, 2U, buffer)
           == MODBUS_CAL_WRITE_BAD_ADDRESS);
    assert(modbus_cal_regs_check_write(0x0F03U, 1U, NULL)
           == MODBUS_CAL_WRITE_BAD_ADDRESS);
}

int main(void)
{
    assert(modbus_cal_regs_in_block(0x0F00U, 1U));
    assert(modbus_cal_regs_in_block(0x0F00U, 0x20U));
    assert(!modbus_cal_regs_in_block(0x0EFFU, 2U));
    assert(!modbus_cal_regs_in_block(0x0F00U, 0x21U));

    assert(modbus_cal_regs_overlaps(0x0EFFU, 3U));
    assert(modbus_cal_regs_overlaps(0x0F1FU, 2U));
    assert(!modbus_cal_regs_overlaps(0x0F20U, 1U));

    assert(modbus_cal_regs_valid_cmd(1U));
    assert(modbus_cal_regs_valid_cmd(6U));
    assert(!modbus_cal_regs_valid_cmd(0U));
    assert(!modbus_cal_regs_valid_cmd(7U));

    test_check_write();

    puts("test_cal_regs: PASS");
    return 0;
}
