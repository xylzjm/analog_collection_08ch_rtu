#ifndef COLD_JUNCTION_APP_H
#define COLD_JUNCTION_APP_H

#include "qpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return the cold-junction active object.
 *
 * @return The cold-junction AO, or NULL while the module is a stub.
 * @note Callers must skip every post when NULL.
 */
QActive* cold_junction_app_get(void);

/** @brief Construct the cold-junction stub; safe to call more than once. */
void cold_junction_app_init(void);

#ifdef __cplusplus
}
#endif

#endif
