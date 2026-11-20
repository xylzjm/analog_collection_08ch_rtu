#ifndef KV_STORE_BOARD_H
#define KV_STORE_BOARD_H

#include "kv_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Construct the board's key-value store handle.
 *
 * Installs the FlashDB/FAL operation table on the file-static handle; no
 * flash access happens here, the database opens in the store's init()
 * operation.
 * @note Defect handler: a failure means the built-in table is incomplete.
 */
void kv_store_board_ctor(void);

/** @brief Return the board's key-value store handle. */
kv_store_t* kv_store_board_get(void);

#ifdef __cplusplus
}
#endif

#endif
