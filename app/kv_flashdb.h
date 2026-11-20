#ifndef KV_FLASHDB_H
#define KV_FLASHDB_H

/**
 * @file kv_flashdb.h
 * @brief Target FlashDB/FAL implementation of the modbus_kv_ops_t seam.
 *
 * The composition root binds this store in modbus_ao_ctor(); the host
 * tests bind tests/stubs/kv_mem.c instead. Implemented in app/kv_flashdb.c.
 */

#include "modbus_ao.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return the target FlashDB/FAL implementation of modbus_kv_ops_t.
 *
 * Implemented in app/kv_flashdb.c.
 */
const modbus_kv_ops_t* kv_flashdb_ops(void);

#ifdef __cplusplus
}
#endif

#endif
