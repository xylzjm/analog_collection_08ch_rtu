#include "modbus_cal_regs.h"

#include <stddef.h>

#include "modbus_app_regs.h"

modbus_cal_write_status_t modbus_cal_regs_check_write(
    uint16_t start,
    uint16_t count,
    uint8_t const* buffer)
{
    uint16_t i;
    uint16_t address;
    uint16_t value;

    if ( buffer == NULL || modbus_cal_regs_in_block(start, count) == false ) {
        return MODBUS_CAL_WRITE_BAD_ADDRESS;
    }

    for ( i = 0U; i < count; ++i ) {
        address = (uint16_t)(start + i);
        value   = modbus_app_regs_get_u16(&buffer[2U * i]);

        switch ( address ) {
            case MODBUS_CAL_MAGIC1:
            case MODBUS_CAL_MAGIC2:
            case MODBUS_CAL_CHANNEL:
            case MODBUS_CAL_TYPE:
                break;

            case MODBUS_CAL_CMD:
                if ( count != 1U
                     || modbus_cal_regs_valid_cmd(value) == false ) {
                    return MODBUS_CAL_WRITE_BAD_VALUE;
                }
                break;

            default:
                return MODBUS_CAL_WRITE_BAD_ADDRESS;
        }
    }
    return MODBUS_CAL_WRITE_OK;
}

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
