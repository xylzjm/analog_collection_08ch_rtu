#ifndef KV_STORE_FLASHDB_H
#define KV_STORE_FLASHDB_H

/**
 * @file kv_store_flashdb.h
 * @brief FlashDB/FAL implementation of the kv_store interface.
 */

#include "kv_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Return the target FlashDB/FAL implementation of kv_store_ops_t. */
const kv_store_ops_t* kv_store_flashdb_ops(void);

#ifdef __cplusplus
}
#endif

#endif
