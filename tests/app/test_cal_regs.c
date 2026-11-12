#include <assert.h>
#include <stdio.h>

#include "modbus_cal_regs.h"

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

    puts("test_cal_regs: PASS");
    return 0;
}
