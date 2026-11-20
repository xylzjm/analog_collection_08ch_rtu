#ifndef QP_PORT_H_
#define QP_PORT_H_

/**
 * @file qp_port.h
 * @brief 活动对象测试的宿主 QP/C 移植层（tests/run_host_tests.ps1）。
 *
 * 镜像 board/ports/qp/qp_port.h，但中断与睡眠原语被中性化：宿主测试是单线程的，
 * 由测试自己驱动派发循环（QActive_get_ -> QASM_DISPATCH -> QF_gc，与 QV 内核
 * 循环执行的同一三元组）。qp_config.h 与固件构建共用，因此事件、事件池和队列
 * 的尺寸与目标板完全一致。
 */

#include "qp_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QACTIVE_EQUEUE_TYPE QEQueue
#define QACTIVE_THREAD_TYPE void const*

/* 单线程宿主派发：没有任何东西能抢占 RTC 步进。 */
#define QF_INT_DISABLE() ((void)0)
#define QF_INT_ENABLE()  ((void)0)
#define QF_CRIT_STAT
#define QF_CRIT_ENTRY()    ((void)0)
#define QF_CRIT_EXIT()     ((void)0)
#define QF_CRIT_EST()      ((void)0)
#define QF_CRIT_EXIT_NOP() ((void)0)

/* 宿主 gcc 提供与 ARMv7-M 分支相同的基于 CLZ 的 log2。 */
#define QF_LOG2(n_)            ((uint_fast8_t)(32 - __builtin_clz((unsigned)(n_))))

#define QF_AWARE_ISR_CMSIS_PRI 0

/* QV 调度器本身从不在宿主测试中运行。 */
#define QV_CPU_SLEEP()          ((void)0)
#define QV_ARM_ERRATUM_838869() ((void)0)
#define QV_INIT()               ((void)0)

#include "qequeue.h"
#include "qmpool.h"
#include "qp.h"
#include "qv.h"

#endif
