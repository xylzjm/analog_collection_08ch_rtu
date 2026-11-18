#ifndef COLD_JUNCTION_AO_H
#define COLD_JUNCTION_AO_H

/**
 * @file cold_junction_ao.h
 * @brief Cold junction temperature active object (TMP75).
 *
 * Sole owner of the TMP75 and the shared soft I2C bus; publishes the CJC_SIG
 * heartbeat once per second. See docs/ao/cold_junction_ao.md.
 */

#include "qpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Queue depth (1 Hz tick plus margin). */
#define COLD_JUNCTION_AO_QUEUE_LEN 4U

/** @brief Lowest plausible cold junction temperature. */
#define COLD_JUNCTION_AO_MIN_TEMP_C (-30.0f)

/** @brief Highest plausible cold junction temperature. */
#define COLD_JUNCTION_AO_MAX_TEMP_C 85.0f

/** @brief Consecutive failures that invalidate the reading. */
#define COLD_JUNCTION_AO_ERR_LIMIT 3U

/** @brief Cold junction heartbeat period in ticks. */
#define COLD_JUNCTION_AO_TICK_MS 1000U

/** @brief AoColdJunction active object. */
typedef struct {
    QActive  super;     /**< Active object base. */
    QTimeEvt te_tick;   /**< 1 Hz heartbeat timer. */
    float    temp_c;    /**< Latest valid temperature. */
    uint8_t  err_cnt;   /**< Consecutive read failures, saturating. */
    uint8_t  have_reading; /**< Non-zero once a reading succeeded. */
} cold_junction_ao_t;

/**
 * @brief Construct the cold junction active object.
 *
 * @param me Storage owned by the caller.
 */
void cold_junction_ao_ctor(cold_junction_ao_t* me);

#ifdef __cplusplus
}
#endif

#endif
