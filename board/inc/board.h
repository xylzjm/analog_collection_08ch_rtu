#ifndef __BOARD_H__
#define __BOARD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"

/* Interrupt mask must match the QP port (PRIMASK or QF_USE_BASEPRI). */
#include "qp_config.h"

/** @brief Bring up the board hardware and board-level device objects. */
void board_init(void);

/** @brief Milliseconds since boot, from the SysTick counter. */
uint32_t board_tick_get(void);

/** @brief Report a critical-section contract violation; never returns. */
#ifdef __cplusplus
[[noreturn]] void board_crit_error_(int id);
#else
_Noreturn void board_crit_error_(int id);
#endif

/** @brief Read the current interrupt-mask state. */
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

/** @brief Mask interrupts for the board critical section. */
static __inline __attribute__((always_inline)) void board_crit_mask_(void)
{
#ifdef QF_USE_BASEPRI
    __asm volatile("msr basepri, %0" ::"r"((uint32_t)QF_USE_BASEPRI) : "memory");
#else
    __asm volatile("cpsid i" ::: "memory");
#endif
}

/** @brief Restore interrupts after the board critical section. */
static __inline __attribute__((always_inline)) void board_crit_unmask_(void)
{
#ifdef QF_USE_BASEPRI
    __asm volatile("msr basepri, %0" ::"r"(0U) : "memory");
#else
    __asm volatile("cpsie i" ::: "memory");
#endif
}

/** @brief Enter the nestable board critical section; the outermost entry expects interrupts enabled. */
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

/** @brief Leave the nestable board critical section; only the outermost exit restores interrupts. */
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
