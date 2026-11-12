#ifndef CAL_DATA_H
#define CAL_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "analog_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Number of factory calibration types. */
#define CAL_DATA_TYPE_COUNT 4U

/** @brief Number of calibrated channels. */
#define CAL_DATA_CHANNEL_COUNT 8U

/** @brief Calibration record format version. */
#define CAL_DATA_VERSION 1U

/** @brief Allowed factory gain range, 1000000 means 1.0. */
#define CAL_GAIN_MICRO_MIN 500000L
#define CAL_GAIN_MICRO_MAX 2000000L

/** @brief Allowed factory offset magnitude, in raw ADC codes. */
#define CAL_OFFSET_CODE_LIMIT 16777216L

/** @brief Factory calibration type; independent of the Modbus user type. */
typedef enum {
    CAL_DATA_TYPE_THERMOCOUPLE = 0, /**< Thermocouple mV channel. */
    CAL_DATA_TYPE_PT100        = 1, /**< PT100 resistance channel. */
    CAL_DATA_TYPE_4_20MA       = 2, /**< 4-20 mA current channel. */
    CAL_DATA_TYPE_0_10V        = 3  /**< 0-10 V voltage channel. */
} cal_data_type_t;

/** @brief Coefficients of one calibration type. */
typedef struct {
    int32_t gain_micro;  /**< 1000000 means 1.0. */
    int32_t offset_code; /**< Offset in raw ADC codes. */
} cal_entry_t;

/** @brief One channel's complete factory calibration record. */
typedef struct {
    uint16_t version;   /**< CAL_DATA_VERSION. */
    uint16_t size;      /**< sizeof(cal_ch_record_t). */
    uint8_t valid_mask; /**< One bit per cal_data_type_t. */
    uint8_t reserved[3];
    cal_entry_t entry[CAL_DATA_TYPE_COUNT];
} cal_ch_record_t;

/** @brief Reset the module to "nothing calibrated", gain 1.0. */
void cal_data_init(void);

/**
 * @brief Return the record of one channel.
 *
 * @param channel Modbus channel 0..7.
 * @return The internal record, or NULL for an invalid channel.
 */
const cal_ch_record_t* cal_data_get(uint8_t channel);

/**
 * @brief Replace one channel's record with a validated record.
 *
 * @param channel Modbus channel 0..7.
 * @param record Record to copy.
 * @return true when stored, false for an invalid argument.
 */
bool cal_data_set(uint8_t channel, const cal_ch_record_t* record);

/**
 * @brief Validate a calibration record and clear out-of-range entries.
 *
 * @param record Record to check and modify.
 * @return false when the header is invalid; true otherwise.
 * @note Valid bits whose coefficients are out of range are cleared.
 */
bool cal_data_record_sanitize(cal_ch_record_t* record);

/**
 * @brief Return the valid-type bitmap of one channel.
 *
 * @param channel Modbus channel 0..7.
 * @return Bit mask, or 0 for an invalid channel.
 */
uint8_t cal_data_valid_mask(uint8_t channel);

/**
 * @brief Apply one channel's valid coefficients to the board table.
 *
 * @param channel Modbus channel 0..7.
 * @note Invalid types explicitly clear the board entry.
 */
void cal_data_apply_channel(uint8_t channel);

/** @brief Apply every channel's valid coefficients to the board table. */
void cal_data_apply_all(void);

/**
 * @brief Map an analog input type to a factory calibration type.
 *
 * @param type User input type.
 * @return Matching cal_data_type_t.
 */
cal_data_type_t cal_data_type_from_input(analog_input_type_t type);

#ifdef __cplusplus
}
#endif

#endif
