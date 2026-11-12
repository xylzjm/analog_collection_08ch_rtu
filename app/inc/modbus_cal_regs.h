#ifndef MODBUS_CAL_REGS_H
#define MODBUS_CAL_REGS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Hidden calibration block, inclusive range. */
#define MODBUS_CAL_BASE 0x0F00U
#define MODBUS_CAL_LAST 0x0F1FU
#define MODBUS_CAL_COUNT 0x20U

/** @brief Hidden register offsets. */
#define MODBUS_CAL_MAGIC1    0x0F00U
#define MODBUS_CAL_MAGIC2    0x0F01U
#define MODBUS_CAL_CMD       0x0F02U
#define MODBUS_CAL_CHANNEL   0x0F03U
#define MODBUS_CAL_TYPE      0x0F04U
#define MODBUS_CAL_STATUS    0x0F05U
#define MODBUS_CAL_ERROR     0x0F06U
#define MODBUS_CAL_RAW_LOW   0x0F07U
#define MODBUS_CAL_RAW_HIGH  0x0F09U
#define MODBUS_CAL_GAIN      0x0F0BU
#define MODBUS_CAL_OFFSET    0x0F0DU
#define MODBUS_CAL_VALID     0x0F0FU
#define MODBUS_CAL_REMAINING 0x0F10U

/** @brief Two-step unlock values. */
#define MODBUS_CAL_MAGIC1_VALUE 0xCA11U
#define MODBUS_CAL_MAGIC2_VALUE 0xB8A7U

/** @brief Time window of the two-step unlock and the idle lock timeout. */
#define MODBUS_CAL_MAGIC_WINDOW_MS 1000U
#define MODBUS_CAL_LOCK_TIMEOUT_MS 60000U

/** @brief Calibration command written to MODBUS_CAL_CMD. */
typedef enum {
    MODBUS_CAL_CMD_SELECT = 1, /**< Select channel and type. */
    MODBUS_CAL_CMD_LOW    = 2, /**< Capture the low reference. */
    MODBUS_CAL_CMD_HIGH   = 3, /**< Capture the high reference. */
    MODBUS_CAL_CMD_COMMIT = 4, /**< Validate and save. */
    MODBUS_CAL_CMD_ABORT  = 5, /**< Abort the current capture, stay unlocked. */
    MODBUS_CAL_CMD_EXIT   = 6  /**< Lock the block and release the channel. */
} modbus_cal_cmd_t;

/** @brief Value of MODBUS_CAL_STATUS. */
typedef enum {
    CAL_STATUS_IDLE            = 0,
    CAL_STATUS_WAITING_CHANNEL = 1,
    CAL_STATUS_SETTLING        = 2,
    CAL_STATUS_SETTLED         = 3,
    CAL_STATUS_CAPTURING_LOW   = 4,
    CAL_STATUS_LOW_CAPTURED    = 5,
    CAL_STATUS_CAPTURING_HIGH  = 6,
    CAL_STATUS_HIGH_CAPTURED   = 7,
    CAL_STATUS_SAVING          = 8,
    CAL_STATUS_DONE            = 9,
    CAL_STATUS_ERROR           = 10
} cal_status_t;

/** @brief Value of MODBUS_CAL_ERROR. */
typedef enum {
    CAL_ERROR_NONE                  = 0,
    CAL_ERROR_INVALID_CHANNEL_TYPE  = 1,
    CAL_ERROR_CHANNEL_NOT_READY     = 2,
    CAL_ERROR_CAPTURE_FAILED        = 3,
    CAL_ERROR_SPAN_TOO_SMALL        = 4,
    CAL_ERROR_GAIN_OUT_OF_RANGE     = 5,
    CAL_ERROR_OFFSET_OUT_OF_RANGE   = 6,
    CAL_ERROR_SAVE_FAILED           = 7,
    CAL_ERROR_BUSY                  = 8,
    CAL_ERROR_NOT_SELECTED          = 9,
    CAL_ERROR_CONFIG_FAILED         = 10,
    CAL_ERROR_REFERENCE_FAILED      = 11
} cal_error_t;

/** @brief Result of validating one hidden-block write range. */
typedef enum {
    MODBUS_CAL_WRITE_OK = 0,       /**< Range is valid. */
    MODBUS_CAL_WRITE_BAD_ADDRESS,  /**< Address or reserved register. */
    MODBUS_CAL_WRITE_BAD_VALUE     /**< CMD span or CMD value is invalid. */
} modbus_cal_write_status_t;

/**
 * @brief Validate one hidden-block write range without applying it.
 *
 * @param start First register address.
 * @param count Number of registers.
 * @param buffer Big-endian register data; count * 2 bytes.
 * @return Validation result; BAD_ADDRESS maps to exception 02, BAD_VALUE
 *         maps to exception 03.
 */
modbus_cal_write_status_t modbus_cal_regs_check_write(
    uint16_t start,
    uint16_t count,
    uint8_t const* buffer);

/**
 * @brief Test whether a register range lies inside the hidden block.
 *
 * @param start First register address.
 * @param count Number of registers.
 * @return true when every register is in 0x0F00..0x0F1F.
 */
bool modbus_cal_regs_in_block(uint16_t start, uint16_t count);

/**
 * @brief Test whether a register range overlaps the hidden block.
 *
 * @param start First register address.
 * @param count Number of registers.
 * @return true when at least one register is in 0x0F00..0x0F1F.
 */
bool modbus_cal_regs_overlaps(uint16_t start, uint16_t count);

/** @brief Test a MODBUS_CAL_CMD value. */
bool modbus_cal_regs_valid_cmd(uint16_t cmd);

#ifdef __cplusplus
}
#endif

#endif
