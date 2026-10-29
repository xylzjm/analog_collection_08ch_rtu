#include "qpc.h"

static volatile uint32_t systick_ctr;

uint32_t board_tick_get(void)
{
    uint32_t ctr;

    QF_CRIT_STAT;
    QF_CRIT_ENTRY();
    ctr = systick_ctr;
    QF_CRIT_EXIT();

    return ctr;
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
