#ifndef FLASHDB_H_
#define FLASHDB_H_

/**
 * @file flashdb.h
 * @brief Host shadow of the FlashDB header, only the tags param.h mentions.
 *
 * The real header pulls the whole middleware configuration; the active
 * object tests link tests/stubs/param_stub.c instead of app/component/
 * src/param.c, so only the incomplete type tags are needed for param.h
 * to compile. Found before ../middleware/flashDB/inc.
 */

struct fdb_kvdb;
struct fdb_default_kv;
struct fdb_default_kv_node;

#endif
