#ifndef FLASHDB_STUB_H
#define FLASHDB_STUB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FDB_NO_ERR 0

typedef int fdb_err_t;

/** @brief Opaque host-test KVDB; the store is kept in the stub module. */
struct fdb_kvdb {
    uint8_t unused;
};

/** @brief Blob view used by the FlashDB API. */
struct fdb_blob {
    void* buf;
    size_t size;
};

size_t fdb_blob_make(struct fdb_blob* blob, const void* value_buf, size_t buf_len);

size_t fdb_kv_get_blob(struct fdb_kvdb* db, const char* key, struct fdb_blob* blob);

fdb_err_t fdb_kv_set_blob(struct fdb_kvdb* db,
                          const char* key,
                          struct fdb_blob* blob);

fdb_err_t fdb_kvdb_init(struct fdb_kvdb* db,
                        const char* name,
                        const char* part_name,
                        void* default_kv,
                        void* user_data);

#ifdef __cplusplus
}
#endif

#endif
