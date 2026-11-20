/**
 * @file fdb_fake.c
 * @brief In-memory fake of the four FlashDB entry points modbus_ao.c uses.
 *
 * The behaviours mirror the vendor engine (fdb_blob_make leaves saved
 * alone, get reports the full stored length and copies min(len, size), set
 * counts before honouring fail_write, init materializes the default table
 * only into an empty medium and reads a zero-length row as a C string, a
 * closed database answers under the vendor's db_init_ok guards, and a NULL
 * body deletes the key); test_fake_medium() pins them. Unmodelled: the real
 * set_kv can answer FDB_SAVED_FULL under GC pressure; the fake always
 * succeeds unless fail_write (wire behavior is unaffected because
 * param_commit decides on read-back).
 */

#include "fdb_fake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flashdb.h"

/** @brief One stored key/value pair; opaque here, the medium's entry. */
typedef struct {
    char    key[FDB_FAKE_MAX_KEY];        /**< NUL-terminated key. */
    uint8_t value[FDB_FAKE_MAX_VALUE];    /**< Stored body. */
    size_t  len;                          /**< Stored length in bytes. */
    uint8_t used;                         /**< Non-zero when the entry is occupied. */
} medium_entry_t;

/** @brief The one medium; reached only through the fake API. */
static medium_entry_t entries[FDB_FAKE_MAX_ENTRIES];

/** @brief Open flag: set by a successful init, cleared by reset and fail_init. */
static uint8_t open_ok;

fdb_fake_t fdb_fake;

/** @brief Abort the test run; the fake never degrades silently. */
static void fake_die(const char* msg)
{
    fprintf(stderr, "fdb_fake: %s\n", msg);
    abort();
}

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
static medium_entry_t* find_entry(const char* key)
{
    uint8_t i;

    for ( i = 0U; i < FDB_FAKE_MAX_ENTRIES; ++i ) {
        if ( (entries[i].used != 0U)
             && (strcmp(entries[i].key, key) == 0) ) {
            return &entries[i];
        }
    }
    return NULL;
}

/** @brief Insert or overwrite one entry without touching the counters. */
static void put_entry(const char* key,
                      const void* value,
                      size_t len)
{
    medium_entry_t* entry = find_entry(key);
    uint8_t i;

    if ( entry == NULL ) {
        for ( i = 0U; i < FDB_FAKE_MAX_ENTRIES; ++i ) {
            if ( entries[i].used == 0U ) {
                entry = &entries[i];
                break;
            }
        }
    }
    if ( entry == NULL ) {
        fake_die("medium full: raise FDB_FAKE_MAX_ENTRIES");
    }
    if ( len > sizeof(entry->value) ) {
        fake_die("body longer than FDB_FAKE_MAX_VALUE");
    }

    copy_key(entry->key, sizeof(entry->key), key);
    memset(entry->value, 0, sizeof(entry->value));
    if ( len != 0U ) {
        memcpy(entry->value, value, len);
    }
    entry->len  = len;
    entry->used = 1U;
}

/** @brief Remove one entry by key; a missing key is a no-op. */
static void drop_entry(const char* key)
{
    medium_entry_t* entry = find_entry(key);

    if ( entry != NULL ) {
        memset(entry, 0, sizeof(*entry));
    }
}

/** @brief Report whether the medium holds no entries. */
static int store_empty(void)
{
    uint8_t i;

    for ( i = 0U; i < FDB_FAKE_MAX_ENTRIES; ++i ) {
        if ( entries[i].used != 0U ) {
            return 0;
        }
    }
    return 1;
}

fdb_blob_t fdb_blob_make(fdb_blob_t blob, const void *value_buf, size_t buf_len)
{
    /* Rule: only buf/size; the saved block stays as the caller left it. */
    blob->buf  = (void*)value_buf;
    blob->size = buf_len;
    return blob;
}

size_t fdb_kv_get_blob(fdb_kvdb_t db, const char *key, fdb_blob_t blob)
{
    medium_entry_t* entry;
    size_t n;

    (void)db;

    /* Rule: a closed database answers 0 without touching saved.len
     * (fdb_kvdb.c fdb_kv_get_blob). */
    if ( open_ok == 0U ) {
        return 0U;
    }

    entry = find_entry(key);
    if ( entry == NULL ) {
        blob->saved.len = 0U;
        return 0U;
    }

    /* Rule: saved.len is the FULL stored length even on a truncated copy. */
    blob->saved.len = entry->len;
    n = (entry->len < blob->size) ? entry->len : blob->size;
    if ( (n != 0U) && (blob->buf != NULL) ) {
        memcpy(blob->buf, entry->value, n);
        if ( fdb_fake.corrupt_read != 0U ) {
            ((uint8_t*)blob->buf)[0] ^= 0x5AU;
        }
    }
    return n;
}

fdb_err_t fdb_kv_set_blob(fdb_kvdb_t db, const char *key, fdb_blob_t blob)
{
    size_t len = blob->size;

    (void)db;

    /* Rule: a closed database rejects the call before any attempt is
     * recorded (fdb_kvdb.c fdb_kv_set_blob). */
    if ( open_ok == 0U ) {
        return FDB_INIT_FAILED;
    }

    /* Rule: set_kv ignores the length of a NULL body and deletes the key
     * (fdb_kvdb.c set_kv); an over-length body fails loudly, never clamps. */
    if ( blob->buf == NULL ) {
        len = 0U;
    } else if ( len > FDB_FAKE_MAX_VALUE ) {
        fake_die("body longer than FDB_FAKE_MAX_VALUE");
    }

    /* Rule: the attempt is counted and recorded before fail_write can
     * reject it - the consumer decides on the read-back, not the code. */
    ++fdb_fake.set_calls;
    copy_key(fdb_fake.last_key, sizeof(fdb_fake.last_key), key);
    memset(fdb_fake.last_value, 0, sizeof(fdb_fake.last_value));
    if ( len != 0U ) {
        memcpy(fdb_fake.last_value, blob->buf, len);
    }
    fdb_fake.last_len = len;

    if ( fdb_fake.fail_write != 0U ) {
        return FDB_WRITE_ERR;
    }
    if ( blob->buf == NULL ) {
        drop_entry(key);
        return FDB_NO_ERR;
    }
    put_entry(key, blob->buf, len);
    return FDB_NO_ERR;
}

fdb_err_t fdb_kvdb_init(fdb_kvdb_t db, const char *name, const char *path,
        struct fdb_default_kv *default_kv, void *user_data)
{
    size_t i;
    size_t len;

    (void)db;
    (void)name;
    (void)path;
    (void)user_data;

    ++fdb_fake.init_calls;
    if ( fdb_fake.fail_init != 0U ) {
        open_ok = 0U;
        return FDB_INIT_FAILED;
    }
    open_ok = 1U;

    /* Rule: factory-boot materialization - the whole default table lands
     * only in an empty medium, never backfilling or overwriting, a
     * zero-length row is a C string (fdb_kvdb.c fdb_kv_set_default), and
     * materialization is not a set. */
    if ( (default_kv != NULL) && (default_kv->kvs != NULL) && store_empty() ) {
        for ( i = 0U; i < default_kv->num; ++i ) {
            len = default_kv->kvs[i].value_len;
            if ( len == 0U ) {
                len = strlen((const char*)default_kv->kvs[i].value);
            }
            put_entry(default_kv->kvs[i].key, default_kv->kvs[i].value, len);
        }
    }
    return FDB_NO_ERR;
}

void fdb_fake_reset(void)
{
    memset(&fdb_fake, 0, sizeof(fdb_fake));
    memset(entries, 0, sizeof(entries));
    open_ok = 0U;
}

void fdb_fake_put(const char* key, const void* value, size_t len)
{
    put_entry(key, value, len);
}
