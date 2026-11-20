#ifndef ANALOG_INPUT_CONVERT_H
#define ANALOG_INPUT_CONVERT_H

#include <stdint.h>

#include "analog_input.h"
#include "analog_input_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file analog_input_convert.h
 * @brief Pure board conversion formulas over the electrical constants,
 *        header-only so the host test needs no board translation unit.
 *
 * Every function is a computation over the board's electrical constants
 * and the library sensor math; nothing here touches registers, so the
 * host tests reach the compose path directly (the register-bound ops
 * callbacks stay in analog_input_board.c).
 */

/** @brief Full-scale code of a signed 24-bit conversion. */
#define FULL_SCALE_CODE 8388608.0f

/** @brief Electrical spans of the two linear input types. */
#define CURRENT_SPAN_LOW_MA  4.0f
#define CURRENT_SPAN_HIGH_MA 20.0f
#define VOLTAGE_SPAN_LOW_V   0.0f
#define VOLTAGE_SPAN_HIGH_V  10.0f

/**
 * @brief Convert one ADC code to volts at the given programmed gain.
 *
 * @param sample_code Signed 24-bit conversion code.
 * @param gain Programmed PGA gain (1, 2, 8 or 32).
 * @return Volts at the converter input.
 */
static inline float analog_input_convert_adc_volts(int32_t sample_code,
                                                   float gain)
{
    return (float)sample_code
           * ANALOG_INPUT_BOARD_INTERNAL_REFERENCE_VOLTS
           / (FULL_SCALE_CODE * gain);
}

/**
 * @brief Convert a 4-20 mA or 0-10 V sample to an engineering value.
 *
 * @param type Input category; selects the electrical span.
 * @param sample_code Signed 24-bit conversion code.
 * @param context Linear scaling limits.
 * @param measurement Receives the engineering value.
 * @return ANALOG_INPUT_OK, or ANALOG_INPUT_ERR_ARG for a NULL context.
 * @note Deliberately extrapolates outside the span so over-range stays
 *       visible instead of being clipped.
 */
static inline analog_input_status_t analog_input_convert_linear(
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

/**
 * @brief Convert a ratiometric PT100 sample to degrees Celsius.
 *
 * @param sample_code Signed 24-bit conversion code.
 * @param measurement Receives the temperature.
 * @return ANALOG_INPUT_OK, or ANALOG_INPUT_ERR_ARG / ERR_CONVERSION for a
 *         NULL pointer, a non-positive resistance or a temperature outside
 *         the board's accepted PT100 range.
 */
static inline analog_input_status_t analog_input_convert_pt100(
    int32_t sample_code,
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

/**
 * @brief Convert a thermocouple sample with cold junction compensation.
 *
 * @param tc_type Thermocouple model of the configured channel.
 * @param sample_code Signed 24-bit conversion code.
 * @param context Carries the cold junction temperature.
 * @param measurement Receives the temperature.
 * @return Analog_input status of the composed conversions.
 */
static inline analog_input_status_t analog_input_convert_thermocouple(
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

/**
 * @brief Convert one sample to a calibrated engineering value.
 *
 * Dispatches on the input type, runs the matching pure formula and applies
 * the per-channel calibration (gain/offset) from @p context.
 *
 * @param type Input category.
 * @param tc_type Thermocouple model; ignored unless @p type is THERMOCOUPLE.
 * @param sample_code Signed 24-bit conversion code.
 * @param context Conversion context including calibration.
 * @param measurement Receives the calibrated engineering value.
 * @return The formula status, or ANALOG_INPUT_ERR_ARG for a NULL pointer or
 *         an unknown input type.
 */
static inline analog_input_status_t analog_input_convert_sample(
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

/**
 * @brief Wire verdict of one thermocouple burnout check.
 *
 * @param sample_code Signed 24-bit conversion code.
 * @return ANALOG_INPUT_WIRE_BROKEN beyond the burnout threshold, normal
 *         otherwise.
 */
static inline analog_input_wire_status_t analog_input_convert_thermocouple_wire(
    int32_t sample_code)
{
    if ( sample_code > ANALOG_INPUT_BOARD_BREAK_CODE_THRESHOLD
         || sample_code < -ANALOG_INPUT_BOARD_BREAK_CODE_THRESHOLD ) {
        return ANALOG_INPUT_WIRE_BROKEN;
    }
    return ANALOG_INPUT_WIRE_NORMAL;
}

/**
 * @brief Wire verdict of one PT100 lead/return check.
 *
 * @param sample_code Signed 24-bit conversion code.
 * @return ANALOG_INPUT_WIRE_BROKEN_RETURN, ANALOG_INPUT_WIRE_BROKEN_LEAD
 *         or ANALOG_INPUT_WIRE_NORMAL by reference voltage ratio.
 */
static inline analog_input_wire_status_t analog_input_convert_rtd_wire(
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

/**
 * @brief Conversion wait after a configuration change, in milliseconds.
 *
 * @param rate_millihz Effective sample rate in millihertz.
 * @return Milliseconds for ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS
 *         conversions, rounded up.
 */
static inline uint32_t analog_input_convert_settle_delay_ms(
    uint32_t rate_millihz)
{
    return ((uint32_t)ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS * 1000000U
            + rate_millihz - 1U)
           / rate_millihz;
}

#ifdef __cplusplus
}
#endif

#endif
