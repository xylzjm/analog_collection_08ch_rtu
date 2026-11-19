/**
 * @file param_defaults.c
 * @brief Factory default parameter table (the single source of defaults).
 *
 * The table is handed to the KV store at init: an empty flash materializes
 * every key from here on first boot. Default channel configuration:
 * thermocouple type K at 10 Hz with unit calibration
 * (docs/component/param.md section 6).
 */

#include "param_defaults.h"

#include "ch_config.h"
#include "reg_map.h"

/** @brief Factory default channel configuration: TC type K, 10 Hz. */
static const ch_config_t default_ch_cfg = {
    (uint8_t)CH_TYPE_TC,   /**< type */
    (uint8_t)CH_TC_TYPE_K, /**< tc_type */
    2U,                    /**< rate_code: 10 Hz */
    0.0f,                  /**< linear_min (unused for TC) */
    0.0f,                  /**< linear_max (unused for TC) */
    0.0f,                  /**< offset */
    1.0f,                  /**< gain */
};

/** @brief Factory default Modbus slave address. */
static const uint16_t default_dev_addr = REG_MAP_DEV_ADDR_MIN;

/**
 * @brief Default KV table: one entry per persisted parameter.
 *
 * The key set is the 8-position board layout even when fewer channels are
 * populated, so a SKU variant keeps the same KV layout.
 */
static const kv_default_t defaults[] = {
    {"cfg/ch0", &default_ch_cfg, sizeof(default_ch_cfg)},
    {"cfg/ch1", &default_ch_cfg, sizeof(default_ch_cfg)},
    {"cfg/ch2", &default_ch_cfg, sizeof(default_ch_cfg)},
    {"cfg/ch3", &default_ch_cfg, sizeof(default_ch_cfg)},
    {"cfg/ch4", &default_ch_cfg, sizeof(default_ch_cfg)},
    {"cfg/ch5", &default_ch_cfg, sizeof(default_ch_cfg)},
    {"cfg/ch6", &default_ch_cfg, sizeof(default_ch_cfg)},
    {"cfg/ch7", &default_ch_cfg, sizeof(default_ch_cfg)},
    {"dev/addr", &default_dev_addr, sizeof(default_dev_addr)},
};

const kv_default_t* param_defaults(size_t* count)
{
    if ( count != NULL ) {
        *count = sizeof(defaults) / sizeof(defaults[0]);
    }
    return defaults;
}
