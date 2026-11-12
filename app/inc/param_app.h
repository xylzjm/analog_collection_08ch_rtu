#ifndef PARAM_APP_H
#define PARAM_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "analog_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Number of user parameter channels. */
#define PARAM_APP_CHANNEL_COUNT 8U

/** @brief Parameter record format version. */
#define PARAM_APP_VERSION 1U

/** @brief Default slave address. */
#define PARAM_APP_DEFAULT_ADDRESS 1U

/** @brief Modbus sensor type used by every channel after power-up. */
#define PARAM_APP_DEFAULT_TYPE 6U

/** @brief Default sample rate index (10 Hz). */
#define PARAM_APP_DEFAULT_RATE_INDEX 2U

/** @brief Default user calibration gain, 1000 means 1.0. */
#define PARAM_APP_DEFAULT_USER_GAIN 1000L

/** @brief User calibration gain range. */
#define PARAM_APP_USER_GAIN_MIN 100L
#define PARAM_APP_USER_GAIN_MAX 10000L

/** @brief User calibration offset range, x0.001. */
#define PARAM_APP_USER_OFFSET_MIN (-1000000L)
#define PARAM_APP_USER_OFFSET_MAX 1000000L

/** @brief System parameter record stored under key "sys". */
typedef struct {
    uint16_t version; /**< PARAM_APP_VERSION. */
    uint16_t size;    /**< sizeof(param_sys_record_t). */
    uint16_t address; /**< Modbus slave address 1..247. */
    uint16_t reserved;
} param_sys_record_t;

/** @brief One channel record stored under key "chN". */
typedef struct {
    uint16_t version;     /**< PARAM_APP_VERSION. */
    uint16_t size;        /**< sizeof(param_ch_record_t). */
    uint8_t type;         /**< Modbus sensor type 0..10. */
    uint8_t rate_index;   /**< Sample rate index 0..12. */
    uint16_t reserved;
    int32_t linear_min;   /**< Linear lower limit x0.001. */
    int32_t linear_max;   /**< Linear upper limit x0.001. */
    int32_t user_offset;  /**< User offset x0.001. */
    int32_t user_gain;    /**< User gain x0.001; 1000 is 1.0. */
} param_ch_record_t;

/** @brief Load defaults into every parameter and clear the dirty mask. */
void param_app_init(void);

/** @brief Return the slave address. */
uint16_t param_app_get_address(void);

/** @brief Copy the system record. */
void param_app_get_sys(param_sys_record_t* record);

/**
 * @brief Copy one channel record.
 *
 * @param channel Modbus channel 0..7.
 * @param record Receives the record.
 * @return true when the channel is valid.
 */
bool param_app_get_channel(uint8_t channel, param_ch_record_t* record);

/**
 * @brief Store the system record and mark "sys" dirty.
 *
 * @param record New record; version and size are filled in.
 */
void param_app_set_sys(const param_sys_record_t* record);

/**
 * @brief Store one channel record and mark "chN" dirty.
 *
 * @param channel Modbus channel 0..7.
 * @param record New record; version and size are filled in.
 * @return true when stored.
 */
bool param_app_set_channel(uint8_t channel, const param_ch_record_t* record);

/**
 * @brief Restore a system record from Flash without marking it dirty.
 *
 * @param record Record read from Flash.
 * @return true when version and size match.
 */
bool param_app_restore_sys(const param_sys_record_t* record);

/**
 * @brief Restore a channel record from Flash without marking it dirty.
 *
 * @param channel Modbus channel 0..7.
 * @param record Record read from Flash.
 * @return true when version and size match.
 */
bool param_app_restore_channel(uint8_t channel, const param_ch_record_t* record);

/**
 * @brief Dirty key mask used by storage.
 *
 * @return bit0 "sys", bit1..8 "ch0".."ch7".
 */
uint16_t param_app_dirty_mask(void);

/**
 * @brief Copy the current blob of one key for storage.
 *
 * @param key 0 is "sys", 1..8 are "ch0".."ch7".
 * @param buf Receives the record.
 * @param len Receives the record length.
 * @param rev Receives the key revision.
 * @return true when the key is valid.
 */
bool param_app_snapshot(uint8_t key, void* buf, uint16_t* len, uint32_t* rev);

/**
 * @brief Clear one key's dirty bit when no newer write happened.
 *
 * @param key 0 is "sys", 1..8 are "ch0".."ch7".
 * @param rev Revision obtained from param_app_snapshot().
 */
void param_app_clear_dirty(uint8_t key, uint32_t rev);

/**
 * @brief Convert a Modbus sensor type to an analog input configuration.
 *
 * @param type Modbus type 0..10.
 * @param config Receives type and thermocouple model.
 * @return true when the type is defined.
 */
bool param_app_type_to_config(uint8_t type, analog_input_config_t* config);

/**
 * @brief Test whether a linear limit pair is usable.
 *
 * @param min_value Lower limit x0.001.
 * @param max_value Upper limit x0.001.
 * @return true when min_value < max_value.
 */
bool param_app_linear_valid(int32_t min_value, int32_t max_value);

#ifdef __cplusplus
}
#endif

#endif
