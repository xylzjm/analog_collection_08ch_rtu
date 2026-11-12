#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "param_app.h"

static void check_type(uint8_t type,
                       analog_input_type_t expected,
                       analog_input_thermocouple_type_t tc)
{
    analog_input_config_t config;

    assert(param_app_type_to_config(type, &config));
    assert(config.type == expected);
    assert(config.thermocouple_type == tc);
}

int main(void)
{
    param_sys_record_t sys;
    param_ch_record_t record;
    union {
        param_sys_record_t sys;
        param_ch_record_t ch;
    } snapshot;
    analog_input_config_t config;
    uint16_t len;
    uint32_t rev;
    uint32_t rev2;

    param_app_init();

    assert(param_app_get_address() == PARAM_APP_DEFAULT_ADDRESS);
    assert(param_app_dirty_mask() == 0U);

    assert(param_app_get_channel(0U, &record));
    assert(record.type == PARAM_APP_DEFAULT_TYPE);
    assert(record.rate_index == PARAM_APP_DEFAULT_RATE_INDEX);
    assert(record.user_gain == PARAM_APP_DEFAULT_USER_GAIN);
    assert(!param_app_get_channel(8U, &record));

    param_app_get_sys(&sys);
    sys.address = 5U;
    param_app_set_sys(&sys);
    assert((param_app_dirty_mask() & 0x0001U) != 0U);

    assert(param_app_snapshot(0U, &snapshot, &len, &rev));
    assert(len == sizeof(snapshot.sys));
    assert(((param_sys_record_t*)&snapshot)->address == 5U);
    param_app_clear_dirty(0U, rev + 1U);
    assert((param_app_dirty_mask() & 0x0001U) != 0U);
    param_app_clear_dirty(0U, rev);
    assert((param_app_dirty_mask() & 0x0001U) == 0U);

    assert(param_app_get_channel(2U, &record));
    record.type = 0U;
    record.linear_min = 0L;
    record.linear_max = 100000L;
    param_app_set_channel(2U, &record);
    assert((param_app_dirty_mask() & 0x0008U) != 0U);

    assert(param_app_snapshot(3U, &snapshot, &len, &rev2));
    assert(len == sizeof(snapshot.ch));
    assert(((param_ch_record_t*)&snapshot)->type == 0U);
    param_app_clear_dirty(3U, rev2);
    assert((param_app_dirty_mask() & 0x0008U) == 0U);

    check_type(0U, ANALOG_INPUT_TYPE_4_20MA,
               ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE);
    check_type(1U, ANALOG_INPUT_TYPE_0_10V,
               ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE);
    check_type(2U, ANALOG_INPUT_TYPE_PT100,
               ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE);
    check_type(3U, ANALOG_INPUT_TYPE_THERMOCOUPLE,
               ANALOG_INPUT_THERMOCOUPLE_TYPE_B);
    check_type(4U, ANALOG_INPUT_TYPE_THERMOCOUPLE,
               ANALOG_INPUT_THERMOCOUPLE_TYPE_E);
    check_type(5U, ANALOG_INPUT_TYPE_THERMOCOUPLE,
               ANALOG_INPUT_THERMOCOUPLE_TYPE_J);
    check_type(6U, ANALOG_INPUT_TYPE_THERMOCOUPLE,
               ANALOG_INPUT_THERMOCOUPLE_TYPE_K);
    check_type(7U, ANALOG_INPUT_TYPE_THERMOCOUPLE,
               ANALOG_INPUT_THERMOCOUPLE_TYPE_N);
    check_type(8U, ANALOG_INPUT_TYPE_THERMOCOUPLE,
               ANALOG_INPUT_THERMOCOUPLE_TYPE_R);
    check_type(9U, ANALOG_INPUT_TYPE_THERMOCOUPLE,
               ANALOG_INPUT_THERMOCOUPLE_TYPE_S);
    check_type(10U, ANALOG_INPUT_TYPE_THERMOCOUPLE,
               ANALOG_INPUT_THERMOCOUPLE_TYPE_T);
    assert(!param_app_type_to_config(11U, &config));
    assert(param_app_linear_valid(0L, 1L));
    assert(!param_app_linear_valid(1L, 1L));

    record.version = 99U;
    record.size = (uint16_t)sizeof(record);
    assert(!param_app_restore_channel(0U, &record));

    puts("test_param_app: PASS");
    return 0;
}
