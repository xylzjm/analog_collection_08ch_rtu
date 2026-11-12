#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "analog_input_board.h"
#include "sig24131_stub.h"

static int32_t abs_i32(int32_t value)
{
    return (value < 0L) ? -value : value;
}

static void check_code(analog_input_type_t type,
                       float physical,
                       int32_t expected)
{
    int32_t code = 0;

    assert(analog_input_board_code_from_physical(type, physical, &code)
           == ANALOG_INPUT_OK);
    assert(abs_i32(code - expected) <= 3L);
}

static void check_near(float actual, float expected, float tolerance)
{
    assert(fabsf(actual - expected) <= tolerance);
}

static void test_code_from_physical(void)
{
    check_code(ANALOG_INPUT_TYPE_THERMOCOUPLE, 0.0f, 0L);
    check_code(ANALOG_INPUT_TYPE_THERMOCOUPLE, 50.0f, 5368709L);
    check_code(ANALOG_INPUT_TYPE_PT100, 100.0f, 2033601L);
    check_code(ANALOG_INPUT_TYPE_PT100, 300.0f, 6100805L);
    check_code(ANALOG_INPUT_TYPE_4_20MA, 4.0f, 1073741L);
    check_code(ANALOG_INPUT_TYPE_4_20MA, 20.0f, 5368709L);
    check_code(ANALOG_INPUT_TYPE_0_10V, 0.0f, 0L);
    check_code(ANALOG_INPUT_TYPE_0_10V, 10.0f, 6710886L);

    {
        int32_t code;
        assert(analog_input_board_code_from_physical(
                   ANALOG_INPUT_TYPE_0_10V, 1000.0f, &code)
               == ANALOG_INPUT_ERR_CONVERSION);
    }
}

static void test_linear_convert_and_factory_cal(void)
{
    analog_input_config_t config;
    analog_input_conversion_context_t context;
    analog_input_factory_cal_t cal;
    analog_input_t* input;
    uint32_t delay = 0U;
    int32_t raw;
    float measurement;

    assert(analog_input_board_ctor_all() == ANALOG_INPUT_OK);
    input = analog_input_board_get(1U);
    assert(input != NULL);
    assert(analog_input_board_get(0U) == NULL);
    assert(analog_input_board_get(9U) == NULL);

    memset(&config, 0, sizeof(config));
    config.type                = ANALOG_INPUT_TYPE_4_20MA;
    config.sample_rate_millihz = 10000U;
    assert(analog_input_configure(input, &config, &delay) == ANALOG_INPUT_OK);
    assert(delay > 0U);

    context.cold_junction = 0.0f;
    context.linear_min    = 0.0f;
    context.linear_max    = 100000.0f;

    stub_sig24131_sample = 1073741L;
    assert(analog_input_read_sample(input, &raw) == ANALOG_INPUT_OK);
    assert(analog_input_convert(input, raw, &context, &measurement)
           == ANALOG_INPUT_OK);
    check_near(measurement, 0.0f, 10.0f);

    stub_sig24131_sample = 5368709L;
    assert(analog_input_read_sample(input, &raw) == ANALOG_INPUT_OK);
    assert(analog_input_convert(input, raw, &context, &measurement)
           == ANALOG_INPUT_OK);
    check_near(measurement, 100000.0f, 100.0f);

    cal.gain_micro  = 2000000L;
    cal.offset_code = 0L;
    assert(analog_input_board_set_factory_calibration(1U, 2U, &cal)
           == ANALOG_INPUT_OK);

    stub_sig24131_sample = 1073741L;
    assert(analog_input_read_sample(input, &raw) == ANALOG_INPUT_OK);
    assert(analog_input_convert(input, raw, &context, &measurement)
           == ANALOG_INPUT_OK);
    check_near(measurement, 25000.0f, 100.0f);

    assert(analog_input_board_set_factory_calibration(1U, 2U, NULL)
           == ANALOG_INPUT_OK);
    assert(analog_input_convert(input, 1073741L, &context, &measurement)
           == ANALOG_INPUT_OK);
    check_near(measurement, 0.0f, 10.0f);
}

int main(void)
{
    stub_sig24131_reset();
    test_code_from_physical();
    test_linear_convert_and_factory_cal();

    puts("test_analog_input_board: PASS");
    return 0;
}
