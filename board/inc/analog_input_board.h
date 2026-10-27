#ifndef ANALOG_INPUT_BOARD_H
#define ANALOG_INPUT_BOARD_H

/**
 * @file analog_input_board.h
 * @brief Board parameters of the SIG24131 analog input front end.
 *
 * Everything below is fixed by the schematic: how the four input types are
 * scaled, which converter gain keeps each of them inside its range, and how the
 * multiplexer and the two excitation currents are routed. The implementation
 * that uses these values lives in analog_input_board.c.
 *
 * These are fixed values rather than build-time options: a board variant with a
 * different shunt, divider or routing changes them in this file.
 */

#include <stdint.h>

#include "analog_input.h"
#include "sig24131.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Signal chain scaling ---------------------------------------------------- */

/** Burden resistor that converts the 4-20 mA loop current into a voltage. */
#define ANALOG_INPUT_BOARD_SHUNT_OHMS 200.0f

/**
 * Attenuation between the input terminals and the ADC, as input volts per ADC
 * volt. Both voltage front ends share the same 5:1 divider:
 *
 *     0-10 V  input: 10 V             -> 2.0 V at the ADC
 *     4-20 mA input: 20 mA * 200 ohm  -> 4.0 V -> 0.8 V at the ADC
 *
 * The ADC therefore sees V_adc = V_in / 5.
 */
#define ANALOG_INPUT_BOARD_ATTENUATION_RATIO 5.0f

/**
 * SIG24131 internal reference in volts, used by the thermocouple, 4-20 mA and
 * 0-10 V modes. The PT100 front end is ratiometric and takes its reference from
 * the voltage across the reference resistor instead.
 */
#define ANALOG_INPUT_BOARD_INTERNAL_REFERENCE_VOLTS 2.5f

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
 * output in regulation over the whole -200..+600 degC range.
 */
#define ANALOG_INPUT_BOARD_RTD_REFERENCE_OHMS 1650.0f

/* ADC gain per input type -------------------------------------------------- */

/**
 * Thermocouple: 2.5 V / 32 = +-78.1 mV full scale. Type E reaches 76.4 mV at
 * 1000 degC, the largest of the eight models.
 */
#define ANALOG_INPUT_BOARD_THERMOCOUPLE_GAIN SIG24131_GAIN_32

/**
 * 4-20 mA: 0.8 V at the ADC for 20 mA. Gain 2 maps that to 1.6 V, 64% of the
 * +-2.5 V range, and still measures up to 31.25 mA before clipping.
 */
#define ANALOG_INPUT_BOARD_CURRENT_GAIN SIG24131_GAIN_2

/** 0-10 V: 2.0 V at the ADC, 80% of the +-2.5 V range. */
#define ANALOG_INPUT_BOARD_VOLTAGE_GAIN SIG24131_GAIN_1

/**
 * PT100: the specified range ends at 600 degC, where the sensor drops 156.9 mV,
 * so gain 8 gives 1.26 V against the 1.65 V reference, 76% of full scale. Gain
 * 16 would clip above 284 degC, so 8 is the only setting that covers the range.
 */
#define ANALOG_INPUT_BOARD_RTD_GAIN SIG24131_GAIN_8

/**
 * Temperature range the PT100 input accepts. Outside it the reading is out of
 * specification, so convert() reports ANALOG_INPUT_ERR_CONVERSION instead of
 * handing out a value that looks valid.
 */
#define ANALOG_INPUT_BOARD_RTD_MIN_TEMPERATURE_C -200.0f
#define ANALOG_INPUT_BOARD_RTD_MAX_TEMPERATURE_C 600.0f

/* Analog input routing ----------------------------------------------------- */

/*
 * The thermocouple and the PT100 share one differential pair. The 4-20 mA and
 * 0-10 V inputs are single ended on AIN0 and are measured against AVSS.
 */

#define ANALOG_INPUT_BOARD_SENSOR_MUX_P SIG24131_MUX_AIN5

#define ANALOG_INPUT_BOARD_SENSOR_MUX_N SIG24131_MUX_AIN4

#define ANALOG_INPUT_BOARD_LINEAR_MUX_P SIG24131_MUX_AIN0

#define ANALOG_INPUT_BOARD_LINEAR_MUX_N SIG24131_MUX_AVSS

/* Front-end switches ------------------------------------------------------- */

/*
 * CV and COM are driven directly on the MCU pins, with the level each input
 * type needs:
 *
 *     type        COM  CV
 *     PT100        0    0
 *     0-10 V       1    0
 *     4-20 mA      1    1
 *     thermocouple 1    0
 *
 * COM must be low for a three-wire PT100 and high for the two single-ended
 * inputs; CV must be high only when the burden resistor is in circuit. The
 * thermocouple row uses the preferred level of the don't-care cells.
 */

/* PT100 excitation --------------------------------------------------------- */

/*
 * Three-wire sensor, reference resistor on the low side, two matched IDACs
 * injected into AIN3 and AIN1. Both currents flow through the reference
 * resistor, while each of the two sensor leads carries one of them, so equal
 * lead resistances cancel and the converter measures only I_idac * R_pt100.
 */

#define ANALOG_INPUT_BOARD_RTD_IDAC1_MUX SIG24131_IDAC_MUX_AIN3

#define ANALOG_INPUT_BOARD_RTD_IDAC2_MUX SIG24131_IDAC_MUX_AIN1

/** Excitation current of the ratiometric PT100 measurement. */
#define ANALOG_INPUT_BOARD_RTD_IDAC_CURRENT SIG24131_IDAC_500_UA

/* Supported sample rates --------------------------------------------------- */

/*
 * Data rates this board accepts, in millihertz. They are all the rates the
 * SIG24131 offers and can be assigned directly to
 * analog_input_config_t::sample_rate_millihz; configure() answers
 * ANALOG_INPUT_ERR_UNSUPPORTED for any other value, and zero keeps asking for
 * the default rate.
 *
 *     10 Hz example:  config.sample_rate_millihz = ANALOG_INPUT_BOARD_RATE_10_HZ;
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

/* Timing and detection ----------------------------------------------------- */

/** Sample rate selected when analog_input_config_t::sample_rate_millihz is 0. */
#define ANALOG_INPUT_BOARD_DEFAULT_SAMPLE_RATE_MILLIHZ ANALOG_INPUT_BOARD_RATE_10_HZ

/** Conversions needed after a configuration change before data is valid. */
#define ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS 2U

/* Wire-break detection ----------------------------------------------------- */

/*
 * Both sensor inputs are checked by changing what the converter looks at, so a
 * check cannot run at the same time as a measurement. The two linear inputs
 * have no check at all: a missing loop or a missing voltage looks exactly like a
 * valid low reading to them.
 *
 * Thermocouple: the burnout current sources in CONF0 source into one terminal
 * and sink from the other. A connected sensor closes that path through its own
 * loop resistance, while an open one lets the two terminals drift to the supply
 * rails, which saturates the converter at positive full scale.
 *
 * PT100: the excitation currents are already in circuit, so the voltage across
 * the reference resistor carries the diagnosis. All three leads intact puts
 * both currents through it, 1 mA across 1.65 kohm. One open sensor lead leaves
 * one current source without a path through the sensor and halves the voltage,
 * an open return path clears it.
 *
 * A check is configured like a measurement, so it needs the same filter
 * settling: ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS conversions of the configured
 * data rate.
 */

/** Burnout check: code magnitude above which the thermocouple input is open. */
#define ANALOG_INPUT_BOARD_BREAK_CODE_THRESHOLD 0x600000L

/** Reference resistor voltage while all three PT100 leads carry current. */
#define ANALOG_INPUT_BOARD_RTD_REFERENCE_VOLTS 1.65f

/**
 * Fraction of ANALOG_INPUT_BOARD_RTD_REFERENCE_VOLTS below which a PT100 lead
 * counts as open. The two faults read half and zero, the intact sensor reads
 * one, so the threshold sits above half and leaves room for the tolerance of
 * the excitation currents and of the resistor.
 */
#define ANALOG_INPUT_BOARD_RTD_BREAK_RATIO 0.75f

/**
 * Fraction of ANALOG_INPUT_BOARD_RTD_REFERENCE_VOLTS below which the open lead
 * is the shared return one rather than an excitation lead. An open excitation
 * lead still leaves the other current flowing through the resistor, so half
 * remains; only a broken return path clears the resistor completely. The
 * threshold sits below that half with the same margin as the one above.
 */
#define ANALOG_INPUT_BOARD_RTD_BREAK_RETURN_RATIO 0.25f

/* Concrete analog input object -------------------------------------------- */

/**
 * @brief Analog input object bound to one SIG24131 channel.
 *
 * base must stay the first member: the analog_input helpers cast between
 * analog_input_t* and analog_input_board_t*.
 */
typedef struct {
    analog_input_t base; /**< Interface base object, must be first. */
    sig24131_t* device;  /**< Borrowed SIG24131 driver instance. */
    uint8_t channel;     /**< One-based board channel. */
} analog_input_board_t;

/* Channel objects --------------------------------------------------------- */

/**
 * @brief Construct the analog input object of one board channel.
 *
 * Installs the operation table through analog_input_ctor() and binds the object
 * to the SIG24131 instance of the same channel and to the channel that owns the
 * front-end switches. Nothing is written to the hardware and no configuration is
 * loaded, so analog_input_configure() has to succeed before the object produces
 * values. The driver instances have to be constructed first, by
 * sig24131_board_ctor_all().
 *
 * @param channel One-based board channel, 1..analog_input_board_count().
 *
 * @return ANALOG_INPUT_OK, or ANALOG_INPUT_ERR_ARG when the channel is outside
 *         the populated range.
 */
analog_input_status_t analog_input_board_ctor(uint8_t channel);

/**
 * @brief Construct the object of every populated channel.
 *
 * Only RAM is written: no channel is configured and no channel produces values
 * until analog_input_configure() has succeeded on it.
 *
 * @return ANALOG_INPUT_OK, or the first failure of a channel constructor.
 */
analog_input_status_t analog_input_board_ctor_all(void);

/** Number of channel objects the module owns. */
uint8_t analog_input_board_count(void);

/**
 * @brief Look up one channel.
 *
 * @param channel One-based channel number, 1..analog_input_board_count().
 * @return The analog_input_t base of that channel, so the caller only needs the
 *         interface, or NULL when channel is outside the populated range.
 */
analog_input_t* analog_input_board_get(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif
