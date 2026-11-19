/**
 * @file param.c
 * @brief Parameter store: synchronous commit with write-then-read-back check.
 *
 * The storage backend sits behind the kv_store interface (kv_store.h); this
 * file owns the parameter semantics only: change check, read-back commit
 * and the boot load result.
 */

#include "param.h"

#include <string.h>

#include "elog.h"
#include "kv_store.h"
#include "param_defaults.h"
#include "reg_map.h"

/** @brief KV key of the slave address. */
#define PARAM_KEY_DEV_ADDR "dev/addr"

/** @brief KV keys of the per-channel configurations (zero-based index). */
static const char* const cfg_keys[] = {
    "cfg/ch0", "cfg/ch1", "cfg/ch2", "cfg/ch3",
    "cfg/ch4", "cfg/ch5", "cfg/ch6", "cfg/ch7"};

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

/**
 * @brief Read one configuration key and validate its body.
 *
 * @param ch Zero-based channel number.
 * @param cfg Receives the validated, normalized configuration.
 * @return true when the key exists, has the expected size and validates.
 */
static bool load_cfg(uint8_t ch, ch_config_t* cfg)
{
    int len;

    len = store->get(store_ctx, cfg_keys[ch], cfg, sizeof(*cfg));
    if ( len != (int)sizeof(*cfg) ) {
        return false;
    }
    return ch_config_validate(cfg);
}

/**
 * @brief Read the slave address key and check its range.
 *
 * @param addr Receives the stored address.
 * @return true when the key exists, has the expected size and is 1..247.
 */
static bool load_addr(uint16_t* addr)
{
    int len;

    len = store->get(store_ctx, PARAM_KEY_DEV_ADDR, addr, sizeof(*addr));
    if ( len != (int)sizeof(*addr) ) {
        return false;
    }
    return reg_map_dev_addr_valid(*addr);
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
            elog_warn("param", "load failed: %s", cfg_keys[ch]);
        }
    }

    if ( load_addr(&result->dev_addr) ) {
        addr_snapshot       = result->dev_addr;
        addr_snapshot_valid = 1U;
    } else {
        result->dev_addr         = REG_MAP_DEV_ADDR_MIN;
        result->addr_load_failed = 1U;
        addr_snapshot_valid      = 0U;
        elog_warn("param", "load failed: %s", PARAM_KEY_DEV_ADDR);
    }
}

void param_boot_load(param_boot_result_t* result)
{
    const kv_default_t* defaults;
    size_t count;

    memset(result, 0, sizeof(*result));
    result->dev_addr = REG_MAP_DEV_ADDR_MIN;
    db_ok            = 0U;

    if ( store == NULL ) {
        elog_error("param", "kv store not bound");
        return;
    }

    defaults = param_defaults(&count);
    if ( store->init(store_ctx, defaults, count) != 0 ) {
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
    ch_config_t verify;
    int st;
    int len;

    if ( (cfg == NULL) || (ch >= CH_CONFIG_CHANNEL_COUNT) || (db_ok == 0U) ) {
        return PARAM_ERR_WRITE;
    }

    if ( (snapshot_valid[ch] != 0U)
         && (memcmp(&snapshot[ch], cfg, sizeof(*cfg)) == 0) ) {
        return PARAM_NO_CHANGE;
    }

    st = store->set(store_ctx, cfg_keys[ch], cfg, sizeof(*cfg));

    /* The reply follows the stored content, not the library return code. */
    len = store->get(store_ctx, cfg_keys[ch], &verify, sizeof(verify));
    if ( (len != (int)sizeof(verify))
         || (memcmp(&verify, cfg, sizeof(verify)) != 0) ) {
        elog_error("param", "commit failed: %s (kv %d, read back %d)",
                   cfg_keys[ch], st, len);
        (void)st;
        return PARAM_ERR_WRITE;
    }

    snapshot[ch]       = *cfg;
    snapshot_valid[ch] = 1U;
    elog_info("param", "commit ok: %s (kv %d)", cfg_keys[ch], st);
    (void)st;
    return PARAM_OK;
}

param_status_t param_save_addr(uint16_t addr)
{
    uint16_t verify;
    int st;
    int len;

    if ( (db_ok == 0U) || !reg_map_dev_addr_valid(addr) ) {
        return PARAM_ERR_WRITE;
    }

    if ( (addr_snapshot_valid != 0U) && (addr_snapshot == addr) ) {
        return PARAM_NO_CHANGE;
    }

    st = store->set(store_ctx, PARAM_KEY_DEV_ADDR, &addr, sizeof(addr));

    len = store->get(store_ctx, PARAM_KEY_DEV_ADDR, &verify, sizeof(verify));
    if ( (len != (int)sizeof(verify)) || (verify != addr) ) {
        elog_error("param", "commit failed: %s (kv %d, read back %d)",
                   PARAM_KEY_DEV_ADDR, st, len);
        (void)st;
        return PARAM_ERR_WRITE;
    }

    addr_snapshot       = addr;
    addr_snapshot_valid = 1U;
    elog_info("param", "commit ok: %s -> %u (kv %d)",
              PARAM_KEY_DEV_ADDR, (unsigned)addr, st);
    (void)st;
    return PARAM_OK;
}

void param_set_default_all(void)
{
    param_boot_result_t result;
    const kv_default_t* defaults;
    size_t count;

    if ( (db_ok == 0U) || (store == NULL) ) {
        return;
    }

    defaults = param_defaults(&count);
    if ( store->reset(store_ctx, defaults, count) != 0 ) {
        elog_error("param", "set_default failed");
        return;
    }

    /* Defaults are materialized now; adopt them as the committed state. */
    memset(&result, 0, sizeof(result));
    load_all(&result);
}
