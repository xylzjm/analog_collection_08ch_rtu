#ifndef KV_STORE_H
#define KV_STORE_H

/**
 * @file kv_store.h
 * @brief Key-value store adapter interface used by the param component.
 *
 * param depends on this interface only; no FlashDB type crosses it. The
 * target implementation (kv_store_flashdb.c) sits on FlashDB/FAL, while the
 * host tests bind an in-memory implementation to run the real param module.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One factory-default key/value pair. */
typedef struct {
    const char* key;   /**< NUL-terminated key name. */
    const void* value; /**< Default value body. */
    size_t len;        /**< Value length in bytes. */
} kv_default_t;

/**
 * @brief KV store operations.
 *
 * Every operation runs in the caller's execution context (the AoModbus RTC
 * step); implementations add no locking of their own.
 */
typedef struct {
    /**
     * @brief Open the store and materialize missing default keys.
     *
     * @param ctx Implementation context; may be NULL.
     * @param defaults Default table; may be NULL when count is 0.
     * @param count Number of entries in @p defaults.
     * @return 0 on success, a negative value on failure.
     */
    int (*init)(void* ctx, const kv_default_t* defaults, size_t count);

    /**
     * @brief Read one value.
     *
     * @param ctx Implementation context; may be NULL.
     * @param key NUL-terminated key name.
     * @param buf Destination buffer.
     * @param cap Destination capacity in bytes.
     * @return The stored length (which may exceed @p cap, in which case the
     *         copy is truncated), or a negative value when the key is missing
     *         or the read failed.
     */
    int (*get)(void* ctx, const char* key, void* buf, size_t cap);

    /**
     * @brief Write one value.
     *
     * @param ctx Implementation context; may be NULL.
     * @param key NUL-terminated key name.
     * @param buf Value body.
     * @param len Value length in bytes.
     * @return 0 on success, a negative value on failure.
     */
    int (*set)(void* ctx, const char* key, const void* buf, size_t len);
} kv_store_ops_t;

#ifdef __cplusplus
}
#endif

#endif
