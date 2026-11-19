#ifndef PARAM_DEFAULTS_H
#define PARAM_DEFAULTS_H

/**
 * @file param_defaults.h
 * @brief Factory default parameter table, independent of the storage backend.
 */

#include <stddef.h>

#include "kv_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Default KV nodes: the 8 board channel positions plus dev/addr. */
#define PARAM_DEFAULT_KV_COUNT 9U

/**
 * @brief Return the factory default table.
 *
 * @param count Receives the number of entries; may be NULL.
 * @return The static default table.
 */
const kv_default_t* param_defaults(size_t* count);

#ifdef __cplusplus
}
#endif

#endif
