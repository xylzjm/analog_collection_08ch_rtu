#include "param_app.h"

#include <string.h>

#include "board.h"

static param_sys_record_t sys_record;
static param_ch_record_t ch_records[PARAM_APP_CHANNEL_COUNT];
static uint16_t dirty_mask;
static uint32_t sys_revision;
static uint32_t ch_revision[PARAM_APP_CHANNEL_COUNT];

static void init_sys(param_sys_record_t* record)
{
    memset(record, 0, sizeof(*record));
    record->version = PARAM_APP_VERSION;
    record->size    = (uint16_t)sizeof(*record);
    record->address = PARAM_APP_DEFAULT_ADDRESS;
}

static void init_channel(param_ch_record_t* record)
{
    memset(record, 0, sizeof(*record));
    record->version    = PARAM_APP_VERSION;
    record->size       = (uint16_t)sizeof(*record);
    record->type       = PARAM_APP_DEFAULT_TYPE;
    record->rate_index = PARAM_APP_DEFAULT_RATE_INDEX;
    record->linear_min = 0L;
    record->linear_max = 0L;
    record->user_offset = 0L;
    record->user_gain   = PARAM_APP_DEFAULT_USER_GAIN;
}

static uint16_t key_bit(uint8_t key)
{
    return (uint16_t)(1U << key);
}

static uint32_t* key_revision(uint8_t key)
{
    if ( key == 0U ) {
        return &sys_revision;
    }
    return &ch_revision[key - 1U];
}

void param_app_init(void)
{
    uint8_t channel;

    board_critical_enter();
    init_sys(&sys_record);
    for ( channel = 0U; channel < PARAM_APP_CHANNEL_COUNT; ++channel ) {
        init_channel(&ch_records[channel]);
        ch_revision[channel] = 0U;
    }
    sys_revision = 0U;
    dirty_mask   = 0U;
    board_critical_exit();
}

uint16_t param_app_get_address(void)
{
    uint16_t address;

    board_critical_enter();
    address = sys_record.address;
    board_critical_exit();
    return address;
}

void param_app_get_sys(param_sys_record_t* record)
{
    if ( record == NULL ) {
        return;
    }
    board_critical_enter();
    *record = sys_record;
    board_critical_exit();
}

bool param_app_get_channel(uint8_t channel, param_ch_record_t* record)
{
    if ( channel >= PARAM_APP_CHANNEL_COUNT || record == NULL ) {
        return false;
    }
    board_critical_enter();
    *record = ch_records[channel];
    board_critical_exit();
    return true;
}

void param_app_set_sys(const param_sys_record_t* record)
{
    if ( record == NULL ) {
        return;
    }
    board_critical_enter();
    sys_record = *record;
    sys_record.version = PARAM_APP_VERSION;
    sys_record.size    = (uint16_t)sizeof(sys_record);
    ++sys_revision;
    dirty_mask |= key_bit(0U);
    board_critical_exit();
}

bool param_app_set_channel(uint8_t channel, const param_ch_record_t* record)
{
    if ( channel >= PARAM_APP_CHANNEL_COUNT || record == NULL ) {
        return false;
    }
    board_critical_enter();
    ch_records[channel] = *record;
    ch_records[channel].version = PARAM_APP_VERSION;
    ch_records[channel].size    = (uint16_t)sizeof(ch_records[channel]);
    ++ch_revision[channel];
    dirty_mask |= key_bit((uint8_t)(channel + 1U));
    board_critical_exit();
    return true;
}

bool param_app_restore_sys(const param_sys_record_t* record)
{
    if ( record == NULL || record->version != PARAM_APP_VERSION
         || record->size != (uint16_t)sizeof(*record) ) {
        return false;
    }
    board_critical_enter();
    sys_record       = *record;
    sys_revision     = 0U;
    dirty_mask      &= (uint16_t)~key_bit(0U);
    board_critical_exit();
    return true;
}

bool param_app_restore_channel(uint8_t channel, const param_ch_record_t* record)
{
    if ( channel >= PARAM_APP_CHANNEL_COUNT || record == NULL
         || record->version != PARAM_APP_VERSION
         || record->size != (uint16_t)sizeof(*record) ) {
        return false;
    }
    board_critical_enter();
    ch_records[channel]  = *record;
    ch_revision[channel] = 0U;
    dirty_mask          &= (uint16_t)~key_bit((uint8_t)(channel + 1U));
    board_critical_exit();
    return true;
}

uint16_t param_app_dirty_mask(void)
{
    uint16_t mask;

    board_critical_enter();
    mask = dirty_mask;
    board_critical_exit();
    return mask;
}

bool param_app_snapshot(uint8_t key, void* buf, uint16_t* len, uint32_t* rev)
{
    if ( buf == NULL || len == NULL || rev == NULL
         || key > PARAM_APP_CHANNEL_COUNT ) {
        return false;
    }

    board_critical_enter();
    if ( key == 0U ) {
        *(param_sys_record_t*)buf = sys_record;
        *len                      = sys_record.size;
        *rev                      = sys_revision;
    } else {
        *(param_ch_record_t*)buf = ch_records[key - 1U];
        *len                     = ch_records[key - 1U].size;
        *rev                     = ch_revision[key - 1U];
    }
    board_critical_exit();
    return true;
}

void param_app_clear_dirty(uint8_t key, uint32_t rev)
{
    if ( key > PARAM_APP_CHANNEL_COUNT ) {
        return;
    }

    board_critical_enter();
    if ( *key_revision(key) == rev ) {
        dirty_mask &= (uint16_t)~key_bit(key);
    }
    board_critical_exit();
}

bool param_app_linear_valid(int32_t min_value, int32_t max_value)
{
    return (min_value < max_value) ? true : false;
}

bool param_app_type_to_config(uint8_t type, analog_input_config_t* config)
{
    analog_input_thermocouple_type_t tc_type = ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE;
    analog_input_type_t input_type;

    if ( config == NULL ) {
        return false;
    }

    switch ( type ) {
        case 0U:
            input_type = ANALOG_INPUT_TYPE_4_20MA;
            break;
        case 1U:
            input_type = ANALOG_INPUT_TYPE_0_10V;
            break;
        case 2U:
            input_type = ANALOG_INPUT_TYPE_PT100;
            break;
        case 3U:
            input_type = ANALOG_INPUT_TYPE_THERMOCOUPLE;
            tc_type    = ANALOG_INPUT_THERMOCOUPLE_TYPE_B;
            break;
        case 4U:
            input_type = ANALOG_INPUT_TYPE_THERMOCOUPLE;
            tc_type    = ANALOG_INPUT_THERMOCOUPLE_TYPE_E;
            break;
        case 5U:
            input_type = ANALOG_INPUT_TYPE_THERMOCOUPLE;
            tc_type    = ANALOG_INPUT_THERMOCOUPLE_TYPE_J;
            break;
        case 6U:
            input_type = ANALOG_INPUT_TYPE_THERMOCOUPLE;
            tc_type    = ANALOG_INPUT_THERMOCOUPLE_TYPE_K;
            break;
        case 7U:
            input_type = ANALOG_INPUT_TYPE_THERMOCOUPLE;
            tc_type    = ANALOG_INPUT_THERMOCOUPLE_TYPE_N;
            break;
        case 8U:
            input_type = ANALOG_INPUT_TYPE_THERMOCOUPLE;
            tc_type    = ANALOG_INPUT_THERMOCOUPLE_TYPE_R;
            break;
        case 9U:
            input_type = ANALOG_INPUT_TYPE_THERMOCOUPLE;
            tc_type    = ANALOG_INPUT_THERMOCOUPLE_TYPE_S;
            break;
        case 10U:
            input_type = ANALOG_INPUT_TYPE_THERMOCOUPLE;
            tc_type    = ANALOG_INPUT_THERMOCOUPLE_TYPE_T;
            break;
        default:
            return false;
    }

    config->type                = input_type;
    config->thermocouple_type   = tc_type;
    config->sample_rate_millihz = 0U;
    return true;
}
