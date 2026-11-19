/**
 * @file kv_store_mem.c
 * @brief In-memory kv_store implementation for the host tests.
 */

#include "kv_store_mem.h"

#include <string.h>

/** @brief Copy a key into a fixed buffer, truncating without overflow. */
static void copy_key(char* dst,
                     size_t cap,
                     const char* src)
{
    size_t i = 0U;

    while ( (i + 1U < cap) && (src[i] != '\0') ) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

/** @brief Find one entry by key; NULL when absent. */
static kv_store_mem_entry_t* find_entry(kv_store_mem_t* mem,
                                        const char* key)
{
    uint8_t i;

    for ( i = 0U; i < KV_STORE_MEM_MAX_ENTRIES; ++i ) {
        if ( (mem->entries[i].used != 0U)
             && (strcmp(mem->entries[i].key, key) == 0) ) {
            return &mem->entries[i];
        }
    }
    return NULL;
}

/** @brief Insert or overwrite one entry without touching the counters. */
static void put_entry(kv_store_mem_t* mem,
                      const char* key,
                      const void* value,
                      size_t len)
{
    kv_store_mem_entry_t* entry = find_entry(mem, key);
    uint8_t i;

    if ( entry == NULL ) {
        for ( i = 0U; i < KV_STORE_MEM_MAX_ENTRIES; ++i ) {
            if ( mem->entries[i].used == 0U ) {
                entry = &mem->entries[i];
                break;
            }
        }
    }
    if ( entry == NULL ) {
        return;
    }

    copy_key(entry->key, sizeof(entry->key), key);
    if ( len > sizeof(entry->value) ) {
        len = sizeof(entry->value);
    }
    memset(entry->value, 0, sizeof(entry->value));
    if ( len != 0U ) {
        memcpy(entry->value, value, len);
    }
    entry->len  = len;
    entry->used = 1U;
}

/** @brief Materialize every missing default key. */
static void materialize_defaults(kv_store_mem_t* mem,
                                 const kv_default_t* defaults,
                                 size_t count)
{
    size_t i;

    for ( i = 0U; i < count; ++i ) {
        if ( find_entry(mem, defaults[i].key) == NULL ) {
            put_entry(mem, defaults[i].key, defaults[i].value, defaults[i].len);
        }
    }
}

static int mem_init(void* ctx,
                    const kv_default_t* defaults,
                    size_t count)
{
    kv_store_mem_t* mem = (kv_store_mem_t*)ctx;

    ++mem->init_calls;
    if ( mem->fail_init != 0U ) {
        return -1;
    }

    materialize_defaults(mem, defaults, count);
    mem->initialized = 1U;
    return 0;
}

static int mem_get(void* ctx,
                   const char* key,
                   void* buf,
                   size_t cap)
{
    kv_store_mem_t* mem = (kv_store_mem_t*)ctx;
    kv_store_mem_entry_t* entry;
    size_t n;

    ++mem->get_calls;

    entry = find_entry(mem, key);
    if ( entry == NULL ) {
        return -1;
    }

    n = entry->len;
    if ( n > cap ) {
        n = cap;
    }
    if ( (n != 0U) && (buf != NULL) ) {
        memcpy(buf, entry->value, n);
        if ( mem->corrupt_read != 0U ) {
            ((uint8_t*)buf)[0] ^= 0x5AU;
        }
    }
    return (int)entry->len;
}

static int mem_set(void* ctx,
                   const char* key,
                   const void* buf,
                   size_t len)
{
    kv_store_mem_t* mem = (kv_store_mem_t*)ctx;

    ++mem->set_calls;
    copy_key(mem->last_key, sizeof(mem->last_key), key);
    if ( len > sizeof(mem->last_value) ) {
        len = sizeof(mem->last_value);
    }
    memset(mem->last_value, 0, sizeof(mem->last_value));
    if ( len != 0U ) {
        memcpy(mem->last_value, buf, len);
    }
    mem->last_len = len;

    if ( mem->fail_write != 0U ) {
        return -1;
    }

    put_entry(mem, key, buf, len);
    return 0;
}

static int mem_reset(void* ctx,
                     const kv_default_t* defaults,
                     size_t count)
{
    kv_store_mem_t* mem = (kv_store_mem_t*)ctx;

    ++mem->reset_calls;
    memset(mem->entries, 0, sizeof(mem->entries));
    materialize_defaults(mem, defaults, count);
    mem->initialized = 1U;
    return 0;
}

/** @brief The one implementation table; ctx is the kv_store_mem_t. */
static const kv_store_ops_t ops = {
    &mem_init,
    &mem_get,
    &mem_set,
    &mem_reset,
};

const kv_store_ops_t* kv_store_mem_ops(void)
{
    return &ops;
}

void kv_store_mem_reset(kv_store_mem_t* mem)
{
    memset(mem, 0, sizeof(*mem));
}

void kv_store_mem_put(kv_store_mem_t* mem,
                      const char* key,
                      const void* value,
                      size_t len)
{
    put_entry(mem, key, value, len);
}

void kv_store_mem_erase(kv_store_mem_t* mem,
                        const char* key)
{
    kv_store_mem_entry_t* entry = find_entry(mem, key);

    if ( entry != NULL ) {
        memset(entry, 0, sizeof(*entry));
    }
}
