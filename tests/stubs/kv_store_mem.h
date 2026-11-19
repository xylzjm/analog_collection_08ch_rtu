#ifndef KV_STORE_MEM_H
#define KV_STORE_MEM_H

/**
 * @file kv_store_mem.h
 * @brief In-memory kv_store implementation for the host tests.
 *
 * The real param module binds this store instead of the FlashDB adapter.
 * Fault flags simulate a failed init, a failed write and a read-back that
 * differs from the written value, so the EX 00 read-back invariant runs
 * under the host compiler.
 */

#include <stddef.h>
#include <stdint.h>

#include "kv_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Capacity of the test store. */
#define KV_STORE_MEM_MAX_ENTRIES 12U

/** @brief Key buffer size (keys are short: cfg/chN, dev/addr). */
#define KV_STORE_MEM_MAX_KEY 16U

/** @brief Value buffer size; ch_config_t is 20 bytes. */
#define KV_STORE_MEM_MAX_VALUE 32U

/** @brief One stored key/value pair. */
typedef struct {
    char key[KV_STORE_MEM_MAX_KEY];        /**< NUL-terminated key. */
    uint8_t value[KV_STORE_MEM_MAX_VALUE]; /**< Stored body. */
    size_t len;                            /**< Stored length in bytes. */
    uint8_t used;                          /**< Non-zero when the entry is occupied. */
} kv_store_mem_entry_t;

/** @brief In-memory store state and the test observation counters. */
typedef struct {
    kv_store_mem_entry_t entries[KV_STORE_MEM_MAX_ENTRIES]; /**< Stored pairs. */
    uint8_t fail_init;                                      /**< Script: init returns -1. */
    uint8_t fail_write;                                     /**< Script: set returns -1 and stores nothing. */
    uint8_t corrupt_read;                                   /**< Script: get returns a modified copy. */
    uint16_t init_calls;                                    /**< init invocations. */
    uint16_t get_calls;                                     /**< get invocations. */
    uint16_t set_calls;                                     /**< set invocations (not default materialization). */
    char last_key[KV_STORE_MEM_MAX_KEY];                    /**< Key of the last set. */
    uint8_t last_value[KV_STORE_MEM_MAX_VALUE];             /**< Body of the last set. */
    size_t last_len;                                        /**< Length of the last set. */
} kv_store_mem_t;

/** @brief Return the in-memory implementation of kv_store_ops_t. */
const kv_store_ops_t* kv_store_mem_ops(void);

/**
 * @brief Clear every entry, fault flag and counter.
 *
 * @param mem Store to reset; it need not be initialized.
 */
void kv_store_mem_reset(kv_store_mem_t* mem);

/**
 * @brief Preload one raw value without touching the observation counters.
 *
 * Used to script boot failures: preload a wrong-length body before
 * param_boot_load() and let the real loader reject it.
 *
 * @param mem Store to write.
 * @param key NUL-terminated key.
 * @param value Body to store.
 * @param len Body length; clamped to KV_STORE_MEM_MAX_VALUE.
 */
void kv_store_mem_put(kv_store_mem_t* mem,
                      const char* key,
                      const void* value,
                      size_t len);

/**
 * @brief Remove one key from the store.
 *
 * @param mem Store to modify.
 * @param key NUL-terminated key.
 */
void kv_store_mem_erase(kv_store_mem_t* mem,
                        const char* key);

#ifdef __cplusplus
}
#endif

#endif
