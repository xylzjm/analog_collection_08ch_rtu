/**
 * @file test_analog_input_convert.c
 * @brief 板级纯转换合成路径的宿主测试。
 *
 * 每个期望值都由电气常数（2.5 V 基准、有符号 24 位码、5:1 衰减、200 ohm 分流、
 * 1.65 kohm 比例基准）和公布的 NIST 值手工推导而来，因此合成公式中的接线错误
 * ——这类缺陷在目标板上看起来像校准问题——会在这里失败。
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "analog_input_convert.h"

static int checks;
static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        ++checks;                                                  \
        if ( !(cond) ) {                                           \
            ++failures;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while ( 0 )

/*----------------------------------------------------------------------------*/
/* PT100 参考：0 C 以上温度的双精度 CVD 反解                                  */
/*----------------------------------------------------------------------------*/

static double cvd_temperature(double resistance_ohm)
{
    const double r0 = 100.0;
    const double a  = 3.9083e-3;
    const double b  = -5.775e-7;
    double ratio    = resistance_ohm / r0;

    return (-a + sqrt((a * a) - (4.0 * b * (1.0 - ratio)))) / (2.0 * b);
}

/*----------------------------------------------------------------------------*/
/* ADC 电压                                                                   */
/*----------------------------------------------------------------------------*/

static void test_adc_volts(void)
{
    /* 单位增益下的满量程就是 2.5 V 内部基准。 */
    CHECK(fabsf(analog_input_convert_adc_volts(8388608, 1.0f) - 2.5f)
          <= 0.00001f);
    CHECK(fabsf(analog_input_convert_adc_volts(4194304, 2.0f) - 0.625f)
          <= 0.00001f);
    CHECK(fabsf(analog_input_convert_adc_volts(-8388608, 1.0f) + 2.5f)
          <= 0.00001f);
}

/*----------------------------------------------------------------------------*/
/* 线性输入：4-20 mA 与 0-10 V                                                */
/*----------------------------------------------------------------------------*/

static void test_current_input(void)
{
    analog_input_conversion_context_t ctx;
    float value;

    ctx.linear_min    = 0.0f;
    ctx.linear_max    = 100.0f;
    ctx.cold_junction = 0.0f;
    ctx.gain          = 1.0f;
    ctx.offset        = 0.0f;

    /* 12 mA 流过 200 ohm 分流电阻：code = 12 / 62.5 * 2^24 / 2。 */
    CHECK(analog_input_convert_linear(ANALOG_INPUT_TYPE_4_20MA, 3221225,
                                      &ctx, &value)
          == ANALOG_INPUT_OK);
    CHECK(fabsf(value - 50.0f) <= 0.01f);

    /* 低于 4 mA 时有意向负方向外推：超量程保持可见。 */
    CHECK(analog_input_convert_linear(ANALOG_INPUT_TYPE_4_20MA, 0,
                                      &ctx, &value)
          == ANALOG_INPUT_OK);
    CHECK(fabsf(value - (-25.0f)) <= 0.01f);

    CHECK(analog_input_convert_linear(ANALOG_INPUT_TYPE_4_20MA, 0, NULL,
                                      &value)
          == ANALOG_INPUT_ERR_ARG);
}

static void test_voltage_input(void)
{
    analog_input_conversion_context_t ctx;
    float value;

    ctx.linear_min    = 0.0f;
    ctx.linear_max    = 100.0f;
    ctx.cold_junction = 0.0f;
    ctx.gain          = 1.0f;
    ctx.offset        = 0.0f;

    /* 端子处 5 V：code = 5 / 12.5 * 2^24。 */
    CHECK(analog_input_convert_linear(ANALOG_INPUT_TYPE_0_10V, 3355443,
                                      &ctx, &value)
          == ANALOG_INPUT_OK);
    CHECK(fabsf(value - 50.0f) <= 0.01f);

    ctx.linear_min = -100.0f;
    ctx.linear_max = 100.0f;
    /* 8 V 映射到量程的 80 % 点。 */
    CHECK(analog_input_convert_linear(ANALOG_INPUT_TYPE_0_10V, 5368709,
                                      &ctx, &value)
          == ANALOG_INPUT_OK);
    CHECK(fabsf(value - 60.0f) <= 0.01f);
}

/*----------------------------------------------------------------------------*/
/* PT100：比例电阻转摄氏度                                                    */
/*----------------------------------------------------------------------------*/

static void test_pt100_input(void)
{
    float value;

    /* 半量程：R = 0.5 * (2 * 1.65 kOhm / gain 8) = 206.25 ohm。 */
    CHECK(analog_input_convert_pt100(4194304, &value) == ANALOG_INPUT_OK);
    CHECK(fabsf(value - (float)cvd_temperature(206.25)) <= 0.05f);

    /* 零码值：非正电阻被拒绝，而不是外推。 */
    CHECK(analog_input_convert_pt100(0, &value) == ANALOG_INPUT_ERR_CONVERSION);
    CHECK(analog_input_convert_pt100(-9000000, &value)
          == ANALOG_INPUT_ERR_CONVERSION);

    /* 满量程读到 412.5 ohm（约 926 C），超出 600 C 的板级上限。 */
    CHECK(analog_input_convert_pt100(8388608, &value)
          == ANALOG_INPUT_ERR_CONVERSION);

    CHECK(analog_input_convert_pt100(4194304, NULL)
          == ANALOG_INPUT_ERR_ARG);
}

/*----------------------------------------------------------------------------*/
/* 热电偶：冷端合成                                                           */
/*----------------------------------------------------------------------------*/

static void test_thermocouple_input(void)
{
    analog_input_conversion_context_t ctx;
    float value;

    ctx.cold_junction = 25.0f;
    ctx.linear_min    = 0.0f;
    ctx.linear_max    = 0.0f;
    ctx.gain          = 1.0f;
    ctx.offset        = 0.0f;

    /*
     * 200 C 的 K 型热电偶结点、25 C 冷端测得的电动势为
     * E(200) - E(25) = 8.138 - 1.000 = 7.138 mV；码值由 2.5 V 经增益 32
     * 映射到有符号 24 位量程得出。
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
/* 派发与校准                                                                 */
/*----------------------------------------------------------------------------*/

static void test_sample_dispatch(void)
{
    analog_input_conversion_context_t ctx;
    float value;

    ctx.cold_junction = 25.0f;
    ctx.linear_min    = 0.0f;
    ctx.linear_max    = 100.0f;
    ctx.gain          = 1.0f;
    ctx.offset        = 0.0f;

    /* 每种类型都经由唯一的派发入口到达自己的公式。 */
    CHECK(analog_input_convert_sample(
              ANALOG_INPUT_TYPE_4_20MA, ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE,
              3221225, &ctx, &value)
          == ANALOG_INPUT_OK);
    CHECK(fabsf(value - 50.0f) <= 0.01f);

    CHECK(analog_input_convert_sample(
              ANALOG_INPUT_TYPE_0_10V, ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE,
              3355443, &ctx, &value)
          == ANALOG_INPUT_OK);
    CHECK(fabsf(value - 50.0f) <= 0.01f);

    CHECK(analog_input_convert_sample(
              ANALOG_INPUT_TYPE_THERMOCOUPLE, ANALOG_INPUT_THERMOCOUPLE_TYPE_K,
              766324, &ctx, &value)
          == ANALOG_INPUT_OK);
    CHECK(fabsf(value - 200.0f) <= 0.1f);

    CHECK(analog_input_convert_sample(
              ANALOG_INPUT_TYPE_PT100, ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE,
              4194304, &ctx, &value)
          == ANALOG_INPUT_OK);

    /* 校准在公式之后只施加一次。 */
    ctx.gain   = 2.0f;
    ctx.offset = 0.5f;
    CHECK(analog_input_convert_sample(
              ANALOG_INPUT_TYPE_4_20MA, ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE,
              3221225, &ctx, &value)
          == ANALOG_INPUT_OK);
    CHECK(fabsf(value - 100.5f) <= 0.02f);

    /* 非法参数在同一入口被拒绝。 */
    CHECK(analog_input_convert_sample(
              (analog_input_type_t)99, ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE,
              0, &ctx, &value)
          == ANALOG_INPUT_ERR_ARG);
    CHECK(analog_input_convert_sample(
              ANALOG_INPUT_TYPE_4_20MA, ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE,
              0, NULL, &value)
          == ANALOG_INPUT_ERR_ARG);
}

/*----------------------------------------------------------------------------*/
/* 断线判定                                                                   */
/*----------------------------------------------------------------------------*/

static void test_wire_verdicts(void)
{
    /* 热电偶：断路电流把码值推过阈值。 */
    CHECK(analog_input_convert_thermocouple_wire(0x600001)
          == ANALOG_INPUT_WIRE_BROKEN);
    CHECK(analog_input_convert_thermocouple_wire(-0x600001)
          == ANALOG_INPUT_WIRE_BROKEN);
    CHECK(analog_input_convert_thermocouple_wire(0x500000)
          == ANALOG_INPUT_WIRE_NORMAL);

    /*
     * 单位增益下的 PT100 基准比值：回线开路低于 1.65 V 基准的 0.25，
     * 引线开路低于 0.75。
     * 1,000,000 -> 0.298 V（回线），2,000,000 -> 0.596 V（引线），
     * 4,200,000 -> 1.252 V（正常）。
     */
    CHECK(analog_input_convert_rtd_wire(1000000)
          == ANALOG_INPUT_WIRE_BROKEN_RETURN);
    CHECK(analog_input_convert_rtd_wire(2000000)
          == ANALOG_INPUT_WIRE_BROKEN_LEAD);
    CHECK(analog_input_convert_rtd_wire(4200000)
          == ANALOG_INPUT_WIRE_NORMAL);
}

/*----------------------------------------------------------------------------*/
/* 稳定延时                                                                   */
/*----------------------------------------------------------------------------*/

static void test_settle_delay(void)
{
    /* 两次转换、向上取整：10 Hz -> 200 ms，2.5 Hz -> 800 ms。 */
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
    test_sample_dispatch();
    test_wire_verdicts();
    test_settle_delay();

    printf("test_analog_input_convert: %d checks, %d failures\n",
           checks, failures);
    return (failures == 0) ? 0 : 1;
}
