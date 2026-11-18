/**
 * @file test_analog_input_convert.c
 * @brief Host tests of the board's pure conversion compose path.
 *
 * Every expectation is hand-derived from the electrical constants
 * (2.5 V reference, signed 24-bit codes, 5:1 attenuation, 200 ohm shunt,
 * 1.65 kohm ratiometric reference) and the published NIST values, so a
 * wiring mistake in the compose formulas - the class of defect that
 * looks like a calibration problem on target - fails here instead.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "analog_input_convert.h"

static int checks;
static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        ++checks;                                                        \
        if ( !(cond) ) {                                                \
            ++failures;                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                               \
    } while ( 0 )

/*----------------------------------------------------------------------------*/
/* PT100 oracle: double-precision CVD inverse for temperatures above 0 C      */
/*----------------------------------------------------------------------------*/

static double cvd_temperature(double resistance_ohm)
{
    const double r0 = 100.0;
    const double a  = 3.9083e-3;
    const double b  = -5.775e-7;
    double ratio = resistance_ohm / r0;

    return (-a + sqrt((a * a) - (4.0 * b * (1.0 - ratio)))) / (2.0 * b);
}

/*----------------------------------------------------------------------------*/
/* ADC volts                                                                  */
/*----------------------------------------------------------------------------*/

static void test_adc_volts(void)
{
    /* Full scale at unity gain is the 2.5 V internal reference. */
    CHECK(fabsf(analog_input_convert_adc_volts(8388608, 1.0f) - 2.5f)
          <= 0.00001f);
    CHECK(fabsf(analog_input_convert_adc_volts(4194304, 2.0f) - 0.625f)
          <= 0.00001f);
    CHECK(fabsf(analog_input_convert_adc_volts(-8388608, 1.0f) + 2.5f)
          <= 0.00001f);
}

/*----------------------------------------------------------------------------*/
/* Linear inputs: 4-20 mA and 0-10 V                                          */
/*----------------------------------------------------------------------------*/

static void test_current_input(void)
{
    analog_input_conversion_context_t ctx;
    float value;

    ctx.linear_min = 0.0f;
    ctx.linear_max = 100.0f;
    ctx.cold_junction = 0.0f;

    /* 12 mA through the 200 ohm shunt: code = 12 / 62.5 * 2^24 / 2. */
    CHECK(analog_input_convert_linear(ANALOG_INPUT_TYPE_4_20MA, 3221225,
                                      &ctx, &value) == ANALOG_INPUT_OK);
    CHECK(fabsf(value - 50.0f) <= 0.01f);

    /* Below 4 mA extrapolates negative on purpose: over-range stays visible. */
    CHECK(analog_input_convert_linear(ANALOG_INPUT_TYPE_4_20MA, 0,
                                      &ctx, &value) == ANALOG_INPUT_OK);
    CHECK(fabsf(value - (-25.0f)) <= 0.01f);

    CHECK(analog_input_convert_linear(ANALOG_INPUT_TYPE_4_20MA, 0, NULL,
                                      &value) == ANALOG_INPUT_ERR_ARG);
}

static void test_voltage_input(void)
{
    analog_input_conversion_context_t ctx;
    float value;

    ctx.linear_min = 0.0f;
    ctx.linear_max = 100.0f;
    ctx.cold_junction = 0.0f;

    /* 5 V at the terminal: code = 5 / 12.5 * 2^24. */
    CHECK(analog_input_convert_linear(ANALOG_INPUT_TYPE_0_10V, 3355443,
                                      &ctx, &value) == ANALOG_INPUT_OK);
    CHECK(fabsf(value - 50.0f) <= 0.01f);

    ctx.linear_min = -100.0f;
    ctx.linear_max = 100.0f;
    /* 8 V maps to the 80 percent point of the span. */
    CHECK(analog_input_convert_linear(ANALOG_INPUT_TYPE_0_10V, 5368709,
                                      &ctx, &value) == ANALOG_INPUT_OK);
    CHECK(fabsf(value - 60.0f) <= 0.01f);
}

/*----------------------------------------------------------------------------*/
/* PT100: ratiometric resistance to Celsius                                   */
/*----------------------------------------------------------------------------*/

static void test_pt100_input(void)
{
    float value;

    /* Half scale: R = 0.5 * (2 * 1.65 kOhm / gain 8) = 206.25 ohm. */
    CHECK(analog_input_convert_pt100(4194304, &value) == ANALOG_INPUT_OK);
    CHECK(fabsf(value - (float)cvd_temperature(206.25)) <= 0.05f);

    /* Zero code: non-positive resistance is rejected, not extrapolated. */
    CHECK(analog_input_convert_pt100(0, &value) == ANALOG_INPUT_ERR_CONVERSION);
    CHECK(analog_input_convert_pt100(-9000000, &value)
          == ANALOG_INPUT_ERR_CONVERSION);

    /* Full scale reads 412.5 ohm (about 926 C), beyond the 600 C board ceiling. */
    CHECK(analog_input_convert_pt100(8388608, &value)
          == ANALOG_INPUT_ERR_CONVERSION);

    CHECK(analog_input_convert_pt100(4194304, NULL)
          == ANALOG_INPUT_ERR_ARG);
}

/*----------------------------------------------------------------------------*/
/* Thermocouple: cold junction composition                                     */
/*----------------------------------------------------------------------------*/

static void test_thermocouple_input(void)
{
    analog_input_conversion_context_t ctx;
    float value;

    ctx.cold_junction = 25.0f;
    ctx.linear_min = 0.0f;
    ctx.linear_max = 0.0f;

    /*
     * A type K junction at 200 C over a 25 C cold end measures
     * E(200) - E(25) = 8.138 - 1.000 = 7.138 mV; the code follows from
     * 2.5 V over gain 32 across the signed 24-bit scale.
     */
    CHECK(analog_input_convert_thermocouple(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_K, 766324, &ctx, &value)
          == ANALOG_INPUT_OK);
    CHECK(fabsf(value - 200.0f) <= 0.1f);

    CHECK(analog_input_convert_thermocouple(
              ANALOG_INPUT_THERMOCOUPLE_TYPE_K, 766324, NULL, &value)
          == ANALOG_INPUT_ERR_ARG);
}

/*----------------------------------------------------------------------------*/
/* Wire verdicts                                                              */
/*----------------------------------------------------------------------------*/

static void test_wire_verdicts(void)
{
    /* Thermocouple: burnout current pushes the code beyond the threshold. */
    CHECK(analog_input_convert_thermocouple_wire(0x600001)
          == ANALOG_INPUT_WIRE_BROKEN);
    CHECK(analog_input_convert_thermocouple_wire(-0x600001)
          == ANALOG_INPUT_WIRE_BROKEN);
    CHECK(analog_input_convert_thermocouple_wire(0x500000)
          == ANALOG_INPUT_WIRE_NORMAL);

    /*
     * PT100 reference ratios at unity gain: return open below 0.25,
     * lead open below 0.75 of the 1.65 V reference.
     * 1,000,000 -> 0.298 V (return), 2,000,000 -> 0.596 V (lead),
     * 4,200,000 -> 1.252 V (normal).
     */
    CHECK(analog_input_convert_rtd_wire(1000000)
          == ANALOG_INPUT_WIRE_BROKEN_RETURN);
    CHECK(analog_input_convert_rtd_wire(2000000)
          == ANALOG_INPUT_WIRE_BROKEN_LEAD);
    CHECK(analog_input_convert_rtd_wire(4200000)
          == ANALOG_INPUT_WIRE_NORMAL);
}

/*----------------------------------------------------------------------------*/
/* Settle delay                                                               */
/*----------------------------------------------------------------------------*/

static void test_settle_delay(void)
{
    /* Two conversions, rounded up: 10 Hz -> 200 ms, 2.5 Hz -> 800 ms. */
    CHECK(analog_input_convert_settle_delay_ms(10000U) == 200U);
    CHECK(analog_input_convert_settle_delay_ms(2500U) == 800U);
    CHECK(analog_input_convert_settle_delay_ms(1U) == 2000000U);
}

/*----------------------------------------------------------------------------*/

int main(void)
{
    test_adc_volts();
    test_current_input();
    test_voltage_input();
    test_pt100_input();
    test_thermocouple_input();
    test_wire_verdicts();
    test_settle_delay();

    printf("test_analog_input_convert: %d checks, %d failures\n",
           checks, failures);
    return (failures == 0) ? 0 : 1;
}
