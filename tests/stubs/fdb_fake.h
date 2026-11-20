#ifndef FDB_FAKE_H_
#define FDB_FAKE_H_

/**
 * @file fdb_fake.h
 * @brief In-memory fake of the four FlashDB entry points modbus_ao.c uses.
 *
 * Host tests link this instead of the FlashDB engine while compiling against
 * the real vendor headers: fdb_kvdb_init, fdb_kv_get_blob, fdb_kv_set_blob
 * and fdb_blob_make are supplied here at link time. Fault knobs simulate a
 * failed open, a failed write and a read-back copy that differs from the
 * stored bytes. If modbus_ao.c ever touches a fifth vendor behaviour the
 * fake must grow a scenario for it; test_fake_medium() pins the four.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Capacity of the fake medium. */
#define FDB_FAKE_MAX_ENTRIES 12U

/** @brief Key buffer size (keys are short: cfg/chN, dev/addr). */
#define FDB_FAKE_MAX_KEY 16U

/** @brief Value buffer size; ch_config_t is 20 bytes. */
#define FDB_FAKE_MAX_VALUE 32U

/** @brief Test knobs and observables of the fake medium. */
typedef struct {
    uint8_t  fail_init;                       /**< Script: init fails. */
    uint8_t  fail_write;                      /**< Script: set fails and stores nothing. */
    uint8_t  corrupt_read;                    /**< Script: get returns a modified copy. */
    uint16_t init_calls;                      /**< init invocations. */
    uint16_t set_calls;                       /**< set invocations (materialization is not a set). */
    char     last_key[FDB_FAKE_MAX_KEY];      /**< Key of the last set. */
    uint8_t  last_value[FDB_FAKE_MAX_VALUE];  /**< Body of the last set. */
    size_t   last_len;                        /**< Length of the last set. */
} fdb_fake_t;

/** @brief The one fake instance: knobs in, observations out. */
extern fdb_fake_t fdb_fake;

/**
 * @brief Discard the whole medium and every observation.
 *
 * Idempotent, so one call doubles as a scenario reset. A missing key is
 * expressed by simply not planting it; there is no delete.
 */
void fdb_fake_reset(void);

/**
 * @brief Preload one raw value without touching the observation counters.
 *
 * Used to script an existing store before it is opened (the medium is never
 * backfilled once it holds anything).
 *
 * @param key NUL-terminated key.
 * @param value Body to store.
 * @param len Body length; clamped to FDB_FAKE_MAX_VALUE.
 */
void fdb_fake_put(const char* key, const void* value, size_t len);

#ifdef __cplusplus
}
#endif

#endif
