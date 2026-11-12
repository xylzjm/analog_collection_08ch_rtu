#ifndef STORAGE_FLASH_H
#define STORAGE_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "cal_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Deferred-queue depth of storage_app. */
#define STORAGE_FLASH_DEFER_DEPTH 8U

/**
 * @brief Initialize FlashDB and load every parameter and calibration key.
 *
 * @return true when at least one user parameter needed a default, so the
 *         caller should set the parameter-load fault bit.
 * @note Synchronous; call before any active object is started.
 */
bool storage_load(void);

/**
 * @brief Write one user parameter blob.
 *
 * @param key 0 is "sys", 1..8 are "ch0".."ch7".
 * @param data Record bytes.
 * @param len Record length.
 * @return true on success.
 */
bool storage_flash_write_param(uint8_t key, const void* data, uint16_t len);

/**
 * @brief Write one factory calibration record under "calN".
 *
 * @param channel Modbus channel 0..7.
 * @param record Record to write.
 * @return true on success.
 */
bool storage_flash_write_cal(uint8_t channel, const cal_ch_record_t* record);

#ifdef __cplusplus
}
#endif

#endif
