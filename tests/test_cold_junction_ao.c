/**
 * @file test_cold_junction_ao.c
 * @brief Host event-injection tests for the cold junction AO.
 *
 * Drives the real QHsm dispatch loop (QActive_get_ -> QASM_DISPATCH ->
 * QF_gc, the same triple the QV kernel runs) with tick injection through
 * QTIMEEVT_TICK_X. The TMP75 below the AO is the real driver over the
 * scriptable tmp75_port_fake: success, failure and recovery are scripted
 * at the wire, and the recorder AO observes every CJC_SIG heartbeat.
 *
 * Coverage: the startup first reading, the 1 Hz heartbeat cadence, three
 * consecutive failures invalidating the heartbeat with the last valid
 * temperature frozen, and a recovery reading going valid again.
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
/* Harness                                                                    */
/*----------------------------------------------------------------------------*/

static cold_junction_ao_t ao;
static tmp75_t dev;
static tmp75_port_fake_t port;
static tmp75_port_t port_if;
static QEvtPtr ao_queue[COLD_JUNCTION_AO_QUEUE_LEN];
static QSubscrList subscr[MAX_SIG];
static QF_MPOOL_EL(measure_evt_t) pool_a[APP_POOL_A_BLOCK_NUM];

/* Publish observer: a plain QActive whose queue the test drains directly. */
static QActive recorder;
static QEvtPtr rec_queue[32];
static uint8_t ao_started;
static uint8_t rec_started;

/** @brief One observed CJC_SIG heartbeat. */
static struct {
    float temp_c;  /**< Published temperature. */
    uint8_t valid; /**< Published validity flag. */
    uint32_t stamp; /**< g_test_tick when the heartbeat was dispatched. */
} beats[16];
static uint8_t beat_count;

/** @brief Drain the recorder and record every CJC_SIG heartbeat. */
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

/** @brief Drain the AO and recycle every observer event, pool-safe. */
static void drain_all(void)
{
    while ( !QEQueue_isEmpty(&ao.super.eQueue) ) {
        dispatch_one();
    }
    rec_drain();
}

/**
 * @brief Advance virtual time with continuous draining, like the QV loop.
 *
 * The heartbeat tick must interleave with draining or the published
 * events pile up in the recorder queue.
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
 * @brief Bring the AO up over a scripted TMP75 and subscribe the recorder.
 *
 * The observer must be subscribed before QActive_start: the first reading
 * publishes CJC_SIG from the Starting entry inside the start call.
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
        /* QP un-links disarmed time events lazily (inside QTimeEvt_tick_):
         * one tick must run before the re-ctor and re-arm below, or the
         * same te_tick object stays linked twice and the tick scan cycles. */
        tick(1U);
    }

    /* Wipe every subscription so re-subscription cannot double-deliver. */
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
/* Scenarios                                                                  */
/*----------------------------------------------------------------------------*/

/** @brief Startup publishes the first reading; the heartbeat runs at 1 Hz. */
static void test_startup_and_heartbeat(void)
{
    scenario_begin();

    /* The first reading is published from the Starting entry (inside
     * QActive_start): one heartbeat before any tick. */
    drain_all();
    CHECK(beat_count == 1U);
    CHECK(beats[0].temp_c == 25.0f);
    CHECK(beats[0].valid != 0U);
    CHECK(beats[0].stamp == 0U);
    CHECK(port.calls == 1U);
    CHECK(ao.te_tick.interval == COLD_JUNCTION_AO_TICK_MS);

    /* One heartbeat per period, nothing in between. */
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

/** @brief Three consecutive failures invalidate and freeze the last value. */
static void test_three_failures_freeze(void)
{
    scenario_begin();
    drain_all();
    CHECK(beat_count == 1U);

    tmp75_port_fake_fail(&port);
    run_ticks(COLD_JUNCTION_AO_TICK_MS); /* failure 1: err_cnt 1 */
    run_ticks(COLD_JUNCTION_AO_TICK_MS); /* failure 2: err_cnt 2 */
    CHECK(beat_count == 3U);
    CHECK(beats[1].valid != 0U);
    CHECK(beats[2].valid != 0U);
    CHECK(beats[1].temp_c == 25.0f);
    CHECK(beats[2].temp_c == 25.0f);

    run_ticks(COLD_JUNCTION_AO_TICK_MS); /* failure 3: invalid */
    CHECK(beat_count == 4U);
    CHECK(beats[3].valid == 0U);
    CHECK(beats[3].temp_c == 25.0f); /* frozen: last valid temperature */
    CHECK(ao.err_cnt == COLD_JUNCTION_AO_ERR_LIMIT);

    /* It stays invalid while the failures continue. */
    run_ticks(COLD_JUNCTION_AO_TICK_MS);
    CHECK(beat_count == 5U);
    CHECK(beats[4].valid == 0U);
    CHECK(beats[4].temp_c == 25.0f);
}

/** @brief A recovery reading goes valid again and updates the temperature. */
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
