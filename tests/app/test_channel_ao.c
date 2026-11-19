/**
 * @file test_channel_ao.c
 * @brief Host event-injection tests for the channel acquisition AO.
 *
 * Drives the real QHsm dispatch loop (QActive_get_ -> QASM_DISPATCH ->
 * QF_gc, the same triple the QV kernel runs) with tick injection through
 * QTIMEEVT_TICK_X. This replaces the deleted reconfiguration model test:
 * the interleaving sweep runs against the real state machine, and the fake
 * analog input's call log is the trajectory evidence.
 *
 * Host-scope note: the sweep proves the queue-order discipline of
 * docs/ao/channel_ao.md section 6 (a timer event already queued when the
 * reconfiguration is dispatched must be consumed by the old state or
 * ignored in Unconfigured). Expiry landing between two actions of one
 * RTC step cannot happen on the single-threaded host; on target it is
 * excluded structurally by disarming before posting APPLY_SIG, and the
 * deadline assertions below would catch any dispatch-visible regression.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qpc.h"

#include "analog_input_fake.h"
#include "app_events.h"
#include "channel_ao.h"
#include "host_support.h"
#include "reg_map.h"

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

static channel_ao_t ao;
static analog_input_fake_t fake;
static QEvtPtr ao_queue[CHANNEL_AO_QUEUE_LEN];
static QSubscrList subscr[MAX_SIG];
static QF_MPOOL_EL(measure_evt_t) pool_a[APP_POOL_A_BLOCK_NUM];
static QF_MPOOL_EL(config_set_evt_t) pool_b[APP_POOL_B_BLOCK_NUM];

/* Publish observer: a plain QActive whose queue the test drains directly. */
static QActive recorder;
static QEvtPtr rec_queue[32];
static uint8_t ao_started;
static uint8_t rec_started;

/** @brief Last configuration-apply result reported by the AO. */
static uint16_t applied_count;
static uint8_t applied_ch;
static uint8_t applied_ok;

static void on_applied(void* ctx,
                       uint8_t ch,
                       uint8_t ok)
{
    (void)ctx;
    ++applied_count;
    applied_ch = ch;
    applied_ok = ok;
}

/** @brief Drain the recorder queue and return how many events it held. */
static uint16_t rec_drain(void)
{
    uint16_t n = 0U;

    while ( !QEQueue_isEmpty(&recorder.eQueue) ) {
        QEvt const* e = QActive_get_(&recorder);

        ++n;
        QF_gc(e);
    }
    return n;
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

/** @brief Pop one recorder event; NULL when the queue holds none. */
static QEvt const* rec_take(void)
{
    if ( QEQueue_isEmpty(&recorder.eQueue) ) {
        return NULL;
    }
    return QActive_get_(&recorder);
}

static void drain(void)
{
    while ( !QEQueue_isEmpty(&ao.super.eQueue) ) {
        dispatch_one();
    }
}

/** @brief Drain the AO and recycle every observer event, pool-safe. */
static void drain_all(void)
{
    drain();
    rec_drain();
}

/**
 * @brief Advance virtual time with continuous draining, like the QV loop.
 *
 * Settle deadlines are armed during dispatches, so ticks must interleave
 * with draining or the next deadline never lands inside the budget.
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
 * @brief Bring the AO up (its initial transition runs inside QActive_start)
 *        and subscribe the recorder to MEASURE_SIG.
 */
static void scenario_begin(void)
{
    if ( ao_started != 0U ) {
        (void)QTimeEvt_disarm(&ao.te_sample);
        (void)QTimeEvt_disarm(&ao.te_settle);
        (void)QTimeEvt_disarm(&ao.te_break);
        QActive_unregister_(Q_ACTIVE_UPCAST(&ao));
    }
    if ( rec_started != 0U ) {
        QActive_unregister_(&recorder);
    }

    /* Wipe every subscription so re-subscription cannot double-deliver. */
    memset(subscr, 0, sizeof(subscr));
    QActive_psInit(subscr, MAX_SIG);

    analog_input_fake_init(&fake);
    analog_input_fake_reset_log(&fake);
    applied_count = 0U;
    applied_ch    = 0xFFU;
    applied_ok    = 0xFFU;

    channel_ao_ctor(&ao, 0U, &fake.base, on_applied, NULL);
    QActive_start(Q_ACTIVE_UPCAST(&ao), Q_PRIO(2U, 0U),
                  ao_queue, CHANNEL_AO_QUEUE_LEN, (void*)0, 0U, (void*)0);
    ao_started = 1U;

    QActive_ctor(&recorder, Q_STATE_CAST(&rec_handler));
    QActive_start(&recorder, Q_PRIO(30U, 0U),
                  rec_queue, 32U, (void*)0, 0U, (void*)0);
    rec_started = 1U;
    QActive_subscribe(&recorder, MEASURE_SIG);
}

/** @brief Publish one configuration for channel 1. */
static void publish_config(uint8_t type,
                           uint8_t tc_type,
                           uint16_t rate_code)
{
    config_set_evt_t* e = Q_NEW(config_set_evt_t, CONFIG_SET_SIG);

    e->ch             = 0U;
    e->cfg.type       = type;
    e->cfg.tc_type    = tc_type;
    e->cfg.rate_code  = rate_code;
    e->cfg.linear_min = 0.0f;
    e->cfg.linear_max = 0.0f;
    e->cfg.offset     = 0.0f;
    e->cfg.gain       = 1.0f;
    QACTIVE_PUBLISH(&e->super, (void*)0);
}

/** @brief Publish one cold junction heartbeat. */
static void publish_cjc(float temp_c,
                        uint8_t valid)
{
    cjc_evt_t* e = Q_NEW(cjc_evt_t, CJC_SIG);

    e->temp_c = temp_c;
    e->valid  = valid;
    QACTIVE_PUBLISH(&e->super, (void*)0);
}

/**
 * @brief Feed one configuration and run the AO into Measuring.
 *
 * @param settle_ticks Milliseconds to wait on each settle deadline.
 */
static void drive_to_measuring(uint8_t type,
                               uint8_t tc_type,
                               uint16_t rate_code)
{
    publish_config(type, tc_type, rate_code);
    drain(); /* Unconfigured: pending + APPLY_SIG */
    drain(); /* APPLY: configure, Working init, Detecting entry arms te_settle */
    tick(fake.enable_delay_ms + 1U);
    drain(); /* SETTLE: wire check, disable, Settling entry arms te_settle */
    tick(fake.disable_delay_ms + 1U);
    drain(); /* SETTLE: first sample, Measuring entry arms te_sample */
}

/*----------------------------------------------------------------------------*/
/* Batch 1: the reconfiguration race, swept against the real state machine     */
/*----------------------------------------------------------------------------*/

/** @brief Start states of the sweep, mirroring the deleted model test. */
enum {
    SW_UNCONFIG = 0,
    SW_DETECTING,
    SW_SETTLING,
    SW_MEASURING
};

/**
 * @brief One interleaving: reconfigure while timers run, then prove the
 *        re-entered era obeys its deadlines and the merge rule.
 *
 * @param start One of the SW_* start states.
 * @param settle_before Expire the armed timer before the reconfig is posted.
 * @param late_after_post Expire an armed timer after the post, before the
 *        reconfig dispatch (the event must be ignored in Unconfigured).
 * @param second_cfg Post a second reconfig before APPLY (pending merge).
 * @param post_ticks Ticks stirred after the APPLY dispatch.
 */
static void sweep_run(int start,
                      int settle_before,
                      int late_after_post,
                      int second_cfg,
                      unsigned post_ticks)
{
    uint16_t baseline;
    uint16_t new_cfg_count;
    uint16_t i;

    scenario_begin();
    baseline = fake.log_len;

    /* Reach the requested start state. */
    if ( start == SW_DETECTING ) {
        publish_config((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);
        drain();
        drain();
    } else if ( start == SW_SETTLING ) {
        publish_config((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);
        drain();
        drain();
        tick(fake.enable_delay_ms + 1U);
        drain();
    } else if ( start == SW_MEASURING ) {
        drive_to_measuring((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);
    } else { /* SW_UNCONFIG: nothing to set up */
    }

    if ( settle_before ) {
        /* The deadline event queues ahead of the reconfig: the old state
         * must consume it as genuine before the reconfig dispatch. */
        if ( start == SW_MEASURING ) {
            tick(100U + 1U); /* te_sample period at 10 Hz */
        } else {
            tick(201U); /* either settle deadline is at most 200 ms */
        }
    }

    publish_config((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 5U); /* 20 Hz */
    if ( late_after_post ) {
        /* A timer expiry between post and dispatch lands behind the
         * reconfig in the FIFO; Unconfigured must ignore it. */
        if ( start == SW_MEASURING ) {
            tick(100U + 1U);
        } else if ( !settle_before ) {
            tick(201U);
        }
    }
    if ( second_cfg ) {
        publish_config((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 9U); /* 50 Hz */
    }

    drain(); /* reconfig dispatch, ignored late events, APPLY re-entry */
    tick(post_ticks);
    drain();

    /* Convergence: the new era must reach periodic sampling. */
    run_ticks(fake.enable_delay_ms + fake.disable_delay_ms + 2U * 40U);
    run_ticks(3U * 40U);

    /* No assertion fired (Q_onError aborts) and both queues are empty. */
    CHECK(QEQueue_isEmpty(&ao.super.eQueue));
    CHECK(QEQueue_isEmpty(&recorder.eQueue));

    /* Exactly one configure for the final generation, whatever merged. */
    new_cfg_count = 0U;
    for ( i = baseline; i < fake.log_len; ++i ) {
        if ( fake.log[i].op == (uint8_t)ANALOG_INPUT_FAKE_CONFIGURE ) {
            if ( (fake.log[i].rate == ch_config_rate_millihz(5U))
                 || (fake.log[i].rate == ch_config_rate_millihz(9U)) ) {
                ++new_cfg_count;
            }
        }
    }
    CHECK(new_cfg_count == 1U);

    /* Old-era wire checks may only precede the new-era configure. */
    {
        uint16_t cfg_idx          = 0U;
        uint8_t have_cfg          = 0U;
        uint32_t const final_rate = ch_config_rate_millihz((uint16_t)(second_cfg ? 9U : 5U));

        for ( i = baseline; i < fake.log_len; ++i ) {
            if ( (fake.log[i].op == (uint8_t)ANALOG_INPUT_FAKE_CONFIGURE)
                 && (fake.log[i].rate == final_rate) ) {
                cfg_idx  = i;
                have_cfg = 1U;
                break;
            }
        }
        CHECK(have_cfg != 0U);

        if ( have_cfg != 0U ) {
            const analog_input_fake_call_t* enable = NULL;
            const analog_input_fake_call_t* check  = NULL;
            uint16_t reads                         = 0U;

            for ( i = cfg_idx; i < fake.log_len; ++i ) {
                const analog_input_fake_call_t* c = &fake.log[i];

                if ( c->op == (uint8_t)ANALOG_INPUT_FAKE_ENABLE_BREAK ) {
                    CHECK(enable == NULL); /* one era, one enable */
                    enable = c;
                } else if ( c->op == (uint8_t)ANALOG_INPUT_FAKE_CHECK_WIRE ) {
                    CHECK(c->rate == final_rate);
                    if ( check == NULL ) {
                        check = c;
                        /* The deadline of the new era was respected. */
                        CHECK(enable != NULL);
                        CHECK(c->tick >= (enable->tick + fake.enable_delay_ms - 1U));
                    }
                } else if ( c->op == (uint8_t)ANALOG_INPUT_FAKE_READ_SAMPLE ) {
                    /* No sample under a foreign configuration. */
                    CHECK(c->rate == final_rate);
                    ++reads;
                }
            }
            CHECK(check != NULL);
            CHECK(reads >= 2U); /* convergence reached */
        }
    }
}

static void test_reconfig_sweep(void)
{
    int start;
    int settle_before;
    int late_after_post;
    int second_cfg;
    unsigned post_ticks;

    for ( start = SW_UNCONFIG; start <= SW_MEASURING; ++start ) {
        for ( settle_before = 0; settle_before <= 1; ++settle_before ) {
            for ( late_after_post = 0; late_after_post <= 1; ++late_after_post ) {
                for ( second_cfg = 0; second_cfg <= 1; ++second_cfg ) {
                    for ( post_ticks = 0U; post_ticks <= 2U; ++post_ticks ) {
                        sweep_run(start, settle_before, late_after_post,
                                  second_cfg, post_ticks);
                    }
                }
            }
        }
    }
}

/*----------------------------------------------------------------------------*/
/* Batch 2: fixed trajectories                                                 */
/*----------------------------------------------------------------------------*/

/** @brief Check-before-measure: nothing is published before the first check. */
static void test_first_value_after_check(void)
{
    scenario_begin();
    publish_config((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);
    drain();
    drain(); /* APPLY dispatched: Detecting entry, nothing published yet */
    CHECK(rec_drain() == 0U);

    tick(fake.enable_delay_ms + 1U);
    drain(); /* wire check completed, still nothing published from Detecting */
    CHECK(rec_drain() == 0U);

    tick(fake.disable_delay_ms + 1U);
    drain(); /* first sample: the reveal travels with the first value */
    {
        uint16_t n = rec_drain();

        CHECK(n == 1U);
    }
}

/** @brief A wire break is revealed when the check completes, then frozen. */
static void test_wire_break_reveal_and_freeze(void)
{
    QEvt const* e;
    uint16_t status;

    scenario_begin();
    fake.check_wire = ANALOG_INPUT_WIRE_BROKEN;
    drive_to_measuring((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);

    /* Check-before-measure: the first value already carries the break. */
    e = rec_take();
    CHECK(e != NULL);
    if ( e != NULL ) {
        measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

        status = m->status;
        CHECK(m->value == 0.0f); /* frozen: no good sample was ever taken */
        QF_gc(e);
    }
    CHECK(status == (uint16_t)REG_MAP_STATUS_TC_BREAK);
    rec_drain();

    /* Every later sample stays frozen on the same status. */
    tick(200U);
    drain();
    tick(200U);
    drain();
    {
        uint16_t frozen = 0U;
        uint16_t other  = 0U;

        while ( !QEQueue_isEmpty(&recorder.eQueue) ) {
            measure_evt_t const* mm;
            QEvt const* ee = QActive_get_(&recorder);

            mm = Q_EVT_CAST(measure_evt_t);
            if ( mm->status == (uint16_t)REG_MAP_STATUS_TC_BREAK ) {
                ++frozen;
            } else {
                ++other;
            }
            QF_gc(ee);
        }
        CHECK(frozen >= 2U);
        CHECK(other == 0U);
    }
}

/** @brief TC sampling follows the cold junction validity heartbeat. */
static void test_cjc_validity_gates_tc_samples(void)
{
    QEvt const* e;
    uint16_t status;

    scenario_begin();
    drive_to_measuring((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);
    rec_drain();

    publish_cjc(25.0f, 1U);
    drain();
    tick(101U);
    drain();
    rec_drain();

    publish_cjc(25.0f, 0U);
    drain();
    tick(101U);
    drain();
    e = rec_take();
    CHECK(e != NULL);
    if ( e != NULL ) {
        measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

        status = m->status;
        QF_gc(e);
    }
    CHECK(status == (uint16_t)REG_MAP_STATUS_NO_CJC);
    rec_drain();

    publish_cjc(25.0f, 1U);
    drain();
    tick(101U);
    drain();
    e = rec_take();
    CHECK(e != NULL);
    if ( e != NULL ) {
        measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

        status = m->status;
        QF_gc(e);
    }
    CHECK(status == (uint16_t)REG_MAP_STATUS_OK);
    rec_drain();
}

/** @brief PT100 lead, return and unresolvable breaks map to distinct codes. */
static void test_pt100_wire_codes(void)
{
    static const struct {
        analog_input_wire_status_t wire;
        uint16_t expected;
    } cases[] = {
        {ANALOG_INPUT_WIRE_BROKEN_LEAD, (uint16_t)REG_MAP_STATUS_RTD_LEAD_BREAK},
        {ANALOG_INPUT_WIRE_BROKEN_RETURN, (uint16_t)REG_MAP_STATUS_RTD_RETURN_BREAK},
        {ANALOG_INPUT_WIRE_BROKEN, (uint16_t)REG_MAP_STATUS_HW_ERROR}};
    size_t k;

    for ( k = 0; k < (sizeof(cases) / sizeof(cases[0])); ++k ) {
        QEvt const* e;
        uint16_t status;

        scenario_begin();
        fake.check_wire = cases[k].wire;
        drive_to_measuring((uint8_t)CH_TYPE_PT100, CH_TC_TYPE_NONE, 2U);

        /* The first check inside the drive already carries the verdict. */
        e = rec_take();
        CHECK(e != NULL);
        if ( e != NULL ) {
            measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

            status = m->status;
            QF_gc(e);
        }
        CHECK(status == cases[k].expected);
        rec_drain();
    }
}

/** @brief A rejected configuration leaves the AO parked and reporting. */
static void test_configure_failure_parks(void)
{
    scenario_begin();
    fake.configure_status = ANALOG_INPUT_ERR_IO;

    publish_config((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);
    drain();
    drain(); /* APPLY fails: status 6 published, still Unconfigured */

    CHECK(applied_count == 1U);
    CHECK(applied_ch == 0U);
    CHECK(applied_ok == 0U);

    {
        QEvt const* e = rec_take();

        CHECK(e != NULL);
        if ( e != NULL ) {
            measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

            CHECK(m->status == (uint16_t)REG_MAP_STATUS_HW_ERROR);
            QF_gc(e);
        }
    }
    rec_drain();

    /* No sampling while parked, but a rewritten configuration recovers. */
    tick(1000U);
    drain();
    CHECK(rec_drain() == 0U);

    fake.configure_status = ANALOG_INPUT_OK;
    publish_config((uint8_t)CH_TYPE_4_20MA, CH_TC_TYPE_NONE, 2U);
    drain();
    drain();
    tick(fake.enable_delay_ms + 1U); /* linear type: Settling only */
    drain();
    tick(fake.disable_delay_ms + 1U);
    drain();
    CHECK(rec_drain() >= 1U);
    CHECK(applied_count == 2U);
    CHECK(applied_ok == 1U);
}

/*----------------------------------------------------------------------------*/

int main(void)
{
    QF_init();
    QF_poolInit(pool_a, sizeof(pool_a), sizeof(pool_a[0]));
    QF_poolInit(pool_b, sizeof(pool_b), sizeof(pool_b[0]));

    test_reconfig_sweep();
    test_first_value_after_check();
    test_wire_break_reveal_and_freeze();
    test_cjc_validity_gates_tc_samples();
    test_pt100_wire_codes();
    test_configure_failure_parks();

    printf("test_channel_ao: %d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
