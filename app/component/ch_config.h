#ifndef CH_CONFIG_H
#define CH_CONFIG_H

/**
 * @file ch_config.h
 * @brief Channel configuration model, validation and rate tables.
 *
 * Pure logic, no dependencies beyond the C standard library. Shared by the
 * Modbus write pipeline (reg_map / AoModbus), the parameter store (param) and
 * the channel active object.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Number of measurement channels in this product. */
#define CH_CONFIG_CHANNEL_COUNT 8U

/** @brief Highest valid rate code (inclusive); 0..CH_CONFIG_RATE_CODE_MAX. */
#define CH_CONFIG_RATE_CODE_MAX 12U

/** @brief Number of entries in the sample rate tables. */
#define CH_CONFIG_RATE_CODE_COUNT (CH_CONFIG_RATE_CODE_MAX + 1U)

/** @brief Input type codes stored in ch_config_t::type. */
typedef enum {
    CH_TYPE_TC     = 0, /**< Thermocouple. */
    CH_TYPE_PT100  = 1, /**< RTD. */
    CH_TYPE_4_20MA = 2, /**< Current input. */
    CH_TYPE_0_10V  = 3  /**< Voltage input. */
} ch_type_t;

/** @brief Thermocouple type codes stored in ch_config_t::tc_type. */
typedef enum {
    CH_TC_TYPE_B = 0,
    CH_TC_TYPE_E = 1,
    CH_TC_TYPE_J = 2,
    CH_TC_TYPE_K = 3,
    CH_TC_TYPE_N = 4,
    CH_TC_TYPE_R = 5,
    CH_TC_TYPE_S = 6,
    CH_TC_TYPE_T = 7
} ch_tc_type_t;

/** @brief tc_type value meaning "not a thermocouple" (normalization result). */
#define CH_TC_TYPE_NONE 0xFFU

/** @brief Sample rate codes; the register encoding of the sample rate. */
typedef enum {
    CH_RATE_2_5_HZ  = 0,
    CH_RATE_5_HZ    = 1,
    CH_RATE_10_HZ   = 2,
    CH_RATE_12_5_HZ = 3,
    CH_RATE_15_HZ   = 4,
    CH_RATE_20_HZ   = 5,
    CH_RATE_25_HZ   = 6,
    CH_RATE_30_HZ   = 7,
    CH_RATE_40_HZ   = 8,
    CH_RATE_50_HZ   = 9,
    CH_RATE_60_HZ   = 10,
    CH_RATE_80_HZ   = 11,
    CH_RATE_100_HZ  = 12
} rate_code_t;

/**
 * @brief One channel's configuration, 20 bytes, no padding, no version field.
 *
 * This layout is also the persistence format (KV value body) and the source
 * of the register encoding, so it must stay stable.
 */
typedef struct {
    uint8_t  type;      /**< Input type, ch_type_t. */
    uint8_t  tc_type;   /**< Thermocouple type (CH_TC_TYPE_*) or CH_TC_TYPE_NONE. */
    uint16_t rate_code; /**< Sample rate code, rate_code_t (0..12). */
    float    linear_min; /**< Linear input low limit, engineering value. */
    float    linear_max; /**< Linear input high limit, engineering value. */
    float    offset;     /**< Calibration offset: value = raw * gain + offset. */
    float    gain;       /**< Calibration gain. */
} ch_config_t;

/* Compile-time guard: the struct body is the 20 byte persistence format. */
typedef char ch_config_persist_size_check[(sizeof(ch_config_t) == 20U) ? 1 : -1];

/**
 * @brief Validate one configuration and normalize it in place.
 *
 * Validation and normalization are one operation so that semantically equal
 * configurations always compare equal byte for byte (the change check in the
 * write pipeline relies on memcmp).
 *
 * @param cfg Configuration to validate; written only on success.
 * @return true when the configuration is valid (and now normalized), false
 *         when it must be rejected. On false the input is left untouched.
 * @note Normalization: tc_type becomes CH_TC_TYPE_NONE for non-thermocouple
 *       types; linear_min/linear_max become 0.0 for non-linear types.
 */
bool ch_config_validate(ch_config_t* cfg);

/**
 * @brief Sample rate of a rate code in millihertz.
 *
 * @param rate_code Rate code (0..12).
 * @return The rate in mHz (same values as ANALOG_INPUT_BOARD_RATE_*), or 0
 *         for an invalid code.
 */
uint32_t ch_config_rate_millihz(uint16_t rate_code);

/**
 * @brief Sample period of a rate code in 1 ms ticks.
 *
 * @param rate_code Rate code (0..12).
 * @return ceil(1e6 / rate_mhz) ticks, or 0 for an invalid code. Rounding up
 *         keeps the realized rate at or below the nominal rate.
 */
uint32_t ch_config_rate_ticks(uint16_t rate_code);

#ifdef __cplusplus
}
#endif

#endif
