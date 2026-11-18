#ifndef HOST_SUPPORT_H_
#define HOST_SUPPORT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Virtual milliseconds since scenario start; the fakes stamp calls. */
extern uint32_t g_test_tick;

#ifdef __cplusplus
}
#endif

#endif
