#ifndef __BOARD_H__
#define __BOARD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"

/* 中断屏蔽方式必须与 QP 端口一致（PRIMASK 或 QF_USE_BASEPRI）。 */
#include "qp_config.h"

/* 板级拓扑事实（板位与通道数），重新导出供使用方引用。 */
#include "board_config.h"

/** @brief 初始化板级硬件与板级设备对象。 */
void board_init(void);

/** @brief 自启动以来的毫秒数，取自 SysTick 计数器。 */
uint32_t board_tick_get(void);

/** @brief 上报临界区契约违规；不会返回。 */
#ifdef __cplusplus
[[noreturn]] void board_crit_error_(int id);
#else
_Noreturn void board_crit_error_(int id);
#endif

/** @brief 读取当前中断屏蔽状态。 */
static __inline __attribute__((always_inline)) uint32_t board_crit_state_(void)
{
    uint32_t state;

#ifdef QF_USE_BASEPRI
    __asm volatile("mrs %0, basepri" : "=r"(state)::"memory");
#else
    __asm volatile("mrs %0, primask" : "=r"(state)::"memory");
#endif
    return state;
}

/** @brief 为板级临界区屏蔽中断。 */
static __inline __attribute__((always_inline)) void board_crit_mask_(void)
{
#ifdef QF_USE_BASEPRI
    __asm volatile("msr basepri, %0" ::"r"((uint32_t)QF_USE_BASEPRI) : "memory");
#else
    __asm volatile("cpsid i" ::: "memory");
#endif
}

/** @brief 板级临界区结束后恢复中断。 */
static __inline __attribute__((always_inline)) void board_crit_unmask_(void)
{
#ifdef QF_USE_BASEPRI
    __asm volatile("msr basepri, %0" ::"r"(0U) : "memory");
#else
    __asm volatile("cpsie i" ::: "memory");
#endif
}

/** @brief 进入可嵌套的板级临界区；最外层进入时要求中断已开启。 */
#define board_critical_enter()                                             \
    do {                                                                   \
        extern uint32_t volatile board_crit_nest;                          \
        uint32_t const state_ = board_crit_state_();                       \
        if ( (board_crit_nest == 0U) ? (state_ != 0U) : (state_ == 0U) ) { \
            board_crit_error_(110);                                        \
        }                                                                  \
        board_crit_mask_();                                                \
        ++board_crit_nest;                                                 \
    } while ( 0 )

/** @brief 离开可嵌套的板级临界区；仅最外层退出时恢复中断。 */
#define board_critical_exit()                                           \
    do {                                                                \
        extern uint32_t volatile board_crit_nest;                       \
        if ( (board_crit_nest == 0U) || (board_crit_state_() == 0U) ) { \
            board_crit_error_(111);                                     \
        }                                                               \
        if ( --board_crit_nest == 0U ) {                                \
            board_crit_unmask_();                                       \
        }                                                               \
    } while ( 0 )

#ifdef __cplusplus
}
#endif

#endif
