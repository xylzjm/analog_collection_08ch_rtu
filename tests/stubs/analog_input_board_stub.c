#include "analog_input_board.h"

static analog_input_factory_cal_t applied[8][4];
uint32_t stub_factory_cal_mask;

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
