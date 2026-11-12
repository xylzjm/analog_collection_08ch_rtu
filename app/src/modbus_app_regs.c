#include "modbus_app_regs.h"

#include <stddef.h>

static const uint32_t rates_millihz[] = {
    2500U,   /* 2.5 Hz */
    5000U,   /* 5 Hz */
    10000U,  /* 10 Hz */
    12500U,  /* 12.5 Hz */
    15000U,  /* 15 Hz */
    20000U,  /* 20 Hz */
    25000U,  /* 25 Hz */
    30000U,  /* 30 Hz */
    40000U,  /* 40 Hz */
    50000U,  /* 50 Hz */
    60000U,  /* 60 Hz */
    80000U,  /* 80 Hz */
    100000U  /* 100 Hz */
};

bool modbus_app_regs_type_valid(uint16_t value)
{
    return (value <= 10U) ? true : false;
}

bool modbus_app_regs_type_is_linear(uint16_t value)
{
    return (value <= 1U) ? true : false;
}

bool modbus_app_regs_rate_valid(uint16_t value)
{
    return (value < (uint16_t)(sizeof(rates_millihz) / sizeof(rates_millihz[0]))
            || value == 0xFFFFU)
               ? true
               : false;
}

uint32_t modbus_app_regs_rate_millihz(uint16_t index)
{
    if ( index >= (uint16_t)(sizeof(rates_millihz) / sizeof(rates_millihz[0])) ) {
        return rates_millihz[2];
    }
    return rates_millihz[index];
}

uint8_t modbus_app_regs_rate_store_index(uint16_t value)
{
    return (value == MODBUS_REG_RATE_DEFAULT)
               ? (uint8_t)MODBUS_REG_RATE_DEFAULT_INDEX
               : (uint8_t)value;
}

bool modbus_app_regs_gain_valid(int32_t gain)
{
    return (gain >= 100L && gain <= 10000L) ? true : false;
}

bool modbus_app_regs_offset_valid(int32_t offset)
{
    return (offset >= -1000000L && offset <= 1000000L) ? true : false;
}

bool modbus_app_regs_channel_addr(uint16_t address, uint8_t* channel, uint8_t* offset)
{
    uint16_t relative;

    if ( address < MODBUS_REG_CH_FIRST || address > MODBUS_REG_CH_LAST
         || channel == NULL || offset == NULL ) {
        return false;
    }

    relative = (uint16_t)(address - MODBUS_REG_CH_FIRST);
    *channel = (uint8_t)(relative / MODBUS_REG_CH_SIZE);
    *offset  = (uint8_t)(relative % MODBUS_REG_CH_SIZE);
    return true;
}

uint16_t modbus_app_regs_get_u16(const uint8_t* buffer)
{
    return (uint16_t)(((uint16_t)buffer[0] << 8U) | (uint16_t)buffer[1]);
}

void modbus_app_regs_put_u16(uint8_t* buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value >> 8U);
    buffer[1] = (uint8_t)value;
}

int32_t modbus_app_regs_get_s32(const uint8_t* buffer)
{
    uint32_t value;

    value = ((uint32_t)modbus_app_regs_get_u16(buffer) << 16U)
            | (uint32_t)modbus_app_regs_get_u16(&buffer[2]);
    return (int32_t)value;
}

void modbus_app_regs_put_s32(uint8_t* buffer, int32_t value)
{
    modbus_app_regs_put_u16(buffer, (uint16_t)((uint32_t)value >> 16U));
    modbus_app_regs_put_u16(&buffer[2], (uint16_t)value);
}
