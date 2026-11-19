/**
 * @file analog_input_convert.c
 * @brief Pure board conversion formulas over the electrical constants.
 *
 * Moved out of analog_input_board.c so the compose path (shunt, front-end
 * attenuation, ratiometric reference, cold junction compensation) runs
 * under the host tests without any register or vendor dependency.
 */

#include "analog_input_convert.h"

#include "analog_input_board.h"

/** @brief Full-scale code of a signed 24-bit conversion. */
#define FULL_SCALE_CODE 8388608.0f

/** @brief Electrical spans of the two linear input types. */
#define CURRENT_SPAN_LOW_MA  4.0f
#define CURRENT_SPAN_HIGH_MA 20.0f
#define VOLTAGE_SPAN_LOW_V   0.0f
#define VOLTAGE_SPAN_HIGH_V  10.0f

float analog_input_convert_adc_volts(int32_t sample_code,
                                     float gain)
{
    return (float)sample_code
           * ANALOG_INPUT_BOARD_INTERNAL_REFERENCE_VOLTS
           / (FULL_SCALE_CODE * gain);
}

analog_input_status_t analog_input_convert_linear(
    analog_input_type_t type,
    int32_t sample_code,
    const analog_input_conversion_context_t* context,
    float* measurement)
{
    float gain;
    float span_low;
    float span_high;
    float value;

    if ( context == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }

    if ( type == ANALOG_INPUT_TYPE_4_20MA ) {
        gain      = (float)(1UL << ANALOG_INPUT_BOARD_CURRENT_GAIN);
        value     = analog_input_convert_adc_volts(sample_code, gain)
                    * ANALOG_INPUT_BOARD_ATTENUATION_RATIO
                    / ANALOG_INPUT_BOARD_SHUNT_OHMS * 1000.0f;
        span_low  = CURRENT_SPAN_LOW_MA;
        span_high = CURRENT_SPAN_HIGH_MA;
    } else {
        gain      = (float)(1UL << ANALOG_INPUT_BOARD_VOLTAGE_GAIN);
        value     = analog_input_convert_adc_volts(sample_code, gain)
                    * ANALOG_INPUT_BOARD_ATTENUATION_RATIO;
        span_low  = VOLTAGE_SPAN_LOW_V;
        span_high = VOLTAGE_SPAN_HIGH_V;
    }

    /* Deliberately extrapolate outside the span so over-range stays visible. */
    *measurement = context->linear_min
                   + (value - span_low)
                         * (context->linear_max - context->linear_min)
                         / (span_high - span_low);
    return ANALOG_INPUT_OK;
}

analog_input_status_t analog_input_convert_pt100(int32_t sample_code,
                                                 float* measurement)
{
    analog_input_status_t status;
    float gain;
    float resistance;
    float temperature;

    if ( measurement == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }

    gain       = (float)(1UL << ANALOG_INPUT_BOARD_RTD_GAIN);
    resistance = (float)sample_code / FULL_SCALE_CODE
                 * (2.0f * ANALOG_INPUT_BOARD_RTD_REFERENCE_OHMS) / gain;

    if ( resistance <= 0.0f ) {
        return ANALOG_INPUT_ERR_CONVERSION;
    }

    status = analog_input_pt100_resistance_to_temperature(resistance, &temperature);
    if ( status != ANALOG_INPUT_OK ) {
        return status;
    }
    /* Reject out-of-range PT100 values instead of extrapolating. */
    if ( temperature < ANALOG_INPUT_BOARD_RTD_MIN_TEMPERATURE_C
         || temperature > ANALOG_INPUT_BOARD_RTD_MAX_TEMPERATURE_C ) {
        return ANALOG_INPUT_ERR_CONVERSION;
    }
    *measurement = temperature;
    return ANALOG_INPUT_OK;
}

analog_input_status_t analog_input_convert_thermocouple(
    analog_input_thermocouple_type_t tc_type,
    int32_t sample_code,
    const analog_input_conversion_context_t* context,
    float* measurement)
{
    analog_input_status_t status;
    float cold_junction_mv;
    float gain;
    float emf_mv;

    if ( context == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }

    gain   = (float)(1UL << ANALOG_INPUT_BOARD_THERMOCOUPLE_GAIN);
    emf_mv = analog_input_convert_adc_volts(sample_code, gain) * 1000.0f;

    status = analog_input_thermocouple_temperature_to_mv(tc_type,
                                                         context->cold_junction,
                                                         &cold_junction_mv);
    if ( status != ANALOG_INPUT_OK ) {
        return status;
    }

    return analog_input_thermocouple_mv_to_temperature(tc_type,
                                                       emf_mv + cold_junction_mv,
                                                       measurement);
}

analog_input_status_t analog_input_convert_sample(
    analog_input_type_t type,
    analog_input_thermocouple_type_t tc_type,
    int32_t sample_code,
    const analog_input_conversion_context_t* context,
    float* measurement)
{
    analog_input_status_t status;
    float value;

    if ( (context == NULL) || (measurement == NULL) ) {
        return ANALOG_INPUT_ERR_ARG;
    }

    switch ( type ) {
        case ANALOG_INPUT_TYPE_4_20MA:
        case ANALOG_INPUT_TYPE_0_10V:
            status = analog_input_convert_linear(type, sample_code, context,
                                                 &value);
            break;
        case ANALOG_INPUT_TYPE_PT100:
            status = analog_input_convert_pt100(sample_code, &value);
            break;
        case ANALOG_INPUT_TYPE_THERMOCOUPLE:
            status = analog_input_convert_thermocouple(tc_type, sample_code,
                                                       context, &value);
            break;
        default:
            return ANALOG_INPUT_ERR_ARG;
    }

    if ( status != ANALOG_INPUT_OK ) {
        return status;
    }

    *measurement = (value * context->gain) + context->offset;
    return ANALOG_INPUT_OK;
}

analog_input_wire_status_t analog_input_convert_thermocouple_wire(
    int32_t sample_code)
{
    if ( sample_code > ANALOG_INPUT_BOARD_BREAK_CODE_THRESHOLD
         || sample_code < -ANALOG_INPUT_BOARD_BREAK_CODE_THRESHOLD ) {
        return ANALOG_INPUT_WIRE_BROKEN;
    }
    return ANALOG_INPUT_WIRE_NORMAL;
}

analog_input_wire_status_t analog_input_convert_rtd_wire(
    int32_t sample_code)
{
    float gain;
    float volts;

    /* The reference resistor is measured at unity gain, like the voltage input. */
    gain  = (float)(1UL << SIG24131_GAIN_1);
    volts = analog_input_convert_adc_volts(sample_code, gain);

    if ( volts
         < ANALOG_INPUT_BOARD_RTD_REFERENCE_VOLTS
               * ANALOG_INPUT_BOARD_RTD_BREAK_RETURN_RATIO ) {
        return ANALOG_INPUT_WIRE_BROKEN_RETURN;
    }
    if ( volts
         < ANALOG_INPUT_BOARD_RTD_REFERENCE_VOLTS
               * ANALOG_INPUT_BOARD_RTD_BREAK_RATIO ) {
        return ANALOG_INPUT_WIRE_BROKEN_LEAD;
    }
    return ANALOG_INPUT_WIRE_NORMAL;
}

uint32_t analog_input_convert_settle_delay_ms(uint32_t rate_millihz)
{
    return ((uint32_t)ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS * 1000000U
            + rate_millihz - 1U)
           / rate_millihz;
}
