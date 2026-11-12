#ifndef MODBUS_IMAGE_H
#define MODBUS_IMAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Input image of one measurement channel. */
typedef struct {
    int32_t value;   /**< Engineering value x0.001; INT32_MIN when invalid. */
    uint16_t status; /**< Channel status code; 0 is normal. */
} modbus_ch_image_t;

/** @brief Input image of the whole device. */
typedef struct {
    modbus_ch_image_t ch[8]; /**< One entry per Modbus channel 0..7. */
    int32_t cj_value;        /**< Cold-junction temperature x0.001. */
    uint16_t cj_status;      /**< Cold-junction status code. */
} modbus_image_t;

/** @brief Value forced into an invalid measurement. */
#define MODBUS_IMAGE_INVALID_VALUE ((int32_t)0x80000000L)

/** @brief Status reported before the first sample arrives. */
#define MODBUS_IMAGE_NO_SAMPLE_STATUS 5U

/** @brief Cold-junction status reported before the first sample arrives. */
#define MODBUS_IMAGE_CJ_NO_SAMPLE_STATUS 1U

#ifdef __cplusplus
}
#endif

#endif
