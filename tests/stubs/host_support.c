/**
 * @file host_support.c
 * @brief Host-side QP callbacks and the shared virtual tick.
 *
 * The QV scheduler never runs in the host tests, but its link-time
 * callbacks must exist. Q_onError aborts the test: in the host world an
 * assertion is a failure, not a halt-and-leave-the-scene event.
 */

#include <stdio.h>
#include <stdlib.h>

#include "qpc.h"

#include "host_support.h"

uint32_t g_test_tick;

/* The QV kernel loop references these; the tests never call QF_run(). */
void QV_onIdle(void)
{
}

void QF_onStartup(void)
{
}

void QF_onCleanup(void)
{
}

Q_NORETURN Q_onError(char const* const module,
                     int_t const id)
{
    fprintf(stderr, "Q_onError: module=%s id=%d tick=%u\n",
            module, (int)id, (unsigned)g_test_tick);
    exit(1);
}
