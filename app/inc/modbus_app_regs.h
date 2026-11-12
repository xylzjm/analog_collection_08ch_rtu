#ifndef MODBUS_APP_REGS_H
#define MODBUS_APP_REGS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief User holding-register addresses. */
#define MODBUS_REG_ADDRESS   0x0000U
#define MODBUS_REG_BAUDRATE  0x0001U
#define MODBUS_REG_USER_FIRST 0x0002U
#define MODBUS_REG_USER_LAST  0x00FFU
#define MODBUS_REG_CH_FIRST   0x0100U
#define MODBUS_REG_CH_LAST    0x01FFU
#define MODBUS_REG_CH_SIZE    0x0020U
#define MODBUS_REG_CH_COUNT   8U
#define MODBUS_REG_USER_RESERVED_LAST 0x0EFFU

/** @brief Offsets inside one channel block. */
#define MODBUS_REG_CH_TYPE       0x00U
#define MODBUS_REG_CH_RATE       0x01U
#define MODBUS_REG_CH_LINEAR_MIN 0x02U
#define MODBUS_REG_CH_LINEAR_MAX 0x04U
#define MODBUS_REG_CH_USER_OFFSET 0x06U
#define MODBUS_REG_CH_USER_GAIN   0x08U
#define MODBUS_REG_CH_RESERVED   0x0AU

/** @brief Input-register addresses. */
#define MODBUS_IN_CH_VALUE_FIRST 0x0000U
#define MODBUS_IN_CH_VALUE_LAST  0x000FU
#define MODBUS_IN_CH_STATUS      0x0010U
#define MODBUS_IN_CJ_VALUE       0x0018U
#define MODBUS_IN_CJ_STATUS      0x001AU
#define MODBUS_IN_DEV_FAULTS     0x001BU
#define MODBUS_IN_RESERVED_FIRST 0x001CU

/** @brief Baud rate reported by MODBUS_REG_BAUDRATE. */
#define MODBUS_BAUDRATE 9600U

/** @brief Wire value selecting the default sample rate (10 Hz). */
#define MODBUS_REG_RATE_DEFAULT 0xFFFFU

/** @brief Stored index of the default sample rate (10 Hz). */
#define MODBUS_REG_RATE_DEFAULT_INDEX 2U

/** @brief Report-Slave-ID instance data. */
#define MODBUS_APP_DEV_FAULT_LOAD 0x0001U
#define MODBUS_APP_DEV_FAULT_SAVE 0x0002U

/**
 * @brief Test a Modbus sensor type.
 *
 * @param value Type value 0..10.
 * @return true when defined.
 */
bool modbus_app_regs_type_valid(uint16_t value);

/**
 * @brief Test a sample-rate index.
 *
 * @param value Index 0..12, or 0xFFFF for the default.
 * @return true when defined.
 */
bool modbus_app_regs_rate_valid(uint16_t value);

/**
 * @brief Return the rate in millihertz of a valid index.
 *
 * @param index Index 0..12, or 0xFFFF for 10 Hz.
 * @return Rate in mHz.
 */
uint32_t modbus_app_regs_rate_millihz(uint16_t index);

/**
 * @brief Convert a wire rate index to the stored index.
 *
 * @param value Wire value 0..12 or MODBUS_REG_RATE_DEFAULT.
 * @return Stored index; the default wire value maps to
 *         MODBUS_REG_RATE_DEFAULT_INDEX.
 */
uint8_t modbus_app_regs_rate_store_index(uint16_t value);

/**
 * @brief Test whether a sensor type uses the linear lower/upper limits.
 *
 * @param value Modbus type 0..10.
 * @return true only for 4-20 mA (0) and 0-10 V (1).
 */
bool modbus_app_regs_type_is_linear(uint16_t value);

/** @brief Test a user calibration gain, x0.001. */
bool modbus_app_regs_gain_valid(int32_t gain);

/** @brief Test a user calibration offset, x0.001. */
bool modbus_app_regs_offset_valid(int32_t offset);

/**
 * @brief Decompose a holding-register address into a channel block offset.
 *
 * @param address Register address.
 * @param channel Receives channel 0..7.
 * @param offset Receives offset 0..0x1F.
 * @return true when the address belongs to a channel block.
 */
bool modbus_app_regs_channel_addr(uint16_t address, uint8_t* channel, uint8_t* offset);

/** @brief Read one big-endian 16-bit register from a callback buffer. */
uint16_t modbus_app_regs_get_u16(const uint8_t* buffer);

/** @brief Write one big-endian 16-bit register into a callback buffer. */
void modbus_app_regs_put_u16(uint8_t* buffer, uint16_t value);

/** @brief Read a high-word-first big-endian int32 from a callback buffer. */
int32_t modbus_app_regs_get_s32(const uint8_t* buffer);

/** @brief Write a high-word-first big-endian int32 into a callback buffer. */
void modbus_app_regs_put_s32(uint8_t* buffer, int32_t value);

#ifdef __cplusplus
}
#endif

#endif
