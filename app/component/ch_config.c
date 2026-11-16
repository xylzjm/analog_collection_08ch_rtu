/**
 * @file ch_config.c
 * @brief Channel configuration validation, normalization and rate tables.
 */

#include "ch_config.h"

#include <float.h>
#include <string.h>

/** @brief Sample rates of the rate codes, in mHz (mirrors ANALOG_INPUT_BOARD_RATE_*). */
static const uint32_t rate_millihz_table[CH_CONFIG_RATE_CODE_COUNT] = {
    2500U,   /* 2.5 Hz  */
    5000U,   /* 5 Hz    */
    10000U,  /* 10 Hz   */
    12500U,  /* 12.5 Hz */
    15000U,  /* 15 Hz   */
    20000U,  /* 20 Hz   */
    25000U,  /* 25 Hz   */
    30000U,  /* 30 Hz   */
    40000U,  /* 40 Hz   */
    50000U,  /* 50 Hz   */
    60000U,  /* 60 Hz   */
    80000U,  /* 80 Hz   */
    100000U  /* 100 Hz  */
};

/** @brief Sample period of the rate codes in 1 ms ticks: ceil(1e6 / rate_mhz). */
static const uint32_t rate_ticks_table[CH_CONFIG_RATE_CODE_COUNT] = {
    400U, /* 2.5 Hz  */
    200U, /* 5 Hz    */
    100U, /* 10 Hz   */
    80U,  /* 12.5 Hz */
    67U,  /* 15 Hz   */
    50U,  /* 20 Hz   */
    40U,  /* 25 Hz   */
    34U,  /* 30 Hz   */
    25U,  /* 40 Hz   */
    20U,  /* 50 Hz   */
    17U,  /* 60 Hz   */
    13U,  /* 80 Hz   */
    10U   /* 100 Hz  */
};

/**
 * @brief Report whether a float holds a finite value.
 *
 * @param x Value to test.
 * @return true for finite values, false for NaN and either infinity.
 */
static bool f32_is_finite(float x)
{
    return (x == x) && (x >= -FLT_MAX) && (x <= FLT_MAX);
}

bool ch_config_validate(ch_config_t* cfg)
{
    ch_config_t norm;

    if ( cfg == NULL ) {
        return false;
    }
    norm = *cfg;

    if ( norm.type > (uint8_t)CH_TYPE_0_10V ) {
        return false;
    }

    if ( norm.type == (uint8_t)CH_TYPE_TC ) {
        if ( norm.tc_type > (uint8_t)CH_TC_TYPE_T ) {
            return false;
        }
    }
    else {
        norm.tc_type = CH_TC_TYPE_NONE;
    }

    if ( norm.rate_code > CH_CONFIG_RATE_CODE_MAX ) {
        return false;
    }

    if ( !f32_is_finite(norm.linear_min) || !f32_is_finite(norm.linear_max)
         || !f32_is_finite(norm.offset) || !f32_is_finite(norm.gain) ) {
        return false;
    }

    if ( norm.type != (uint8_t)CH_TYPE_4_20MA
         && norm.type != (uint8_t)CH_TYPE_0_10V ) {
        norm.linear_min = 0.0f;
        norm.linear_max = 0.0f;
    }

    *cfg = norm;
    return true;
}

uint32_t ch_config_rate_millihz(uint16_t rate_code)
{
    if ( rate_code > CH_CONFIG_RATE_CODE_MAX ) {
        return 0U;
    }
    return rate_millihz_table[rate_code];
}

uint32_t ch_config_rate_ticks(uint16_t rate_code)
{
    if ( rate_code > CH_CONFIG_RATE_CODE_MAX ) {
        return 0U;
    }
    return rate_ticks_table[rate_code];
}
