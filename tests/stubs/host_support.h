#ifndef HOST_SUPPORT_H_
#define HOST_SUPPORT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 自场景开始以来的虚拟毫秒数；假件用它给调用盖戳。 */
extern uint32_t g_test_tick;

#ifdef __cplusplus
}
#endif

#endif
