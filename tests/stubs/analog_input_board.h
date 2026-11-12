#ifndef ANALOG_INPUT_BOARD_H
#define ANALOG_INPUT_BOARD_H

#include <stdint.h>

#include "analog_input.h"

typedef struct {
    int32_t gain_micro;
    int32_t offset_code;
} analog_input_factory_cal_t;

analog_input_status_t analog_input_board_set_factory_calibration(
    uint8_t channel,
    uint8_t cal_type,
    const analog_input_factory_cal_t* cal);

/* Host-test observation: bit (channel - 1) * 4 + cal_type. */
extern uint32_t stub_factory_cal_mask;

void stub_factory_cal_reset(void);

#endif
