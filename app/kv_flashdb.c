/**
 * @file kv_flashdb.c
 * @brief FlashDB/FAL implementation of the modbus_kv_ops_t seam.
 *
 * This is the only application file that touches FlashDB. The database is
 * opened during modbus_ao_ctor() (before QActive_start() and QF_run()) and
 * every access runs in the AoModbus RTC context.
 */

#include "kv_flashdb.h"

#include "elog.h"
#include "flashdb.h"

/** @brief FAL partition that backs the key-value database. */
#define KV_FLASHDB_PATH "fdb_kvdb"

/** @brief FlashDB database name (diagnostics only). */
#define KV_FLASHDB_NAME "param"

/** @brief The single KV database instance owned by this adapter. */
static struct fdb_kvdb kvdb;

/** @brief Default nodes handed to fdb_kvdb_init(). */
static struct fdb_default_kv_node default_nodes[APP_BOARD_POSITION_COUNT + 1U];

/** @brief Default table wrapper handed to fdb_kvdb_init(). */
static struct fdb_default_kv default_kv;

static int flashdb_init(void* ctx,
                        const modbus_kv_default_t* defaults,
                        size_t count)
{
    size_t i;

    (void)ctx;

    if ( count > (sizeof(default_nodes) / sizeof(default_nodes[0])) ) {
        elog_error("kv_flashdb", "too many defaults (%u)", (unsigned)count);
        return -1;
    }

    for ( i = 0U; i < count; ++i ) {
        default_nodes[i].key       = (char*)defaults[i].key;
        default_nodes[i].value     = (void*)defaults[i].value;
        default_nodes[i].value_len = defaults[i].len;
    }
    default_kv.kvs = default_nodes;
    default_kv.num = count;

    if ( fal_init() <= 0 ) {
        elog_error("kv_flashdb", "fal_init failed");
        return -1;
    }
    if ( fdb_kvdb_init(&kvdb, KV_FLASHDB_NAME, KV_FLASHDB_PATH,
                       &default_kv, NULL)
         != FDB_NO_ERR ) {
        elog_error("kv_flashdb", "fdb_kvdb_init failed");
        return -1;
    }
    return 0;
}

static int flashdb_get(void* ctx,
                       const char* key,
                       void* buf,
                       size_t cap)
{
    struct fdb_blob blob;

    (void)ctx;

    blob.saved.len = 0U;
    (void)fdb_kv_get_blob(&kvdb, key, fdb_blob_make(&blob, buf, cap));
    if ( blob.saved.len == 0U ) {
        return -1;
    }
    return (int)blob.saved.len;
}

static int flashdb_set(void* ctx,
                       const char* key,
                       const void* buf,
                       size_t len)
{
    struct fdb_blob blob;

    (void)ctx;

    if ( fdb_kv_set_blob(&kvdb, key, fdb_blob_make(&blob, buf, len))
         != FDB_NO_ERR ) {
        return -1;
    }
    return 0;
}

/** @brief The one implementation table; ctx is unused. */
static const modbus_kv_ops_t ops = {
    &flashdb_init,
    &flashdb_get,
    &flashdb_set,
};

const modbus_kv_ops_t* kv_flashdb_ops(void)
{
    return &ops;
}
