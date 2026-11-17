#ifndef AO_COLD_JUNCTION_H
#define AO_COLD_JUNCTION_H

/**
 * @file ao_cold_junction.h
 * @brief Cold junction temperature active object (TMP75).
 *
 * Sole owner of the TMP75 and the shared soft I2C bus; publishes the CJC_SIG
 * heartbeat once per second. See docs/ao/ao_cold_junction.md.
 */

#include "qpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Queue depth (1 Hz tick plus margin). */
#define AO_COLD_JUNCTION_QUEUE_LEN 4U

/** @brief Lowest plausible cold junction temperature. */
#define AO_COLD_JUNCTION_MIN_TEMP_C (-30.0f)

/** @brief Highest plausible cold junction temperature. */
#define AO_COLD_JUNCTION_MAX_TEMP_C 85.0f

/** @brief Consecutive failures that invalidate the reading. */
#define AO_COLD_JUNCTION_ERR_LIMIT 3U

/** @brief Cold junction heartbeat period in ticks. */
#define AO_COLD_JUNCTION_TICK_MS 1000U

/** @brief AoColdJunction active object. */
typedef struct {
    QActive  super;     /**< Active object base. */
    QTimeEvt te_tick;   /**< 1 Hz heartbeat timer. */
    float    temp_c;    /**< Latest valid temperature. */
    uint8_t  err_cnt;   /**< Consecutive read failures, saturating. */
    uint8_t  have_reading; /**< Non-zero once a reading succeeded. */
} ao_cold_junction_t;

/**
 * @brief Construct the cold junction active object.
 *
 * @param me Storage owned by the caller.
 */
void ao_cold_junction_ctor(ao_cold_junction_t* me);

#ifdef __cplusplus
}
#endif

#endif
