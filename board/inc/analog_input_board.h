#ifndef ANALOG_INPUT_BOARD_H
#define ANALOG_INPUT_BOARD_H

#include <stdint.h>

#include "analog_input.h"
#include "sig24131.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Burden resistor for the 4-20 mA input. */
#define ANALOG_INPUT_BOARD_SHUNT_OHMS 200.0f

/** @brief Input-to-ADC attenuation ratio of the voltage front ends. */
#define ANALOG_INPUT_BOARD_ATTENUATION_RATIO 5.0f

/** @brief Internal SIG24131 reference voltage. */
#define ANALOG_INPUT_BOARD_INTERNAL_REFERENCE_VOLTS 2.5f

/** @brief Reference resistor of the ratiometric PT100 input. */
#define ANALOG_INPUT_BOARD_RTD_REFERENCE_OHMS 1650.0f

/** @brief ADC gain for the thermocouple input. */
#define ANALOG_INPUT_BOARD_THERMOCOUPLE_GAIN SIG24131_GAIN_32

/** @brief ADC gain for the 4-20 mA input. */
#define ANALOG_INPUT_BOARD_CURRENT_GAIN SIG24131_GAIN_2

/** @brief ADC gain for the 0-10 V input. */
#define ANALOG_INPUT_BOARD_VOLTAGE_GAIN SIG24131_GAIN_1

/** @brief ADC gain for the PT100 input. */
#define ANALOG_INPUT_BOARD_RTD_GAIN SIG24131_GAIN_8

/** @brief Accepted PT100 temperature range. */
#define ANALOG_INPUT_BOARD_RTD_MIN_TEMPERATURE_C -200.0f
#define ANALOG_INPUT_BOARD_RTD_MAX_TEMPERATURE_C 600.0f

/** @brief Differential pair shared by thermocouple and PT100. */
#define ANALOG_INPUT_BOARD_SENSOR_MUX_P SIG24131_MUX_AIN5
#define ANALOG_INPUT_BOARD_SENSOR_MUX_N SIG24131_MUX_AIN4

/** @brief Single-ended pair used by 4-20 mA and 0-10 V. */
#define ANALOG_INPUT_BOARD_LINEAR_MUX_P SIG24131_MUX_AIN0
#define ANALOG_INPUT_BOARD_LINEAR_MUX_N SIG24131_MUX_AVSS

/** @brief PT100 excitation routing. */
#define ANALOG_INPUT_BOARD_RTD_IDAC1_MUX SIG24131_IDAC_MUX_AIN3
#define ANALOG_INPUT_BOARD_RTD_IDAC2_MUX SIG24131_IDAC_MUX_AIN1

/** @brief PT100 excitation current. */
#define ANALOG_INPUT_BOARD_RTD_IDAC_CURRENT SIG24131_IDAC_500_UA

/** @brief Supported SIG24131 data rates, in millihertz. */
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
#define ANALOG_INPUT_BOARD_RATE_120_HZ  120000U
#define ANALOG_INPUT_BOARD_RATE_160_HZ  160000U
#define ANALOG_INPUT_BOARD_RATE_200_HZ  200000U
#define ANALOG_INPUT_BOARD_RATE_240_HZ  240000U
#define ANALOG_INPUT_BOARD_RATE_250_HZ  250000U
#define ANALOG_INPUT_BOARD_RATE_300_HZ  300000U
#define ANALOG_INPUT_BOARD_RATE_400_HZ  400000U
#define ANALOG_INPUT_BOARD_RATE_480_HZ  480000U
#define ANALOG_INPUT_BOARD_RATE_500_HZ  500000U
#define ANALOG_INPUT_BOARD_RATE_600_HZ  600000U
#define ANALOG_INPUT_BOARD_RATE_800_HZ  800000U
#define ANALOG_INPUT_BOARD_RATE_1000_HZ 1000000U
#define ANALOG_INPUT_BOARD_RATE_1200_HZ 1200000U
#define ANALOG_INPUT_BOARD_RATE_2000_HZ 2000000U
#define ANALOG_INPUT_BOARD_RATE_2400_HZ 2400000U
#define ANALOG_INPUT_BOARD_RATE_4000_HZ 4000000U
#define ANALOG_INPUT_BOARD_RATE_4800_HZ 4800000U
#define ANALOG_INPUT_BOARD_RATE_8000_HZ 8000000U
#define ANALOG_INPUT_BOARD_RATE_9600_HZ 9600000U

/** @brief Rate used when the caller passes zero. */
#define ANALOG_INPUT_BOARD_DEFAULT_SAMPLE_RATE_MILLIHZ ANALOG_INPUT_BOARD_RATE_10_HZ

/** @brief Conversions required after a configuration change. */
#define ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS 2U

/** @brief Thermocouple open-circuit magnitude threshold. */
#define ANALOG_INPUT_BOARD_BREAK_CODE_THRESHOLD 0x600000L

/** @brief Intact PT100 reference-resistor voltage. */
#define ANALOG_INPUT_BOARD_RTD_REFERENCE_VOLTS 1.65f

/** @brief Ratio below which a PT100 excitation lead counts as open. */
#define ANALOG_INPUT_BOARD_RTD_BREAK_RATIO 0.75f

/** @brief Ratio below which the PT100 return lead counts as open. */
#define ANALOG_INPUT_BOARD_RTD_BREAK_RETURN_RATIO 0.25f

/** @brief Analog input object bound to one SIG24131 channel. */
typedef struct {
    analog_input_t base; /**< Interface base object; must be first. */
    sig24131_t* device;  /**< Borrowed SIG24131 driver instance. */
    uint8_t channel;     /**< One-based board channel. */
} analog_input_board_t;

/**
 * @brief Construct the analog input object of one board channel.
 *
 * @param channel One-based board channel.
 * @return ANALOG_INPUT_OK, or ANALOG_INPUT_ERR_ARG for an invalid channel.
 * @note Construction only; analog_input_configure() must succeed before use.
 */
analog_input_status_t analog_input_board_ctor(uint8_t channel);

/**
 * @brief Construct the analog input object of every populated channel.
 *
 * @return ANALOG_INPUT_OK, or the first constructor failure.
 */
analog_input_status_t analog_input_board_ctor_all(void);

/** @brief Return the number of channel objects owned by this module. */
uint8_t analog_input_board_count(void);

/**
 * @brief Return the interface object of one channel.
 *
 * @param channel One-based board channel.
 * @return The channel object, or NULL for an invalid channel.
 */
analog_input_t* analog_input_board_get(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif
