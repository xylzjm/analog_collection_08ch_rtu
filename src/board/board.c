#include "qpc.h"

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
