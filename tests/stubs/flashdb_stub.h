#ifndef FLASHDB_STUB_H_
#define FLASHDB_STUB_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Clear the in-memory FlashDB store and its failure flag. */
void stub_fdb_reset(void);

/** @brief Remove one key; returns false when it was absent. */
bool stub_fdb_remove(const char* key);

/** @brief Make the next writes fail; reads still work. */
extern bool stub_fdb_write_fail;

#ifdef __cplusplus
}
#endif

#endif
