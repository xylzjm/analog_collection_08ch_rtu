#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "analog_input_board.h"
#include "cal_data.h"

static void test_record_sanitize(void)
{
    cal_ch_record_t record;

    assert(cal_data_record_sanitize(NULL) == false);

    memset(&record, 0, sizeof(record));
    record.version = CAL_DATA_VERSION;
    record.size    = (uint16_t)sizeof(record);
    record.valid_mask = (uint8_t)((1U << CAL_DATA_TYPE_THERMOCOUPLE)
                                  | (1U << CAL_DATA_TYPE_PT100));
    record.entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro = 1000000L;
    record.entry[CAL_DATA_TYPE_PT100].gain_micro = 0L;

    assert(cal_data_record_sanitize(&record));
    assert((record.valid_mask
            & (uint8_t)(1U << CAL_DATA_TYPE_THERMOCOUPLE)) != 0U);
    assert((record.valid_mask
            & (uint8_t)(1U << CAL_DATA_TYPE_PT100)) == 0U);

    record.version = 99U;
    assert(cal_data_record_sanitize(&record) == false);
    record.version = CAL_DATA_VERSION;

    record.size = 0U;
    assert(cal_data_record_sanitize(&record) == false);
    record.size = (uint16_t)sizeof(record);

    record.valid_mask = 0x80U;
    assert(cal_data_record_sanitize(&record) == false);

    record.valid_mask = (uint8_t)(1U << CAL_DATA_TYPE_THERMOCOUPLE);
    record.entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro = CAL_GAIN_MICRO_MIN;
    record.entry[CAL_DATA_TYPE_THERMOCOUPLE].offset_code = 0L;
    assert(cal_data_record_sanitize(&record));
    assert(record.valid_mask != 0U);

    record.entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro = CAL_GAIN_MICRO_MIN - 1L;
    assert(cal_data_record_sanitize(&record));
    assert(record.valid_mask == 0U);

    record.valid_mask = (uint8_t)(1U << CAL_DATA_TYPE_THERMOCOUPLE);
    record.entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro = CAL_GAIN_MICRO_MAX;
    assert(cal_data_record_sanitize(&record));
    assert(record.valid_mask != 0U);

    record.entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro = CAL_GAIN_MICRO_MAX + 1L;
    assert(cal_data_record_sanitize(&record));
    assert(record.valid_mask == 0U);

    record.valid_mask = (uint8_t)(1U << CAL_DATA_TYPE_THERMOCOUPLE);
    record.entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro = 1000000L;
    record.entry[CAL_DATA_TYPE_THERMOCOUPLE].offset_code = CAL_OFFSET_CODE_LIMIT;
    assert(cal_data_record_sanitize(&record));
    assert(record.valid_mask != 0U);

    record.entry[CAL_DATA_TYPE_THERMOCOUPLE].offset_code =
        CAL_OFFSET_CODE_LIMIT + 1L;
    assert(cal_data_record_sanitize(&record));
    assert(record.valid_mask == 0U);

    record.valid_mask = (uint8_t)(1U << CAL_DATA_TYPE_THERMOCOUPLE);
    record.entry[CAL_DATA_TYPE_THERMOCOUPLE].offset_code = -CAL_OFFSET_CODE_LIMIT;
    assert(cal_data_record_sanitize(&record));
    assert(record.valid_mask != 0U);

    record.entry[CAL_DATA_TYPE_THERMOCOUPLE].offset_code =
        -CAL_OFFSET_CODE_LIMIT - 1L;
    assert(cal_data_record_sanitize(&record));
    assert(record.valid_mask == 0U);
}

int main(void)
{
    cal_ch_record_t record;
    cal_ch_record_t const* stored;
    uint8_t channel;
    uint8_t type;

    stub_factory_cal_reset();
    cal_data_init();
    test_record_sanitize();

    stored = cal_data_get(0U);
    assert(stored != NULL);
    assert(stored->valid_mask == 0U);
    for ( type = 0U; type < CAL_DATA_TYPE_COUNT; ++type ) {
        assert(stored->entry[type].gain_micro == 1000000L);
        assert(stored->entry[type].offset_code == 0L);
    }
    assert(cal_data_get(8U) == NULL);
    assert(cal_data_valid_mask(8U) == 0U);

    memset(&record, 0, sizeof(record));
    record.version = CAL_DATA_VERSION;
    record.size    = (uint16_t)sizeof(record);
    record.valid_mask = (uint8_t)((1U << CAL_DATA_TYPE_THERMOCOUPLE)
                                  | (1U << CAL_DATA_TYPE_PT100));
    record.entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro = 1001000L;
    record.entry[CAL_DATA_TYPE_THERMOCOUPLE].offset_code = -1234L;
    assert(cal_data_set(2U, &record));
    assert(cal_data_valid_mask(2U) == record.valid_mask);

    cal_data_apply_all();
    channel = 2U;
    assert((stub_factory_cal_mask
            & (1UL << ((uint32_t)channel * 4U + CAL_DATA_TYPE_THERMOCOUPLE)))
           != 0U);
    assert((stub_factory_cal_mask
            & (1UL << ((uint32_t)channel * 4U + CAL_DATA_TYPE_PT100)))
           != 0U);
    assert((stub_factory_cal_mask
            & (1UL << ((uint32_t)channel * 4U + CAL_DATA_TYPE_4_20MA)))
           == 0U);

    record.valid_mask = 0U;
    assert(cal_data_set(2U, &record));
    cal_data_apply_channel(2U);
    assert((stub_factory_cal_mask
            & (1UL << ((uint32_t)channel * 4U + CAL_DATA_TYPE_THERMOCOUPLE)))
           == 0U);

    assert(!cal_data_set(8U, &record));
    assert(cal_data_type_from_input(ANALOG_INPUT_TYPE_PT100)
           == CAL_DATA_TYPE_PT100);
    assert(cal_data_type_from_input(ANALOG_INPUT_TYPE_4_20MA)
           == CAL_DATA_TYPE_4_20MA);
    assert(cal_data_type_from_input(ANALOG_INPUT_TYPE_0_10V)
           == CAL_DATA_TYPE_0_10V);
    assert(cal_data_type_from_input(ANALOG_INPUT_TYPE_THERMOCOUPLE)
           == CAL_DATA_TYPE_THERMOCOUPLE);

    puts("test_cal_data: PASS");
    return 0;
}
