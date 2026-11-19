#ifndef ANALOG_INPUT_CONVERT_H
#define ANALOG_INPUT_CONVERT_H

#include <stdint.h>

#include "analog_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file analog_input_convert.h
 * @brief Pure board conversion formulas: ADC code to engineering value.
 *
 * Every function is a computation over the board's electrical constants
 * and the library sensor math; nothing here touches registers, so the
 * host tests reach the compose path directly (the register-bound ops
 * callbacks stay in analog_input_board.c).
 */

/**
 * @brief Convert one ADC code to volts at the given programmed gain.
 *
 * @param sample_code Signed 24-bit conversion code.
 * @param gain Programmed PGA gain (1, 2, 8 or 32).
 * @return Volts at the converter input.
 */
float analog_input_convert_adc_volts(int32_t sample_code,
                                     float gain);

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
analog_input_status_t analog_input_convert_linear(
    analog_input_type_t type,
    int32_t sample_code,
    const analog_input_conversion_context_t* context,
    float* measurement);

/**
 * @brief Convert a ratiometric PT100 sample to degrees Celsius.
 *
 * @param sample_code Signed 24-bit conversion code.
 * @param measurement Receives the temperature.
 * @return ANALOG_INPUT_OK, or ANALOG_INPUT_ERR_ARG / ERR_CONVERSION for a
 *         NULL pointer, a non-positive resistance or a temperature outside
 *         the board's accepted PT100 range.
 */
analog_input_status_t analog_input_convert_pt100(int32_t sample_code,
                                                 float* measurement);

/**
 * @brief Convert a thermocouple sample with cold junction compensation.
 *
 * @param tc_type Thermocouple model of the configured channel.
 * @param sample_code Signed 24-bit conversion code.
 * @param context Carries the cold junction temperature.
 * @param measurement Receives the temperature.
 * @return Analog_input status of the composed conversions.
 */
analog_input_status_t analog_input_convert_thermocouple(
    analog_input_thermocouple_type_t tc_type,
    int32_t sample_code,
    const analog_input_conversion_context_t* context,
    float* measurement);

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
analog_input_status_t analog_input_convert_sample(
    analog_input_type_t type,
    analog_input_thermocouple_type_t tc_type,
    int32_t sample_code,
    const analog_input_conversion_context_t* context,
    float* measurement);

/**
 * @brief Wire verdict of one thermocouple burnout check.
 *
 * @param sample_code Signed 24-bit conversion code.
 * @return ANALOG_INPUT_WIRE_BROKEN beyond the burnout threshold, normal
 *         otherwise.
 */
analog_input_wire_status_t analog_input_convert_thermocouple_wire(
    int32_t sample_code);

/**
 * @brief Wire verdict of one PT100 lead/return check.
 *
 * @param sample_code Signed 24-bit conversion code.
 * @return ANALOG_INPUT_WIRE_BROKEN_RETURN, ANALOG_INPUT_WIRE_BROKEN_LEAD
 *         or ANALOG_INPUT_WIRE_NORMAL by reference voltage ratio.
 */
analog_input_wire_status_t analog_input_convert_rtd_wire(
    int32_t sample_code);

/**
 * @brief Conversion wait after a configuration change, in milliseconds.
 *
 * @param rate_millihz Effective sample rate in millihertz.
 * @return Milliseconds for ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS
 *         conversions, rounded up.
 */
uint32_t analog_input_convert_settle_delay_ms(uint32_t rate_millihz);

#ifdef __cplusplus
}
#endif

#endif
