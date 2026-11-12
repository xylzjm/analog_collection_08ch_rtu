#include "modbus_cal_regs.h"

bool modbus_cal_regs_in_block(uint16_t start, uint16_t count)
{
    uint32_t last;

    if ( count == 0U ) {
        return false;
    }
    last = (uint32_t)start + (uint32_t)count - 1U;
    return (start >= MODBUS_CAL_BASE && last <= MODBUS_CAL_LAST) ? true : false;
}

bool modbus_cal_regs_overlaps(uint16_t start, uint16_t count)
{
    uint32_t last;

    if ( count == 0U ) {
        return false;
    }
    last = (uint32_t)start + (uint32_t)count - 1U;
    return (last >= MODBUS_CAL_BASE && start <= MODBUS_CAL_LAST) ? true : false;
}

bool modbus_cal_regs_valid_cmd(uint16_t cmd)
{
    return (cmd >= MODBUS_CAL_CMD_SELECT && cmd <= MODBUS_CAL_CMD_EXIT) ? true
                                                                        : false;
}
