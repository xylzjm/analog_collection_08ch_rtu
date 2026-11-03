#include "qpc.h"

#include "board.h"

/*----------------------------------------------------------------------------
 * Nestable QF critical sections
 *
 * qv_port.c provides QF_crit_entry_()/QF_crit_exit_() as weak, strictly
 * non-nesting functions that assert the PRIMASK (or BASEPRI) mask is clear on
 * entry and set on exit, and re-enable the interrupts on every exit.  The
 * strong definitions below replace them with the board's nestable critical
 * section (board.h), so a critical section may be entered again from inside
 * another one.  Every caller -- QP's own critical sections included -- shares
 * the one nesting count behind those macros, and only the outermost exit
 * re-enables the interrupts, so the masking behaviour and the rules of the
 * original port (see board.h, same error codes 110/111 as qv_port.c) stay
 * exactly as they were.
 */
/* The one nesting count behind the board.h macros; board.h only declares it
 * inside those macros, so the count is not part of the header's API. */
extern uint32_t volatile board_crit_nest;
uint32_t volatile board_crit_nest;

Q_NORETURN board_crit_error_(int id)
{
    Q_onError("board", id);
}

void QF_crit_entry_(void)
{
    board_critical_enter();
}

void QF_crit_exit_(void)
{
    board_critical_exit();
}

static volatile uint32_t systick_ctr;

uint32_t board_tick_get(void)
{
    return systick_ctr;
}

Q_NORETURN Q_onError(char const* const module,
                     int_t const id)
{
    Q_UNUSED_PAR(module);
    Q_UNUSED_PAR(id);

    for ( ;; ) {
    }
}

void QF_onCleanup(void)
{
}

void QF_onStartup(void)
{
}

void QV_onIdle(void)
{
}

void HardFault_Handler(void)
{
}

void SysTick_Handler(void)
{
    ++systick_ctr;
}
