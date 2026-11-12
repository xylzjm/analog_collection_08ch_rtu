#include "flashdb_stub.h"

#include <string.h>

#include "fal.h"
#include "flashdb.h"

#define STUB_KV_MAX      32
#define STUB_KV_NAME_LEN 16
#define STUB_KV_DATA_LEN 128

typedef struct {
    char key[STUB_KV_NAME_LEN];
    uint8_t data[STUB_KV_DATA_LEN];
    size_t len;
    bool used;
} stub_kv_t;

static stub_kv_t store[STUB_KV_MAX];

bool stub_fdb_write_fail;

static stub_kv_t* find_key(const char* key)
{
    uint8_t index;

    for ( index = 0U; index < STUB_KV_MAX; ++index ) {
        if ( store[index].used && (strcmp(store[index].key, key) == 0) ) {
            return &store[index];
        }
    }
    return NULL;
}

static stub_kv_t* alloc_key(const char* key)
{
    uint8_t index;

    for ( index = 0U; index < STUB_KV_MAX; ++index ) {
        if ( store[index].used == false ) {
            store[index].used = true;
            (void)strncpy(store[index].key, key, STUB_KV_NAME_LEN - 1U);
            store[index].key[STUB_KV_NAME_LEN - 1U] = '\0';
            store[index].len = 0U;
            return &store[index];
        }
    }
    return NULL;
}

void stub_fdb_reset(void)
{
    memset(store, 0, sizeof(store));
    stub_fdb_write_fail = false;
}

bool stub_fdb_remove(const char* key)
{
    stub_kv_t* kv = find_key(key);

    if ( kv != NULL ) {
        kv->used = false;
        return true;
    }
    return false;
}

int fal_init(void)
{
    return 1;
}

size_t fdb_blob_make(struct fdb_blob* blob, const void* value_buf, size_t buf_len)
{
    if ( blob == NULL ) {
        return 0U;
    }
    blob->buf  = (void*)value_buf;
    blob->size = buf_len;
    return buf_len;
}

size_t fdb_kv_get_blob(struct fdb_kvdb* db, const char* key, struct fdb_blob* blob)
{
    stub_kv_t* kv;

    (void)db;
    if ( key == NULL || blob == NULL ) {
        return 0U;
    }
    kv = find_key(key);
    if ( kv == NULL ) {
        return 0U;
    }
    if ( blob->buf != NULL && kv->len <= blob->size ) {
        memcpy(blob->buf, kv->data, kv->len);
    }
    return kv->len;
}

fdb_err_t fdb_kv_set_blob(struct fdb_kvdb* db, const char* key, struct fdb_blob* blob)
{
    stub_kv_t* kv;

    (void)db;
    if ( stub_fdb_write_fail || key == NULL || blob == NULL
         || blob->buf == NULL || blob->size > STUB_KV_DATA_LEN ) {
        return -1;
    }
    kv = find_key(key);
    if ( kv == NULL ) {
        kv = alloc_key(key);
    }
    if ( kv == NULL ) {
        return -1;
    }
    memcpy(kv->data, blob->buf, blob->size);
    kv->len = blob->size;
    return FDB_NO_ERR;
}

fdb_err_t fdb_kvdb_init(struct fdb_kvdb* db,
                        const char* name,
                        const char* part_name,
                        void* default_kv,
                        void* user_data)
{
    (void)db;
    (void)name;
    (void)part_name;
    (void)default_kv;
    (void)user_data;
    return FDB_NO_ERR;
}
