/**
 * @file host_support.c
 * @brief 宿主侧的 QP 回调与共享的虚拟节拍。
 *
 * QV 调度器从不在宿主测试中运行，但它的链接期回调必须存在。Q_onError 会中止
 * 测试：在宿主世界里，断言是失败，而不是停机并离开现场的事件。
 */

#include <stdio.h>
#include <stdlib.h>

#include "qpc.h"

#include "host_support.h"

uint32_t g_test_tick;

/* QV 内核循环会引用这些函数；测试从不调用 QF_run()。 */
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
