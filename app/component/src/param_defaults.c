/**
 * @file param_defaults.c
 * @brief Factory default parameter table (the single source of defaults).
 *
 * The table is handed to fdb_kvdb_init(): an empty flash materializes every
 * key from here on first boot. Default channel configuration: thermocouple
 * type K at 10 Hz with unit calibration (docs/component/param.md section 6).
 */

#include "ch_config.h"
#include "flashdb.h"
#include "reg_map.h"

/** @brief Factory default channel configuration: TC type K, 10 Hz. */
static const ch_config_t default_ch_cfg = {
    (uint8_t)CH_TYPE_TC,     /**< type */
    (uint8_t)CH_TC_TYPE_K,   /**< tc_type */
    2U,                      /**< rate_code: 10 Hz */
    0.0f,                    /**< linear_min (unused for TC) */
    0.0f,                    /**< linear_max (unused for TC) */
    0.0f,                    /**< offset */
    1.0f,                    /**< gain */
};

/** @brief Factory default Modbus slave address. */
static const uint16_t default_dev_addr = REG_MAP_DEV_ADDR_MIN;

/** @brief Default KV nodes: one per persisted parameter (zero-based index). */
static struct fdb_default_kv_node default_kv_nodes[] = {
    { "cfg/ch0", (void*)&default_ch_cfg, sizeof(ch_config_t) },
    { "cfg/ch1", (void*)&default_ch_cfg, sizeof(ch_config_t) },
    { "cfg/ch2", (void*)&default_ch_cfg, sizeof(ch_config_t) },
    { "cfg/ch3", (void*)&default_ch_cfg, sizeof(ch_config_t) },
    { "cfg/ch4", (void*)&default_ch_cfg, sizeof(ch_config_t) },
    { "cfg/ch5", (void*)&default_ch_cfg, sizeof(ch_config_t) },
    { "cfg/ch6", (void*)&default_ch_cfg, sizeof(ch_config_t) },
    { "cfg/ch7", (void*)&default_ch_cfg, sizeof(ch_config_t) },
    { "dev/addr", (void*)&default_dev_addr, sizeof(uint16_t) },
};

/** @brief The factory default table for fdb_kvdb_init(). */
struct fdb_default_kv param_default_kv = {
    default_kv_nodes,
    sizeof(default_kv_nodes) / sizeof(default_kv_nodes[0])
};
