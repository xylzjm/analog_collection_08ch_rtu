#ifndef APP_VERSION_H
#define APP_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Report-Slave-ID model string. */
#define APP_MODEL_STRING "AC08RTU"

/** @brief Firmware version reported by function code 0x11. */
#define APP_VERSION_MAJOR 1U
#define APP_VERSION_MINOR 0U
#define APP_VERSION_PATCH 0U

/** @brief Length of APP_MODEL_STRING without the terminator. */
#define APP_MODEL_STRING_LENGTH 7U

/** @brief Additional FC11 bytes: model followed by major/minor/patch. */
#define APP_IDENTITY_ADDITIONAL_LENGTH (APP_MODEL_STRING_LENGTH + 3U)

#ifdef __cplusplus
}
#endif

#endif
