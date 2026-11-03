#ifndef __BOARD_H__
#define __BOARD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"

/* The interrupt mask must match the QP port: PRIMASK, or BASEPRI with the
 * QF_USE_BASEPRI threshold, is what qv_port.c/QF_crit_entry_() use. */
#include "qp_config.h"

/* Incomplete on purpose: board_init() owns the parameter KVDB instance and
 * hands it out by pointer; users include flashdb.h for the full type. */
struct fdb_kvdb;

/*----------------------------------------------------------------------------
 * Nestable critical section
 *
 * One nesting count and one interrupt mask for the whole board: the QP
 * critical section (board.c's strong QF_crit_entry_()/QF_crit_exit_()
 * override of the qv_port.c ones) and every board module expand the same
 * macros, so the modbus core, board code and the framework nest inside each
 * other and the interrupts come back only at the outermost exit.  The macros
 * below are the implementation; the QF functions just expand them.
 *
 * The nesting count (board_crit_nest) is owned by board.c and only declared
 * inside the macros, so it is not part of this header's API: every user goes
 * through the macros and their checks.
 *
 * The checks keep the rules of the original QP port: the outermost entry must
 * find the interrupts enabled (so no section may start inside
 * QF_INT_DISABLE()), a nested entry must find them masked, and an exit must
 * match a pending entry.  A violation is reported to board_crit_error_().
 */

/** @brief Milliseconds since boot, from the SysTick counter. */
uint32_t board_tick_get(void);

/**
 * @brief Bring up the whole platform once at power-up.
 *
 * WorkBench peripherals first, then SEGGER RTT, EasyLogger, FAL and the
 * parameter KVDB, and finally the board component objects (see board.c for
 * the exact order and its reasons).  It returns no status: a failing step is
 * logged and the remaining steps still run, and BOARD_SELFTEST reports what
 * ended up working.
 */
void board_init(void);

/** @brief The parameter KVDB instance that board_init() brings up. */
struct fdb_kvdb* board_kvdb_get(void);

/** @brief Report a critical-section contract violation; never returns. */
#ifdef __cplusplus
[[noreturn]] void board_crit_error_(int id);
#else
_Noreturn void board_crit_error_(int id);
#endif

/* mask primitives, internal to the macros below */
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

static __inline __attribute__((always_inline)) void board_crit_mask_(void)
{
#ifdef QF_USE_BASEPRI
    __asm volatile("msr basepri, %0" ::"r"((uint32_t)QF_USE_BASEPRI) : "memory");
#else
    __asm volatile("cpsid i" ::: "memory");
#endif
}

static __inline __attribute__((always_inline)) void board_crit_unmask_(void)
{
#ifdef QF_USE_BASEPRI
    __asm volatile("msr basepri, %0" ::"r"(0U) : "memory");
#else
    __asm volatile("cpsie i" ::: "memory");
#endif
}

/**
 * @brief Enter the board critical section (nestable interrupt mask).
 *
 * The first entry masks the interrupts, a nested entry only raises the
 * nesting count, and the interrupts come back at the outermost exit.  Usable
 * both from thread and from interrupt context.
 */
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

/** @brief Leave the board critical section; see board_critical_enter(). */
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

#endif /* __BOARD_H__ */
