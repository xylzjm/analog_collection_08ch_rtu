#ifndef ANALOG_INPUT_BOARD_H
#define ANALOG_INPUT_BOARD_H

#include <stdint.h>

#include "analog_input.h"
#include "board_config.h"
#include "sig24131.h"
#include "sig24131_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 本板的模拟输入通道数量（每个 SIG24131 对应一个通道）。 */
#define ANALOG_INPUT_BOARD_CHANNEL_COUNT SIG24131_BOARD_INSTANCE_COUNT

/** @brief 4-20 mA 输入的取样电阻。 */
#define ANALOG_INPUT_BOARD_SHUNT_OHMS 200.0f

/** @brief 电压前端的输入到 ADC 衰减比。 */
#define ANALOG_INPUT_BOARD_ATTENUATION_RATIO 5.0f

/** @brief SIG24131 内部参考电压。 */
#define ANALOG_INPUT_BOARD_INTERNAL_REFERENCE_VOLTS 2.5f

/** @brief 比例式 PT100 输入的参考电阻。 */
#define ANALOG_INPUT_BOARD_RTD_REFERENCE_OHMS 1650.0f

/** @brief 热电偶输入的 ADC 增益。 */
#define ANALOG_INPUT_BOARD_THERMOCOUPLE_GAIN SIG24131_GAIN_32

/** @brief 4-20 mA 输入的 ADC 增益。 */
#define ANALOG_INPUT_BOARD_CURRENT_GAIN SIG24131_GAIN_2

/** @brief 0-10 V 输入的 ADC 增益。 */
#define ANALOG_INPUT_BOARD_VOLTAGE_GAIN SIG24131_GAIN_1

/** @brief PT100 输入的 ADC 增益。 */
#define ANALOG_INPUT_BOARD_RTD_GAIN SIG24131_GAIN_8

/** @brief PT100 允许的温度范围。 */
#define ANALOG_INPUT_BOARD_RTD_MIN_TEMPERATURE_C -200.0f
#define ANALOG_INPUT_BOARD_RTD_MAX_TEMPERATURE_C 600.0f

/** @brief 热电偶与 PT100 共用的差分对。 */
#define ANALOG_INPUT_BOARD_SENSOR_MUX_P SIG24131_MUX_AIN5
#define ANALOG_INPUT_BOARD_SENSOR_MUX_N SIG24131_MUX_AIN4

/** @brief 4-20 mA 与 0-10 V 使用的单端对。 */
#define ANALOG_INPUT_BOARD_LINEAR_MUX_P SIG24131_MUX_AIN0
#define ANALOG_INPUT_BOARD_LINEAR_MUX_N SIG24131_MUX_AVSS

/** @brief PT100 激励电流的路由。 */
#define ANALOG_INPUT_BOARD_RTD_IDAC1_MUX SIG24131_IDAC_MUX_AIN3
#define ANALOG_INPUT_BOARD_RTD_IDAC2_MUX SIG24131_IDAC_MUX_AIN1

/** @brief PT100 激励电流。 */
#define ANALOG_INPUT_BOARD_RTD_IDAC_CURRENT SIG24131_IDAC_500_UA

/**
 * @brief 本板支持的采样率，单位毫赫兹。
 *
 * 列表止于 ANALOG_INPUT_BOARD_MAX_SAMPLE_RATE_MILLIHZ；高于它的
 * SIG24131 数据率会被 analog_input_configure() 拒绝。
 */
#define ANALOG_INPUT_BOARD_RATE_2_5_HZ  2500U
#define ANALOG_INPUT_BOARD_RATE_5_HZ    5000U
#define ANALOG_INPUT_BOARD_RATE_10_HZ   10000U
#define ANALOG_INPUT_BOARD_RATE_12_5_HZ 12500U
#define ANALOG_INPUT_BOARD_RATE_15_HZ   15000U
#define ANALOG_INPUT_BOARD_RATE_20_HZ   20000U
#define ANALOG_INPUT_BOARD_RATE_25_HZ   25000U
#define ANALOG_INPUT_BOARD_RATE_30_HZ   30000U
#define ANALOG_INPUT_BOARD_RATE_40_HZ   40000U
#define ANALOG_INPUT_BOARD_RATE_50_HZ   50000U
#define ANALOG_INPUT_BOARD_RATE_60_HZ   60000U
#define ANALOG_INPUT_BOARD_RATE_80_HZ   80000U
#define ANALOG_INPUT_BOARD_RATE_100_HZ  100000U

/**
 * @brief 本板可接受的最高采样率，单位毫赫兹。
 *
 * SIG24131 本身最高可运行在 9600 Hz，但本板仅按 100 Hz 规格设计：
 * 前端开关、8 通道轮询与遥测路径都以该上限为前提。
 */
#define ANALOG_INPUT_BOARD_MAX_SAMPLE_RATE_MILLIHZ ANALOG_INPUT_BOARD_RATE_100_HZ

/** @brief 调用方传 0 时采用的速率。 */
#define ANALOG_INPUT_BOARD_DEFAULT_SAMPLE_RATE_MILLIHZ ANALOG_INPUT_BOARD_RATE_10_HZ

/** @brief 配置变更后所需的转换次数。 */
#define ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS 2U

/** @brief 热电偶开路幅值阈值。 */
#define ANALOG_INPUT_BOARD_BREAK_CODE_THRESHOLD 0x600000L

/** @brief PT100 参考电阻完好时的电压。 */
#define ANALOG_INPUT_BOARD_RTD_REFERENCE_VOLTS 1.65f

/** @brief 低于该比例时，PT100 激励线判为断开。 */
#define ANALOG_INPUT_BOARD_RTD_BREAK_RATIO 0.75f

/** @brief 低于该比例时，PT100 回流线判为断开。 */
#define ANALOG_INPUT_BOARD_RTD_BREAK_RETURN_RATIO 0.25f

/** @brief 绑定到一个 SIG24131 通道的模拟输入对象。 */
typedef struct {
    analog_input_t base; /**< 接口基对象；必须位于首位。 */
    sig24131_t* device;  /**< 借用的 SIG24131 驱动实例。 */
    uint8_t channel;     /**< 从 0 开始的板级通道号。 */
} analog_input_board_t;

/**
 * @brief 构造一个板级通道的模拟输入对象。
 *
 * @param channel 从 0 开始的板级通道号。
 * @return ANALOG_INPUT_OK；通道无效时返回 ANALOG_INPUT_ERR_ARG。
 * @note 仅完成构造；使用前 analog_input_configure() 必须成功。
 */
analog_input_status_t analog_input_board_ctor(uint8_t channel);

/**
 * @brief 构造每个已装配通道的模拟输入对象。
 *
 * @return ANALOG_INPUT_OK，或首个构造函数失败的状态。
 */
analog_input_status_t analog_input_board_ctor_all(void);

/**
 * @brief 返回某个通道的接口对象。
 *
 * @param channel 从 0 开始的板级通道号。
 * @return 通道对象；通道无效时返回 NULL。
 */
analog_input_t* analog_input_board_get(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif
