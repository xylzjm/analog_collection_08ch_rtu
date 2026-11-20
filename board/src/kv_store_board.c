/**
 * @file kv_store_board.c
 * @brief FlashDB/FAL implementation of the kv_store interface.
 *
 * The composition root builds the handle with kv_store_board_ctor(); the
 * database opens when the consumer runs the init operation (during
 * modbus_ao_ctor(), before QActive_start() and QF_run()) and every access
 * runs in the AoModbus RTC context.
 */

#include "kv_store_board.h"

#include "app.h"
#include "elog.h"
#include "flashdb.h"
#include "qpc.h"

Q_DEFINE_THIS_MODULE("kv_store_board")

/** @brief FAL partition that backs the key-value database. */
#define KV_STORE_BOARD_PATH "fdb_kvdb"

/** @brief FlashDB database name (diagnostics only). */
#define KV_STORE_BOARD_NAME "param"

/** @brief The single KV database instance owned by this adapter. */
static struct fdb_kvdb kvdb;

/** @brief Default nodes handed to fdb_kvdb_init(). */
static struct fdb_default_kv_node default_nodes[APP_BOARD_POSITION_COUNT + 1U];

/** @brief Default table wrapper handed to fdb_kvdb_init(). */
static struct fdb_default_kv default_kv;

/** @brief The board's store handle; built once, read-only afterwards. */
static kv_store_t store;

static kv_store_status_t flashdb_init(kv_store_t* self,
                                      const kv_store_default_t* defaults,
                                      size_t count)
{
    size_t i;

    (void)self;

    if ( count > (sizeof(default_nodes) / sizeof(default_nodes[0])) ) {
        elog_error("kv_store_board", "too many defaults (%u)", (unsigned)count);
        return KV_STORE_ERR_IO;
    }

    for ( i = 0U; i < count; ++i ) {
        default_nodes[i].key       = (char*)defaults[i].key;
        default_nodes[i].value     = (void*)defaults[i].value;
        default_nodes[i].value_len = defaults[i].len;
    }
    default_kv.kvs = default_nodes;
    default_kv.num = count;

    if ( fal_init() <= 0 ) {
        elog_error("kv_store_board", "fal_init failed");
        return KV_STORE_ERR_IO;
    }
    if ( fdb_kvdb_init(&kvdb, KV_STORE_BOARD_NAME, KV_STORE_BOARD_PATH,
                       &default_kv, NULL)
         != FDB_NO_ERR ) {
        elog_error("kv_store_board", "fdb_kvdb_init failed");
        return KV_STORE_ERR_IO;
    }
    return KV_STORE_OK;
}

static kv_store_status_t flashdb_get(kv_store_t* self,
                                     const char* key,
                                     void* buf,
                                     size_t cap,
                                     size_t* stored_len)
{
    struct fdb_blob blob;

    (void)self;

    blob.saved.len = 0U;
    (void)fdb_kv_get_blob(&kvdb, key, fdb_blob_make(&blob, buf, cap));
    if ( blob.saved.len == 0U ) {
        return KV_STORE_ERR_IO;
    }
    *stored_len = blob.saved.len;
    return KV_STORE_OK;
}

static kv_store_status_t flashdb_set(kv_store_t* self,
                                     const char* key,
                                     const void* buf,
                                     size_t len)
{
    struct fdb_blob blob;

    (void)self;

    if ( fdb_kv_set_blob(&kvdb, key, fdb_blob_make(&blob, buf, len))
         != FDB_NO_ERR ) {
        return KV_STORE_ERR_IO;
    }
    return KV_STORE_OK;
}

/** @brief The one implementation table; self is ignored (file-static state). */
static const kv_store_ops_t ops = {
    &flashdb_init,
    &flashdb_get,
    &flashdb_set,
};

void kv_store_board_ctor(void)
{
    /* A failure means the table above is incomplete: composition defect. */
    Q_ASSERT(kv_store_ctor(&store, &ops) == KV_STORE_OK);
}

kv_store_t* kv_store_board_get(void)
{
    return &store;
}
