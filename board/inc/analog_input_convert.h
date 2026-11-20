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
 * @brief 基于电气常量的纯板级转换公式，仅头文件实现，
 *        因而主机测试无需板级编译单元。
 *
 * 每个函数都是对板级电气常量与库中传感器数学的运算；此处不接触
 * 寄存器，主机测试因此可直接到达组合路径（绑定寄存器的 ops
 * 回调仍留在 analog_input_board.c 中）。
 */

/** @brief 有符号 24 位转换的满量程码值。 */
#define FULL_SCALE_CODE 8388608.0f

/** @brief 两种线性输入类型的电气量程。 */
#define CURRENT_SPAN_LOW_MA  4.0f
#define CURRENT_SPAN_HIGH_MA 20.0f
#define VOLTAGE_SPAN_LOW_V   0.0f
#define VOLTAGE_SPAN_HIGH_V  10.0f

/**
 * @brief 按给定的已编程增益把一个 ADC 码值转换为伏特。
 *
 * @param sample_code 有符号 24 位转换码值。
 * @param gain 已编程的 PGA 增益（1、2、8 或 32）。
 * @return 转换器输入端的电压（伏特）。
 */
static inline float analog_input_convert_adc_volts(int32_t sample_code,
                                                   float gain)
{
    return (float)sample_code
           * ANALOG_INPUT_BOARD_INTERNAL_REFERENCE_VOLTS
           / (FULL_SCALE_CODE * gain);
}

/**
 * @brief 把 4-20 mA 或 0-10 V 采样转换为工程量。
 *
 * @param type 输入类别；用于选择电气量程。
 * @param sample_code 有符号 24 位转换码值。
 * @param context 线性缩放上下限。
 * @param measurement 接收工程量结果。
 * @return ANALOG_INPUT_OK；context 为 NULL 时返回 ANALOG_INPUT_ERR_ARG。
 * @note 有意在量程外做外推，使超量程保持可见而不是被裁剪。
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

    /* 有意超出量程进行外推，使超量程保持可见。 */
    *measurement = context->linear_min
                   + (value - span_low)
                         * (context->linear_max - context->linear_min)
                         / (span_high - span_low);
    return ANALOG_INPUT_OK;
}

/**
 * @brief 把比例式 PT100 采样转换为摄氏度。
 *
 * @param sample_code 有符号 24 位转换码值。
 * @param measurement 接收温度。
 * @return ANALOG_INPUT_OK；空指针、非正电阻或温度超出本板允许的 PT100
 *         范围时，返回 ANALOG_INPUT_ERR_ARG / ERR_CONVERSION。
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
    /* 拒绝超范围的 PT100 值，而不是外推。 */
    if ( temperature < ANALOG_INPUT_BOARD_RTD_MIN_TEMPERATURE_C
         || temperature > ANALOG_INPUT_BOARD_RTD_MAX_TEMPERATURE_C ) {
        return ANALOG_INPUT_ERR_CONVERSION;
    }
    *measurement = temperature;
    return ANALOG_INPUT_OK;
}

/**
 * @brief 转换热电偶采样并做冷端补偿。
 *
 * @param tc_type 已配置通道的热电偶型号。
 * @param sample_code 有符号 24 位转换码值。
 * @param context 携带冷端温度。
 * @param measurement 接收温度。
 * @return 组合转换的 analog_input 状态。
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
 * @brief 把一个采样转换为经校准的工程量。
 *
 * 按输入类型分派，执行对应的纯公式，并应用来自 @p context 的每通道
 * 校准（增益/偏置）。
 *
 * @param type 输入类别。
 * @param tc_type 热电偶型号；除非 @p type 为 THERMOCOUPLE，否则忽略。
 * @param sample_code 有符号 24 位转换码值。
 * @param context 含校准信息的转换上下文。
 * @param measurement 接收校准后的工程量。
 * @return 公式状态；空指针或未知输入类型时返回 ANALOG_INPUT_ERR_ARG。
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
 * @brief 一次热电偶 burnout 检查的接线判定。
 *
 * @param sample_code 有符号 24 位转换码值。
 * @return 超出 burnout 阈值时为 ANALOG_INPUT_WIRE_BROKEN，否则为正常。
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
 * @brief 一次 PT100 激励线/回流线检查的接线判定。
 *
 * @param sample_code 有符号 24 位转换码值。
 * @return 依据参考电压比例返回 ANALOG_INPUT_WIRE_BROKEN_RETURN、
 *         ANALOG_INPUT_WIRE_BROKEN_LEAD 或 ANALOG_INPUT_WIRE_NORMAL。
 */
static inline analog_input_wire_status_t analog_input_convert_rtd_wire(
    int32_t sample_code)
{
    float gain;
    float volts;

    /* 参考电阻在单位增益下测量，与电压输入相同。 */
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
 * @brief 配置变更后的转换等待时间，单位毫秒。
 *
 * @param rate_millihz 有效采样率，单位毫赫兹。
 * @return ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS 次转换所需毫秒数，
 *         向上取整。
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
