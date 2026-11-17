/**
 * @file param.c
 * @brief Parameter store: synchronous commit with write-then-read-back check.
 */

#include "param.h"

#include <string.h>

#include "elog.h"
#include "reg_map.h"

/** @brief FAL partition that backs the key-value database. */
#define PARAM_FDB_PATH "fdb_kvdb"

/** @brief FlashDB database name (diagnostics only). */
#define PARAM_FDB_NAME "param"

/** @brief KV key of the slave address. */
#define PARAM_KEY_DEV_ADDR "dev/addr"

/** @brief The single KV database instance owned by this module. */
static struct fdb_kvdb kvdb;

/** @brief KV keys of the per-channel configurations. */
static const char* const cfg_keys[] = {
    "cfg/ch1", "cfg/ch2", "cfg/ch3", "cfg/ch4",
    "cfg/ch5", "cfg/ch6", "cfg/ch7", "cfg/ch8"
};

/** @brief Last committed configuration per channel (change check base). */
static ch_config_t snapshot[CH_CONFIG_CHANNEL_COUNT];

/** @brief Non-zero when snapshot[ch] mirrors a committed configuration. */
static uint8_t snapshot_valid[CH_CONFIG_CHANNEL_COUNT];

/** @brief Last committed slave address. */
static uint16_t addr_snapshot;

/** @brief Non-zero when addr_snapshot mirrors a committed address. */
static uint8_t addr_snapshot_valid;

/** @brief Non-zero when FAL and the KV database came up. */
static uint8_t db_ok;

/**
 * @brief Read one configuration key and validate its body.
 *
 * @param ch Zero-based channel number.
 * @param cfg Receives the validated, normalized configuration.
 * @return true when the key exists, has the expected size and validates.
 */
static bool load_cfg(uint8_t ch, ch_config_t* cfg)
{
    struct fdb_blob blob;
    size_t len;

    len = fdb_kv_get_blob(&kvdb, cfg_keys[ch],
                          fdb_blob_make(&blob, cfg, sizeof(*cfg)));
    if ( (len != sizeof(*cfg)) || (blob.saved.len != sizeof(*cfg)) ) {
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
    struct fdb_blob blob;
    size_t len;

    len = fdb_kv_get_blob(&kvdb, PARAM_KEY_DEV_ADDR,
                          fdb_blob_make(&blob, addr, sizeof(*addr)));
    if ( (len != sizeof(*addr)) || (blob.saved.len != sizeof(*addr)) ) {
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
            result->cfg[ch]        = cfg;
            result->cfg_valid[ch]  = 1U;
            snapshot[ch]           = cfg;
            snapshot_valid[ch]     = 1U;
        }
        else {
            result->cfg_load_failed[ch] = 1U;
            snapshot_valid[ch]          = 0U;
            elog_warn("param", "load failed: %s", cfg_keys[ch]);
        }
    }

    if ( load_addr(&result->dev_addr) ) {
        addr_snapshot       = result->dev_addr;
        addr_snapshot_valid = 1U;
    }
    else {
        result->dev_addr        = REG_MAP_DEV_ADDR_MIN;
        result->addr_load_failed = 1U;
        addr_snapshot_valid     = 0U;
        elog_warn("param", "load failed: %s", PARAM_KEY_DEV_ADDR);
    }
}

void param_boot_load(param_boot_result_t* result)
{
    memset(result, 0, sizeof(*result));
    result->dev_addr = REG_MAP_DEV_ADDR_MIN;

    if ( fal_init() <= 0 ) {
        elog_error("param", "fal_init failed");
        return;
    }
    if ( fdb_kvdb_init(&kvdb, PARAM_FDB_NAME, PARAM_FDB_PATH,
                       &param_default_kv, NULL) != FDB_NO_ERR ) {
        elog_error("param", "fdb_kvdb_init failed");
        return;
    }

    db_ok      = 1U;
    result->db_ok = 1U;
    load_all(result);
}

param_status_t param_save_cfg(uint8_t ch,
                              const ch_config_t* cfg)
{
    struct fdb_blob blob;
    ch_config_t verify;
    fdb_err_t err;
    size_t len;

    if ( (cfg == NULL) || (ch >= CH_CONFIG_CHANNEL_COUNT) || (db_ok == 0U) ) {
        return PARAM_ERR_WRITE;
    }

    if ( (snapshot_valid[ch] != 0U)
         && (memcmp(&snapshot[ch], cfg, sizeof(*cfg)) == 0) ) {
        return PARAM_NO_CHANGE;
    }

    err = fdb_kv_set_blob(&kvdb, cfg_keys[ch],
                          fdb_blob_make(&blob, cfg, sizeof(*cfg)));

    /* The reply follows the flash content, not the library return code. */
    len = fdb_kv_get_blob(&kvdb, cfg_keys[ch],
                          fdb_blob_make(&blob, &verify, sizeof(verify)));
    if ( (len != sizeof(verify)) || (blob.saved.len != sizeof(verify))
         || (memcmp(&verify, cfg, sizeof(verify)) != 0) ) {
        elog_error("param", "commit failed: %s (fdb %d, read back %u)",
                   cfg_keys[ch], (int)err, (unsigned)len);
        return PARAM_ERR_WRITE;
    }

    snapshot[ch]    = *cfg;
    snapshot_valid[ch] = 1U;
    elog_info("param", "commit ok: %s (fdb %d)", cfg_keys[ch], (int)err);
    return PARAM_OK;
}

param_status_t param_save_addr(uint16_t addr)
{
    struct fdb_blob blob;
    uint16_t verify;
    fdb_err_t err;
    size_t len;

    if ( (db_ok == 0U) || !reg_map_dev_addr_valid(addr) ) {
        return PARAM_ERR_WRITE;
    }

    if ( (addr_snapshot_valid != 0U) && (addr_snapshot == addr) ) {
        return PARAM_NO_CHANGE;
    }

    err = fdb_kv_set_blob(&kvdb, PARAM_KEY_DEV_ADDR,
                          fdb_blob_make(&blob, &addr, sizeof(addr)));

    len = fdb_kv_get_blob(&kvdb, PARAM_KEY_DEV_ADDR,
                          fdb_blob_make(&blob, &verify, sizeof(verify)));
    if ( (len != sizeof(verify)) || (blob.saved.len != sizeof(verify))
         || (verify != addr) ) {
        elog_error("param", "commit failed: %s (fdb %d, read back %u)",
                   PARAM_KEY_DEV_ADDR, (int)err, (unsigned)len);
        return PARAM_ERR_WRITE;
    }

    addr_snapshot       = addr;
    addr_snapshot_valid = 1U;
    elog_info("param", "commit ok: %s -> %u (fdb %d)",
              PARAM_KEY_DEV_ADDR, (unsigned)addr, (int)err);
    return PARAM_OK;
}

void param_set_default_all(void)
{
    param_boot_result_t result;

    if ( db_ok == 0U ) {
        return;
    }

    if ( fdb_kv_set_default(&kvdb) != FDB_NO_ERR ) {
        elog_error("param", "set_default failed");
        return;
    }

    /* Defaults are materialized now; adopt them as the committed state. */
    memset(&result, 0, sizeof(result));
    load_all(&result);
}
