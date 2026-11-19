#ifndef PARAM_H
#define PARAM_H

/**
 * @file param.h
 * @brief Parameter store: persistence with synchronous commit.
 *
 * Passive component, no autonomous behavior. Storage sits behind the
 * kv_store interface (kv_store.h): the target binds the FlashDB/FAL adapter,
 * the host tests bind an in-memory implementation. Every storage call runs
 * in the AoModbus execution context (initial transition and the
 * holding-register write callback), so access is serialized by the QV
 * kernel. The commit runs synchronously inside the write callback: the
 * Modbus reply is decided by a read-back check, never by the storage return
 * code alone.
 */

#include <stdint.h>

#include "ch_config.h"
#include "kv_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result of one parameter save. */
typedef enum {
    PARAM_OK        = 0, /**< Read-back equals the requested value; committed. */
    PARAM_NO_CHANGE = 1, /**< Same value as the snapshot; store untouched. */
    PARAM_ERR_WRITE = -1 /**< Read-back differs; treat as not committed. */
} param_status_t;

/** @brief Outcome of param_boot_load(): what is usable and what failed. */
typedef struct {
    ch_config_t cfg[CH_CONFIG_CHANNEL_COUNT];         /**< Loaded configurations. */
    uint8_t cfg_valid[CH_CONFIG_CHANNEL_COUNT];       /**< 1 when cfg[ch] is usable. */
    uint16_t dev_addr;                                /**< Slave address to run with (1 on failure). */
    uint8_t db_ok;                                    /**< 1 when the kv_store init succeeded. */
    uint8_t cfg_load_failed[CH_CONFIG_CHANNEL_COUNT]; /**< Per-channel load failure. */
    uint8_t addr_load_failed;                         /**< dev/addr load failure (default used). */
} param_boot_result_t;

/** @brief Default KV nodes: the 8 board positions plus dev/addr. */
#define PARAM_DEFAULT_KV_COUNT 9U

/**
 * @brief Bind the storage backend used by every param operation.
 *
 * Must run before param_boot_load(); the composition root binds the
 * FlashDB adapter, host tests bind an in-memory store.
 *
 * @param ops Implementation table; NULL leaves the store unbound.
 * @param ctx Implementation context handed to every operation.
 */
void param_bind_store(const kv_store_ops_t* ops,
                      void* ctx);

/**
 * @brief Open the storage backend and load every parameter key.
 *
 * Missing, corrupt or semantically invalid keys are reported as load
 * failures and are never replaced by defaults: the caller must leave the
 * affected channel unconfigured. A completely empty store is the factory
 * first boot: the default table materializes and loading succeeds.
 *
 * @param result Receives the loaded parameters and the failure flags; the
 *        caller keeps the flags for the device fault word (b2/b3).
 */
void param_boot_load(param_boot_result_t* result);

/**
 * @brief Persist one channel configuration synchronously.
 *
 * Change check against the snapshot first; a changed value is written and
 * then read back. The outcome is decided by the read-back comparison.
 *
 * @param ch Zero-based channel number.
 * @param cfg Validated, normalized configuration to commit.
 * @return PARAM_OK (read-back equals the request, snapshot updated),
 *         PARAM_NO_CHANGE (snapshot already holds this value, no write), or
 *         PARAM_ERR_WRITE (read-back differs, snapshot untouched).
 */
param_status_t param_save_cfg(uint8_t ch,
                              const ch_config_t* cfg);

/**
 * @brief Persist the slave address synchronously.
 *
 * @param addr Address to commit (1..247; validated by the caller).
 * @return Same codes as param_save_cfg().
 */
param_status_t param_save_addr(uint16_t addr);

#ifdef __cplusplus
}
#endif

#endif
