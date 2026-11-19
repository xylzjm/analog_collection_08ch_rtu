/**
 * @file param.c
 * @brief Parameter store: synchronous commit with write-then-read-back check.
 *
 * This file owns the whole parameter schema: the default table, the KV key
 * names, the change check, the read-back commit and the boot load result.
 * Storage itself sits behind the kv_store interface (kv_store.h).
 */

#include "param.h"

#include <string.h>

#include "elog.h"
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
 * @brief Default KV table: the single source of keys and default bodies.
 *
 * The key set is the 8-position board layout even when fewer channels are
 * populated, so a SKU variant keeps the same KV layout. The last entry is
 * the device address.
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

/* Compile-time guard: the table must match the 8-position board layout. */
typedef char param_table_count_check[((sizeof(defaults) / sizeof(defaults[0])) == PARAM_DEFAULT_KV_COUNT) ? 1 : -1];

/** @brief Bound storage backend; NULL until param_bind_store() runs. */
static const kv_store_ops_t* store;

/** @brief Context handed to every storage operation. */
static void* store_ctx;

/** @brief Last committed configuration per channel (change check base). */
static ch_config_t snapshot[CH_CONFIG_CHANNEL_COUNT];

/** @brief Non-zero when snapshot[ch] mirrors a committed configuration. */
static uint8_t snapshot_valid[CH_CONFIG_CHANNEL_COUNT];

/** @brief Last committed slave address. */
static uint16_t addr_snapshot;

/** @brief Non-zero when addr_snapshot mirrors a committed address. */
static uint8_t addr_snapshot_valid;

/** @brief Non-zero when the storage backend came up. */
static uint8_t db_ok;

void param_bind_store(const kv_store_ops_t* ops,
                      void* ctx)
{
    store     = ops;
    store_ctx = ctx;
}

static const char* param_key_cfg(uint8_t ch)
{
    return defaults[ch].key;
}

static const char* param_key_addr(void)
{
    return defaults[PARAM_DEFAULT_KV_COUNT - 1U].key;
}

/**
 * @brief Read one configuration key and validate its body.
 *
 * @param ch Zero-based channel number.
 * @param cfg Receives the validated, normalized configuration.
 * @return true when the key exists, has the expected size and validates.
 */
static bool load_cfg(uint8_t ch, ch_config_t* cfg)
{
    const char* key = param_key_cfg(ch);
    int len;

    len = store->get(store_ctx, key, cfg, sizeof(*cfg));
    if ( len < 0 ) {
        elog_warn("param", "load missing: %s", key);
        return false;
    }
    if ( len != (int)sizeof(*cfg) ) {
        elog_warn("param", "load size: %s (%d != %u)", key, len,
                  (unsigned)sizeof(*cfg));
        return false;
    }
    if ( !ch_config_validate(cfg) ) {
        elog_warn("param", "load invalid: %s", key);
        return false;
    }
    return true;
}

/**
 * @brief Read the slave address key and check its range.
 *
 * @param addr Receives the stored address.
 * @return true when the key exists, has the expected size and is 1..247.
 */
static bool load_addr(uint16_t* addr)
{
    const char* key = param_key_addr();
    int len;

    len = store->get(store_ctx, key, addr, sizeof(*addr));
    if ( len < 0 ) {
        elog_warn("param", "load missing: %s", key);
        return false;
    }
    if ( len != (int)sizeof(*addr) ) {
        elog_warn("param", "load size: %s (%d != %u)", key, len,
                  (unsigned)sizeof(*addr));
        return false;
    }
    if ( !reg_map_dev_addr_valid(*addr) ) {
        elog_warn("param", "load range: %s (%u)", key, (unsigned)*addr);
        return false;
    }
    return true;
}

/**
 * @brief Load every key into the internal snapshot and the caller's result.
 *
 * @param result Receives the loaded parameters and the per-key flags.
 */
static void load_all(param_boot_result_t* result)
{
    uint8_t ch;

    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        ch_config_t cfg;

        if ( load_cfg(ch, &cfg) ) {
            result->cfg[ch]       = cfg;
            result->cfg_valid[ch] = 1U;
            snapshot[ch]          = cfg;
            snapshot_valid[ch]    = 1U;
        } else {
            result->cfg_load_failed[ch] = 1U;
            snapshot_valid[ch]          = 0U;
        }
    }

    if ( load_addr(&result->dev_addr) ) {
        addr_snapshot       = result->dev_addr;
        addr_snapshot_valid = 1U;
    } else {
        result->dev_addr         = REG_MAP_DEV_ADDR_MIN;
        result->addr_load_failed = 1U;
        addr_snapshot_valid      = 0U;
    }
}

void param_boot_load(param_boot_result_t* result)
{
    memset(result, 0, sizeof(*result));
    result->dev_addr = REG_MAP_DEV_ADDR_MIN;
    db_ok            = 0U;

    if ( store == NULL ) {
        elog_error("param", "kv store not bound");
        return;
    }

    if ( store->init(store_ctx, defaults,
                     sizeof(defaults) / sizeof(defaults[0]))
         != 0 ) {
        elog_error("param", "kv store init failed");
        return;
    }

    db_ok         = 1U;
    result->db_ok = 1U;
    load_all(result);
}

param_status_t param_save_cfg(uint8_t ch,
                              const ch_config_t* cfg)
{
    const char* key;
    ch_config_t verify;
    int st;
    int len;

    if ( (cfg == NULL) || (ch >= CH_CONFIG_CHANNEL_COUNT) || (db_ok == 0U) ) {
        return PARAM_ERR_WRITE;
    }

    key = param_key_cfg(ch);

    if ( (snapshot_valid[ch] != 0U)
         && (memcmp(&snapshot[ch], cfg, sizeof(*cfg)) == 0) ) {
        return PARAM_NO_CHANGE;
    }

    st = store->set(store_ctx, key, cfg, sizeof(*cfg));

    /* The reply follows the stored content, not the library return code. */
    len = store->get(store_ctx, key, &verify, sizeof(verify));
    if ( (len != (int)sizeof(verify))
         || (memcmp(&verify, cfg, sizeof(verify)) != 0) ) {
        elog_error("param", "commit failed: %s (kv %d, read back %d)",
                   key, st, len);
        (void)st;
        return PARAM_ERR_WRITE;
    }

    snapshot[ch]       = *cfg;
    snapshot_valid[ch] = 1U;
    elog_info("param", "commit ok: %s (kv %d)", key, st);
    (void)st;
    return PARAM_OK;
}

param_status_t param_save_addr(uint16_t addr)
{
    const char* key;
    uint16_t verify;
    int st;
    int len;

    if ( (db_ok == 0U) || !reg_map_dev_addr_valid(addr) ) {
        return PARAM_ERR_WRITE;
    }

    key = param_key_addr();

    if ( (addr_snapshot_valid != 0U) && (addr_snapshot == addr) ) {
        return PARAM_NO_CHANGE;
    }

    st = store->set(store_ctx, key, &addr, sizeof(addr));

    len = store->get(store_ctx, key, &verify, sizeof(verify));
    if ( (len != (int)sizeof(verify)) || (verify != addr) ) {
        elog_error("param", "commit failed: %s (kv %d, read back %d)",
                   key, st, len);
        (void)st;
        return PARAM_ERR_WRITE;
    }

    addr_snapshot       = addr;
    addr_snapshot_valid = 1U;
    elog_info("param", "commit ok: %s -> %u (kv %d)",
              key, (unsigned)addr, st);
    (void)st;
    return PARAM_OK;
}
