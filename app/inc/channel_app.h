#ifndef CHANNEL_APP_H
#define CHANNEL_APP_H

#include "qpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Number of measurement channels. */
#define CHANNEL_APP_COUNT 8U

/**
 * @brief Return the active object of one channel.
 *
 * @param channel Modbus channel 0..7.
 * @return The channel AO, or NULL while the channel module is a stub.
 * @note Callers must skip every post when NULL.
 */
QActive* channel_app_get(uint8_t channel);

/** @brief Construct the channel stub; safe to call more than once. */
void channel_app_init(void);

#ifdef __cplusplus
}
#endif

#endif
