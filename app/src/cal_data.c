#include "cal_data.h"

#include <string.h>

#include "analog_input_board.h"

static cal_ch_record_t records[CAL_DATA_CHANNEL_COUNT];

static void init_record(cal_ch_record_t* record)
{
    uint8_t type;

    memset(record, 0, sizeof(*record));
    record->version = CAL_DATA_VERSION;
    record->size    = (uint16_t)sizeof(*record);
    for ( type = 0U; type < CAL_DATA_TYPE_COUNT; ++type ) {
        record->entry[type].gain_micro  = 1000000L;
        record->entry[type].offset_code = 0L;
    }
}

void cal_data_init(void)
{
    uint8_t channel;

    for ( channel = 0U; channel < CAL_DATA_CHANNEL_COUNT; ++channel ) {
        init_record(&records[channel]);
    }
}

const cal_ch_record_t* cal_data_get(uint8_t channel)
{
    if ( channel >= CAL_DATA_CHANNEL_COUNT ) {
        return NULL;
    }
    return &records[channel];
}

bool cal_data_set(uint8_t channel, const cal_ch_record_t* record)
{
    if ( channel >= CAL_DATA_CHANNEL_COUNT || record == NULL ) {
        return false;
    }
    records[channel] = *record;
    return true;
}

uint8_t cal_data_valid_mask(uint8_t channel)
{
    if ( channel >= CAL_DATA_CHANNEL_COUNT ) {
        return 0U;
    }
    return records[channel].valid_mask;
}

cal_data_type_t cal_data_type_from_input(analog_input_type_t type)
{
    switch ( type ) {
        case ANALOG_INPUT_TYPE_PT100:
            return CAL_DATA_TYPE_PT100;
        case ANALOG_INPUT_TYPE_4_20MA:
            return CAL_DATA_TYPE_4_20MA;
        case ANALOG_INPUT_TYPE_0_10V:
            return CAL_DATA_TYPE_0_10V;
        case ANALOG_INPUT_TYPE_THERMOCOUPLE:
            break;
    }
    return CAL_DATA_TYPE_THERMOCOUPLE;
}

void cal_data_apply_channel(uint8_t channel)
{
    analog_input_factory_cal_t cal;
    const cal_ch_record_t* record;
    uint8_t type;

    if ( channel >= CAL_DATA_CHANNEL_COUNT ) {
        return;
    }
    record = &records[channel];

    for ( type = 0U; type < CAL_DATA_TYPE_COUNT; ++type ) {
        if ( (record->valid_mask & (uint8_t)(1U << type)) != 0U ) {
            cal.gain_micro  = record->entry[type].gain_micro;
            cal.offset_code = record->entry[type].offset_code;
            (void)analog_input_board_set_factory_calibration(
                (uint8_t)(channel + 1U), type, &cal);
        } else {
            (void)analog_input_board_set_factory_calibration(
                (uint8_t)(channel + 1U), type, NULL);
        }
    }
}

void cal_data_apply_all(void)
{
    uint8_t channel;

    for ( channel = 0U; channel < CAL_DATA_CHANNEL_COUNT; ++channel ) {
        cal_data_apply_channel(channel);
    }
}
