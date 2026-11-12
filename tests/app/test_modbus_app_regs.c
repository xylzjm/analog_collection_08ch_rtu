#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "modbus_app_regs.h"

static void test_endian(void)
{
    uint8_t buffer[4];

    modbus_app_regs_put_u16(buffer, 0x1234U);
    assert(buffer[0] == 0x12U && buffer[1] == 0x34U);
    assert(modbus_app_regs_get_u16(buffer) == 0x1234U);

    modbus_app_regs_put_s32(buffer, -2L);
    assert(modbus_app_regs_get_s32(buffer) == -2L);
    assert(buffer[0] == 0xFFU && buffer[1] == 0xFFU);
    assert(buffer[2] == 0xFFU && buffer[3] == 0xFEU);

    modbus_app_regs_put_s32(buffer, 0x7FFFFFFFL);
    assert(modbus_app_regs_get_s32(buffer) == 0x7FFFFFFFL);
}

static void test_types(void)
{
    uint16_t type;

    for ( type = 0U; type <= 10U; ++type ) {
        assert(modbus_app_regs_type_valid(type));
    }
    assert(!modbus_app_regs_type_valid(11U));

    assert(modbus_app_regs_type_is_linear(0U));
    assert(modbus_app_regs_type_is_linear(1U));
    assert(!modbus_app_regs_type_is_linear(2U));
    assert(!modbus_app_regs_type_is_linear(6U));
    assert(!modbus_app_regs_type_is_linear(10U));
}

static void test_rates(void)
{
    assert(modbus_app_regs_rate_valid(0U));
    assert(modbus_app_regs_rate_valid(12U));
    assert(modbus_app_regs_rate_valid(0xFFFFU));
    assert(!modbus_app_regs_rate_valid(13U));
    assert(modbus_app_regs_rate_millihz(0U) == 2500U);
    assert(modbus_app_regs_rate_millihz(2U) == 10000U);
    assert(modbus_app_regs_rate_millihz(12U) == 100000U);
    assert(modbus_app_regs_rate_millihz(0xFFFFU) == 10000U);

    assert(modbus_app_regs_rate_store_index(0xFFFFU)
           == MODBUS_REG_RATE_DEFAULT_INDEX);
    assert(modbus_app_regs_rate_store_index(7U) == 7U);
    assert(modbus_app_regs_rate_store_index(12U) == 12U);
}

static void test_ranges(void)
{
    uint8_t channel;
    uint8_t offset;

    assert(modbus_app_regs_gain_valid(100L));
    assert(modbus_app_regs_gain_valid(10000L));
    assert(!modbus_app_regs_gain_valid(99L));
    assert(!modbus_app_regs_gain_valid(10001L));
    assert(modbus_app_regs_offset_valid(-1000000L));
    assert(modbus_app_regs_offset_valid(1000000L));
    assert(!modbus_app_regs_offset_valid(-1000001L));

    assert(modbus_app_regs_channel_addr(0x0100U, &channel, &offset));
    assert(channel == 0U && offset == 0U);
    assert(modbus_app_regs_channel_addr(0x01FFU, &channel, &offset));
    assert(channel == 7U && offset == 0x1FU);
    assert(!modbus_app_regs_channel_addr(0x00FFU, &channel, &offset));
    assert(!modbus_app_regs_channel_addr(0x0200U, &channel, &offset));
}

int main(void)
{
    test_endian();
    test_types();
    test_rates();
    test_ranges();
    puts("test_modbus_app_regs: PASS");
    return 0;
}
