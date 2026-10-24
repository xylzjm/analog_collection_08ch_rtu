#ifndef ANALOG_INPUT_BOARD_H
#define ANALOG_INPUT_BOARD_H

/**
 * @file analog_input_board.h
 * @brief AT32F421 board implementation of the analog_input interface on SIG24131.
 *
 * The board layer turns a SIG24131 ADC instance into an analog_input object.
 * It owns everything that is hardware specific: the SIG24131 register setup per
 * input type, the front-end switches (burden resistor for 4-20 mA, common lead
 * for three-wire PT100), the raw-code to engineering-unit conversion and the
 * wire-break work mode.
 *
 * Layering:
 *
 *     application / Modbus
 *              |
 *     analog_input.h  (library/interface, hardware independent)
 *              |
 *     analog_input_board.h/.c  (this file, board specific)
 *              |
 *     sig24131_board.h/.c -> sig24131.h/.c  (device driver, MCU independent)
 *
 * Every parameter that depends on the schematic is a macro below and can be
 * overridden from the build configuration (for example defineList in eide.yml).
 */

#include <stdint.h>

#include "analog_input.h"
#include "sig24131.h"
#include "sig24131_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Front-end parameters ---------------------------------------------------- */

/**
 * @name Signal chain scaling
 * Values defined by the schematic. They must match it, otherwise the
 * engineering values are wrong even though the ADC codes are correct.
 * @{
 */

/** Burden resistor that converts the 4-20 mA loop current into a voltage. */
#ifndef ANALOG_INPUT_BOARD_SHUNT_OHMS
#define ANALOG_INPUT_BOARD_SHUNT_OHMS 200.0f
#endif

/**
 * Attenuation between the input terminals and the ADC, as input volts per ADC
 * volt. Both voltage front ends share the same 5:1 divider:
 *
 *     0-10 V  input: 10 V             -> 2.0 V at the ADC
 *     4-20 mA input: 20 mA * 200 ohm  -> 4.0 V -> 0.8 V at the ADC
 *
 * The ADC therefore sees V_adc = V_in / 5, and convert() has to multiply by
 * this ratio to recover the voltage at the input terminal.
 */
#ifndef ANALOG_INPUT_BOARD_ATTENUATION_RATIO
#define ANALOG_INPUT_BOARD_ATTENUATION_RATIO 5.0f
#endif

/**
 * SIG24131 internal reference in volts, used by the thermocouple, 4-20 mA and
 * 0-10 V modes.
 *
 * Note: the driver enum calls this source SIG24131_REF_INTERNAL_2V5, so the
 * value used here is 2.5 V. Change it together with the enum if the populated
 * device really provides a different reference.
 */
#ifndef ANALOG_INPUT_BOARD_INTERNAL_REFERENCE_VOLTS
#define ANALOG_INPUT_BOARD_INTERNAL_REFERENCE_VOLTS 2.5f
#endif

/**
 * Reference resistor of the ratiometric PT100 front end.
 *
 * The resistor sits on the low side of a three-wire sensor and carries both
 * matched excitation currents, so
 *
 *     V_ref = 2 * I_idac * R_reference
 *     V_rtd = I_idac * R_pt100
 *
 * and the excitation current cancels in the ratio:
 *
 *     R_pt100 = code / 2^23 * (2 * R_reference) / gain
 *
 * With 1.65 kohm and 500 uA the reference is 1.65 V, which keeps the IDAC
 * output in regulation over the whole -200..+850 degC range.
 */
#ifndef ANALOG_INPUT_BOARD_RTD_REFERENCE_OHMS
#define ANALOG_INPUT_BOARD_RTD_REFERENCE_OHMS 1650.0f
#endif

/** @} */

/**
 * @name ADC setup per input type
 * Gain values are SIG24131_GAIN_x enumerators; the numeric value is log2 of the
 * gain, so gain 32 is SIG24131_GAIN_32. Every gain below is chosen so that the
 * largest signal of that mode stays inside the converter range.
 * @{
 */

/**
 * Thermocouple: 2.5 V / 32 = +-78.1 mV full scale. Type E reaches 76.4 mV at
 * 1000 degC, the largest of the eight models.
 */
#ifndef ANALOG_INPUT_BOARD_THERMOCOUPLE_GAIN
#define ANALOG_INPUT_BOARD_THERMOCOUPLE_GAIN SIG24131_GAIN_32
#endif

/**
 * 4-20 mA: 0.8 V at the ADC for 20 mA. Gain 2 maps that to 1.6 V, 64% of the
 * +-2.5 V range, and still measures up to 31.25 mA before clipping, so the
 * usual 20 mA + 20% over-range fits comfortably.
 */
#ifndef ANALOG_INPUT_BOARD_CURRENT_GAIN
#define ANALOG_INPUT_BOARD_CURRENT_GAIN SIG24131_GAIN_2
#endif

/** 0-10 V: 2.0 V at the ADC, which is 80% of the +-2.5 V range. */
#ifndef ANALOG_INPUT_BOARD_VOLTAGE_GAIN
#define ANALOG_INPUT_BOARD_VOLTAGE_GAIN SIG24131_GAIN_1
#endif

/**
 * PT100: the specified range ends at 600 degC, where the sensor drops 156.9 mV,
 * so gain 8 gives 1.25 V against the 1.65 V reference, 76% of full scale. Gain
 * 16 would clip above 284 degC, and gain 8 still covers the full 850 degC of the
 * sensor at 94.7%, so 8 is the only setting that spans both.
 */
#ifndef ANALOG_INPUT_BOARD_RTD_GAIN
#define ANALOG_INPUT_BOARD_RTD_GAIN SIG24131_GAIN_8
#endif

/** @} */

/**
 * @name Analog input routing
 * The thermocouple and the PT100 share one differential pair. The 4-20 mA and
 * 0-10 V inputs are single ended on AIN0 and are measured against AVSS.
 * @{
 */
#ifndef ANALOG_INPUT_BOARD_SENSOR_MUX_P
#define ANALOG_INPUT_BOARD_SENSOR_MUX_P SIG24131_MUX_AIN5
#endif

#ifndef ANALOG_INPUT_BOARD_SENSOR_MUX_N
#define ANALOG_INPUT_BOARD_SENSOR_MUX_N SIG24131_MUX_AIN4
#endif

#ifndef ANALOG_INPUT_BOARD_LINEAR_MUX_P
#define ANALOG_INPUT_BOARD_LINEAR_MUX_P SIG24131_MUX_AIN0
#endif

#ifndef ANALOG_INPUT_BOARD_LINEAR_MUX_N
#define ANALOG_INPUT_BOARD_LINEAR_MUX_N SIG24131_MUX_AVSS
#endif

/** @} */

/**
 * @name PT100 excitation
 * Three-wire sensor, reference resistor on the low side, two matched IDACs
 * injected into AIN3 and AIN1.
 *
 * Both currents flow through the reference resistor, while each of the two
 * sensor leads carries one of them. Equal lead resistances therefore cancel and
 * the converter measures only I_idac * R_pt100.
 * @{
 */
#ifndef ANALOG_INPUT_BOARD_RTD_IDAC1_MUX
#define ANALOG_INPUT_BOARD_RTD_IDAC1_MUX SIG24131_IDAC_MUX_AIN3
#endif

#ifndef ANALOG_INPUT_BOARD_RTD_IDAC2_MUX
#define ANALOG_INPUT_BOARD_RTD_IDAC2_MUX SIG24131_IDAC_MUX_AIN1
#endif

/** Excitation current of the ratiometric PT100 measurement. */
#ifndef ANALOG_INPUT_BOARD_RTD_IDAC_CURRENT
#define ANALOG_INPUT_BOARD_RTD_IDAC_CURRENT SIG24131_IDAC_500_UA
#endif

/** @} */

/**
 * @name Timing and detection parameters
 * @{
 */

/** Sample rate selected when analog_input_config_t::sample_rate_millihz is 0. */
#ifndef ANALOG_INPUT_BOARD_DEFAULT_SAMPLE_RATE_MILLIHZ
#define ANALOG_INPUT_BOARD_DEFAULT_SAMPLE_RATE_MILLIHZ 10000U
#endif

/** Conversions needed after a configuration change before data is valid. */
#ifndef ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS
#define ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS 2U
#endif

/** Level that engages the front-end switches (CV and COM pins). */
#ifndef ANALOG_INPUT_BOARD_SWITCH_ENGAGED_LEVEL
#define ANALOG_INPUT_BOARD_SWITCH_ENGAGED_LEVEL 1U
#endif

/** Absolute code above which the input is considered open (burnout detection). */
#ifndef ANALOG_INPUT_BOARD_BREAK_CODE_THRESHOLD
#define ANALOG_INPUT_BOARD_BREAK_CODE_THRESHOLD 0x600000L
#endif

/** Time the burnout current needs to charge the input before a check. */
#ifndef ANALOG_INPUT_BOARD_BREAK_SETTLE_MS
#define ANALOG_INPUT_BOARD_BREAK_SETTLE_MS 200U
#endif

/** @} */

/* Concrete analog input object -------------------------------------------- */

/**
 * @brief Analog input object bound to one SIG24131 channel.
 *
 * The object keeps only what cannot be derived elsewhere: the interface base,
 * the ADC instance and the channel. Whether configure() has succeeded and
 * whether the wire-break mode is armed are read back from base.config and from
 * the SIG24131 configuration, so no duplicated flag can go stale.
 *
 * base must stay the first member: the analog_input helpers cast between
 * analog_input_t* and analog_input_board_t*.
 */
typedef struct {
    analog_input_t base; /**< Interface base object, must be first. */
    sig24131_t* device;  /**< Borrowed SIG24131 driver instance. */
    uint8_t channel;     /**< One-based channel, 0 when unmapped. */
} analog_input_board_t;

/* Board API --------------------------------------------------------------- */

/**
 * @brief Bind one SIG24131 driver instance to an analog input object.
 *
 * The driver instance must already be initialized with sig24131_init() (which
 * sig24131_board_init() does). The analog input starts in the unconfigured
 * state, so configure() must be called before any conversion.
 *
 * @param self    Storage owned by the caller.
 * @param device  Initialized SIG24131 driver instance, borrowed.
 * @param channel One-based board channel used for the front-end switches.
 *                Pass 0 for a stand-alone instance without board switches.
 */
analog_input_status_t analog_input_board_init(analog_input_board_t* self,
                                              sig24131_t* device,
                                              uint8_t channel);

/**
 * @brief Initialize the SIG24131 board and bind one input per channel.
 *
 * Calls sig24131_board_init() and then binds analog_input_board_get(1..count).
 * The front-end switches of every channel are left deasserted. A level of the
 * SPI1 peripheral and of the switch GPIOs is configured by the workbench
 * functions (wk_spi1_init(), wk_gpio_config()) before calling this function.
 */
analog_input_status_t analog_input_board_init_all(void);

/**
 * @brief Number of analog input channels populated on this board.
 *
 * Follows SIG24131_BOARD_INSTANCE_COUNT, so a four-device variant reports 4.
 */
uint8_t analog_input_board_count(void);

/**
 * @brief Get the analog input object of one channel. Channels are one-based.
 *
 * @return Pointer usable with analog_input_*(), or NULL when out of range.
 */
analog_input_t* analog_input_board_get(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif
