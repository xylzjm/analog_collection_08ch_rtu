#include "analog_input_board.h"

#include <string.h>

#define STUB_TC_IDEAL_LOW       0
#define STUB_TC_IDEAL_HIGH      5000000
#define STUB_PT100_IDEAL_LOW    2000000
#define STUB_PT100_IDEAL_HIGH   6000000
#define STUB_4_20_IDEAL_LOW     1000000
#define STUB_4_20_IDEAL_HIGH    5000000
#define STUB_0_10_IDEAL_LOW     0
#define STUB_0_10_IDEAL_HIGH    6000000

static analog_input_factory_cal_t applied[8][4];
static analog_input_t stub_input;
static int32_t stub_sample_code;
analog_input_config_t stub_analog_input_config;
uint32_t stub_factory_cal_mask;

static analog_input_status_t stub_configure(
    analog_input_t* self,
    const analog_input_config_t* config,
    uint32_t* first_conversion_delay_ms)
{
    (void)self;
    stub_analog_input_config = *config;
    *first_conversion_delay_ms = 10U;
    return ANALOG_INPUT_OK;
}

static analog_input_status_t stub_read_sample(analog_input_t* self,
                                              int32_t* sample_code)
{
    (void)self;
    *sample_code = stub_sample_code;
    return ANALOG_INPUT_OK;
}

static analog_input_status_t stub_convert(
    analog_input_t* self,
    int32_t sample_code,
    const analog_input_conversion_context_t* context,
    float* measurement)
{
    (void)self;
    (void)context;
    *measurement = (float)sample_code;
    return ANALOG_INPUT_OK;
}

static analog_input_status_t stub_enable_break(analog_input_t* self,
                                               uint32_t* delay_ms)
{
    (void)self;
    *delay_ms = 0U;
    return ANALOG_INPUT_OK;
}

static analog_input_status_t stub_disable_break(analog_input_t* self,
                                                uint32_t* delay_ms)
{
    (void)self;
    *delay_ms = 0U;
    return ANALOG_INPUT_OK;
}

static analog_input_status_t stub_check_break(
    analog_input_t* self,
    analog_input_wire_status_t* wire_status)
{
    (void)self;
    *wire_status = ANALOG_INPUT_WIRE_NORMAL;
    return ANALOG_INPUT_OK;
}

static const analog_input_ops_t stub_ops = {
    .configure               = stub_configure,
    .read_sample             = stub_read_sample,
    .convert                 = stub_convert,
    .enable_break_detection  = stub_enable_break,
    .disable_break_detection = stub_disable_break,
    .check_wire_break        = stub_check_break,
};

void stub_analog_input_set_sample(int32_t code)
{
    stub_sample_code = code;
}

void stub_analog_input_reset(void)
{
    stub_sample_code = 0L;
    memset(&stub_analog_input_config, 0, sizeof(stub_analog_input_config));
}

analog_input_t* analog_input_board_get(uint8_t channel)
{
    (void)channel;
    stub_input.ops = &stub_ops;
    return &stub_input;
}

analog_input_status_t analog_input_board_code_from_physical(
    analog_input_type_t type,
    float physical,
    int32_t* code)
{
    if ( code == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }

    switch ( type ) {
        case ANALOG_INPUT_TYPE_THERMOCOUPLE:
            *code = (physical <= 0.0f) ? STUB_TC_IDEAL_LOW
                                       : STUB_TC_IDEAL_HIGH;
            break;
        case ANALOG_INPUT_TYPE_PT100:
            *code = (physical <= 100.0f) ? STUB_PT100_IDEAL_LOW
                                         : STUB_PT100_IDEAL_HIGH;
            break;
        case ANALOG_INPUT_TYPE_4_20MA:
            *code = (physical <= 4.0f) ? STUB_4_20_IDEAL_LOW
                                       : STUB_4_20_IDEAL_HIGH;
            break;
        case ANALOG_INPUT_TYPE_0_10V:
            *code = (physical <= 0.0f) ? STUB_0_10_IDEAL_LOW
                                       : STUB_0_10_IDEAL_HIGH;
            break;
        default:
            return ANALOG_INPUT_ERR_ARG;
    }
    return ANALOG_INPUT_OK;
}

void stub_factory_cal_reset(void)
{
    uint8_t channel;
    uint8_t type;

    stub_factory_cal_mask = 0U;
    for ( channel = 0U; channel < 8U; ++channel ) {
        for ( type = 0U; type < 4U; ++type ) {
            applied[channel][type].gain_micro  = 0L;
            applied[channel][type].offset_code = 0L;
        }
    }
}

analog_input_status_t analog_input_board_set_factory_calibration(
    uint8_t channel,
    uint8_t cal_type,
    const analog_input_factory_cal_t* cal)
{
    uint32_t bit;

    if ( channel == 0U || channel > 8U || cal_type >= 4U ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    bit = (uint32_t)(channel - 1U) * 4U + cal_type;
    if ( cal != NULL ) {
        applied[channel - 1U][cal_type] = *cal;
        stub_factory_cal_mask |= (1UL << bit);
    } else {
        stub_factory_cal_mask &= ~(1UL << bit);
    }
    return ANALOG_INPUT_OK;
}
