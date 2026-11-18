#ifndef QP_PORT_H_
#define QP_PORT_H_

/**
 * @file qp_port.h
 * @brief Host QP/C port for the active-object tests (tests/run_host_tests.ps1).
 *
 * Mirrors board/ports/qp/qp_port.h with neutralized interrupt and sleep
 * primitives: the host tests are single-threaded and drive the dispatch
 * loop themselves (QActive_get_ -> QASM_DISPATCH -> QF_gc, the same triple
 * the QV kernel loop runs). qp_config.h is shared with the firmware build,
 * so event, pool and queue sizing match the target exactly.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "qp_config.h"

#define QACTIVE_EQUEUE_TYPE     QEQueue
#define QACTIVE_THREAD_TYPE     void const *

/* Single-threaded host dispatch: nothing can preempt an RTC step. */
#define QF_INT_DISABLE()        ((void)0)
#define QF_INT_ENABLE()         ((void)0)
#define QF_CRIT_STAT
#define QF_CRIT_ENTRY()         ((void)0)
#define QF_CRIT_EXIT()          ((void)0)
#define QF_CRIT_EST()           ((void)0)
#define QF_CRIT_EXIT_NOP()      ((void)0)

/* Host gcc provides the same CLZ-based log2 as the ARMv7-M branch. */
#define QF_LOG2(n_)             ((uint_fast8_t)(32 - __builtin_clz((unsigned)(n_))))

#define QF_AWARE_ISR_CMSIS_PRI  0

/* The QV scheduler itself never runs in the host tests. */
#define QV_CPU_SLEEP()          ((void)0)
#define QV_ARM_ERRATUM_838869() ((void)0)
#define QV_INIT()               ((void)0)

#include "qequeue.h"
#include "qmpool.h"
#include "qp.h"
#include "qv.h"

#endif
