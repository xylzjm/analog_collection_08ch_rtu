/**
 * @file test_cold_junction_ao.c
 * @brief 冷端 AO 的宿主事件注入测试。
 *
 * 驱动真实的 QHsm 派发循环（QActive_get_ -> QASM_DISPATCH -> QF_gc，与 QV 内核
 * 执行的同一三元组），并通过 QTIMEEVT_TICK_X 注入节拍。AO 之下的 TMP75 是通过
 * 可编排的 tmp75_port_fake 运行的真实驱动：成功、失败与恢复都在总线上编排，
 * 记录器 AO 观察每一次 CJC_SIG 心跳。
 *
 * 覆盖：启动首次读数、1 Hz 心跳节律、连续三次失败使心跳失效并冻结最后有效
 * 温度，以及恢复读数重新变为有效。
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qpc.h"

#include "app.h"
#include "cold_junction_ao.h"
#include "host_support.h"
#include "tmp75.h"
#include "tmp75_port_fake.h"

static int checks;
static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        ++checks;                                                  \
        if ( !(cond) ) {                                           \
            ++failures;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while ( 0 )

/*----------------------------------------------------------------------------*/
/* 测试夹具                                                                   */
/*----------------------------------------------------------------------------*/

static cold_junction_ao_t ao;
static tmp75_t dev;
static tmp75_port_fake_t port;
static tmp75_port_t port_if;
static QEvtPtr ao_queue[COLD_JUNCTION_AO_QUEUE_LEN];
static QSubscrList subscr[MAX_SIG];
static QF_MPOOL_EL(measure_evt_t) pool_a[APP_POOL_A_BLOCK_NUM];

/* 发布观察者：一个普通 QActive，测试直接排空它的队列。 */
static QActive recorder;
static QEvtPtr rec_queue[32];
static uint8_t ao_started;
static uint8_t rec_started;

/** @brief 一次观察到的 CJC_SIG 心跳。 */
static struct {
    float temp_c;   /**< 发布的温度。 */
    uint8_t valid;  /**< 发布的有效性标志。 */
    uint32_t stamp; /**< 心跳被派发时的 g_test_tick。 */
} beats[16];
static uint8_t beat_count;

/** @brief 排空记录器并记录每一次 CJC_SIG 心跳。 */
static void rec_drain(void)
{
    while ( !QEQueue_isEmpty(&recorder.eQueue) ) {
        QEvt const* e = QActive_get_(&recorder);

        if ( e->sig == CJC_SIG ) {
            cjc_evt_t const* c = Q_EVT_CAST(cjc_evt_t);

            beats[beat_count].temp_c = c->temp_c;
            beats[beat_count].valid  = c->valid;
            beats[beat_count].stamp  = g_test_tick;
            ++beat_count;
        }
        QF_gc(e);
    }
}

static void tick(uint32_t n)
{
    while ( n != 0U ) {
        QTIMEEVT_TICK_X(0U, (void*)0);
        ++g_test_tick;
        --n;
    }
}

static void dispatch_one(void)
{
    QEvt const* e = QActive_get_(Q_ACTIVE_UPCAST(&ao));

    QASM_DISPATCH(Q_ACTIVE_UPCAST(&ao), e, 0U);
    QF_gc(e);
}

/** @brief 排空 AO 并回收每条观察者事件，事件池安全。 */
static void drain_all(void)
{
    while ( !QEQueue_isEmpty(&ao.super.eQueue) ) {
        dispatch_one();
    }
    rec_drain();
}

/**
 * @brief 像 QV 循环那样，在持续排空的同时推进虚拟时间。
 *
 * 心跳节拍必须与排空交错，否则已发布的事件会在记录器队列中堆积。
 */
static void run_ticks(uint32_t n)
{
    while ( n != 0U ) {
        tick(1U);
        drain_all();
        --n;
    }
}

static QState rec_state(QHsm* const me, QEvt const* const e)
{
    (void)me;
    (void)e;
    return Q_SUPER(&QHsm_top);
}

static QState rec_handler(QHsm* const me, QEvt const* const e)
{
    (void)me;
    (void)e;
    return Q_TRAN(&rec_state);
}

/**
 * @brief 在编排好的 TMP75 之上启动 AO，并让记录器订阅。
 *
 * 观察者必须在 QActive_start 之前完成订阅：首次读数会在 start 调用内的
 * Starting 入口发布 CJC_SIG。
 */
static void scenario_begin(void)
{
    if ( ao_started != 0U ) {
        (void)QTimeEvt_disarm(&ao.te_tick);
        QActive_unregister_(Q_ACTIVE_UPCAST(&ao));
    }
    if ( rec_started != 0U ) {
        QActive_unregister_(&recorder);
    }
    if ( (ao_started != 0U) || (rec_started != 0U) ) {
        /* QP 延迟解除已废止时间事件的链接（在 QTimeEvt_tick_ 内部）：
         * 下面的重新构造与重新安装之前必须运行一个节拍，否则同一个 te_tick
         * 对象会保持两次链接，节拍扫描将陷入循环。 */
        tick(1U);
    }

    /* 清除所有订阅，使重复订阅不会造成重复投递。 */
    memset(subscr, 0, sizeof(subscr));
    QActive_psInit(subscr, MAX_SIG);

    tmp75_port_fake_init(&port);
    tmp75_port_fake_port(&port, &port_if);
    CHECK(tmp75_ctor(&dev, &port_if, TMP75_ADDRESS_0) == TMP75_OK);
    beat_count = 0U;

    QActive_ctor(&recorder, Q_STATE_CAST(&rec_handler));
    QActive_start(&recorder, Q_PRIO(30U, 0U),
                  rec_queue, 32U, (void*)0, 0U, (void*)0);
    rec_started = 1U;
    QActive_subscribe(&recorder, CJC_SIG);

    cold_junction_ao_ctor(&ao, &dev);
    QActive_start(Q_ACTIVE_UPCAST(&ao), Q_PRIO(APP_PRIO_CJC, 0U),
                  ao_queue, COLD_JUNCTION_AO_QUEUE_LEN,
                  (void*)0, 0U, (void*)0);
    ao_started = 1U;
}

/*----------------------------------------------------------------------------*/
/* 场景                                                                       */
/*----------------------------------------------------------------------------*/

/** @brief 启动时发布首次读数；心跳以 1 Hz 运行。 */
static void test_startup_and_heartbeat(void)
{
    scenario_begin();

    /* 首次读数在 Starting 入口（QActive_start 内部）发布：任何节拍之前
     * 就有一次心跳。 */
    drain_all();
    CHECK(beat_count == 1U);
    CHECK(beats[0].temp_c == 25.0f);
    CHECK(beats[0].valid != 0U);
    CHECK(beats[0].stamp == 0U);
    CHECK(port.calls == 1U);
    CHECK(ao.te_tick.interval == COLD_JUNCTION_AO_TICK_MS);

    /* 每周期一次心跳，其间没有别的。 */
    run_ticks(COLD_JUNCTION_AO_TICK_MS - 1U);
    CHECK(beat_count == 1U);
    run_ticks(1U);
    CHECK(beat_count == 2U);
    CHECK(beats[1].temp_c == 25.0f);
    CHECK(beats[1].valid != 0U);
    CHECK(beats[1].stamp == COLD_JUNCTION_AO_TICK_MS);
    CHECK(port.calls == 2U);

    run_ticks(COLD_JUNCTION_AO_TICK_MS);
    CHECK(beat_count == 3U);
    CHECK(beats[2].stamp == (2U * COLD_JUNCTION_AO_TICK_MS));
    CHECK(port.calls == 3U);
}

/** @brief 连续三次失败使最后的值失效并冻结。 */
static void test_three_failures_freeze(void)
{
    scenario_begin();
    drain_all();
    CHECK(beat_count == 1U);

    tmp75_port_fake_fail(&port);
    run_ticks(COLD_JUNCTION_AO_TICK_MS); /* 失败 1：err_cnt 1 */
    run_ticks(COLD_JUNCTION_AO_TICK_MS); /* 失败 2：err_cnt 2 */
    CHECK(beat_count == 3U);
    CHECK(beats[1].valid != 0U);
    CHECK(beats[2].valid != 0U);
    CHECK(beats[1].temp_c == 25.0f);
    CHECK(beats[2].temp_c == 25.0f);

    run_ticks(COLD_JUNCTION_AO_TICK_MS); /* 失败 3：失效 */
    CHECK(beat_count == 4U);
    CHECK(beats[3].valid == 0U);
    CHECK(beats[3].temp_c == 25.0f); /* 冻结：最后有效温度 */
    CHECK(ao.err_cnt == COLD_JUNCTION_AO_ERR_LIMIT);

    /* 失败持续期间保持失效。 */
    run_ticks(COLD_JUNCTION_AO_TICK_MS);
    CHECK(beat_count == 5U);
    CHECK(beats[4].valid == 0U);
    CHECK(beats[4].temp_c == 25.0f);
}

/** @brief 恢复读数重新变为有效并更新温度。 */
static void test_recovery(void)
{
    scenario_begin();
    drain_all();

    tmp75_port_fake_fail(&port);
    run_ticks(3U * COLD_JUNCTION_AO_TICK_MS);
    CHECK(beat_count == 4U);
    CHECK(beats[3].valid == 0U);

    port.status = TMP75_OK;
    tmp75_port_fake_set_temperature(&port, 30.5f);
    run_ticks(COLD_JUNCTION_AO_TICK_MS);
    CHECK(beat_count == 5U);
    CHECK(beats[4].valid != 0U);
    CHECK(beats[4].temp_c == 30.5f);
    CHECK(ao.err_cnt == 0U);
    CHECK(ao.temp_c == 30.5f);
}

/*----------------------------------------------------------------------------*/

int main(void)
{
    QF_init();
    QF_poolInit(pool_a, sizeof(pool_a), sizeof(pool_a[0]));

    test_startup_and_heartbeat();
    test_three_failures_freeze();
    test_recovery();

    printf("test_cold_junction_ao: %d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
