/**
 * @file modbus_ao.c
 * @brief Modbus RTU slave active object: routing and publishes only.
 *
 * The data space itself lives in the modbus_image component (typed
 * mirror, write transaction, fault word), which the slave core calls
 * directly in this AO's execution context. What remains here is the QP
 * wiring: boot load and CONFIG_SET_SIG publishes in the initial
 * transition, event routing in Serving, and turning commit reports
 * into publishes through the image's notifier.
 */

#include "modbus_ao.h"

#include <string.h>

#include "app_events.h"
#include "elog.h"
#include "modbus_image.h"
#include "param.h"

Q_DEFINE_THIS_MODULE("modbus_ao")

/* Static events posted from the ISR event port (QEVT_INITIALIZER, ROM). */
static QEvt const frameEvt     = QEVT_INITIALIZER(FRAME_SIG);
static QEvt const txDoneEvt    = QEVT_INITIALIZER(TX_DONE_SIG);
static QEvt const txTimeoutEvt = QEVT_INITIALIZER(TX_TIMEOUT_SIG);

static QState modbus_ao_initial(modbus_ao_t* const me, void const* const par);
static QState modbus_ao_serving(modbus_ao_t* const me, QEvt const* const e);

/** @brief CJC heartbeats between two queue water-level diagnostics. */
#define MODBUS_AO_DIAG_PERIOD_HEARTBEATS 60U

/**
 * @brief Report the low-water mark of every started AO's event queue.
 *
 * The capacity argument of docs/qp-application-design.md section 7 rests
 * on scheduling properties (QV dispatches one event per activation and
 * AoModbus is the highest-priority subscriber), not on queue headroom;
 * this diagnostic turns that argument into measured evidence. getQueueMin
 * reports the smallest free space seen since start - the boot burst and
 * the 8x100 Hz soak both leave their footprint in one number per AO.
 */
static void report_queue_watermarks(void)
{
    uint8_t prio;

    for ( prio = 2U; prio <= 11U; ++prio ) {
        uint16_t const min_free = QActive_getQueueMin(prio);

        if ( min_free != 0xFFFFU ) { /* unstarted AOs report sentinel */
            elog_debug("modbus_ao", "qmin prio=%u free=%u",
                       (unsigned)prio, (unsigned)min_free);
        }
    }
}

/**
 * @brief Publish one CONFIG_SET_SIG event after a committed write.
 *
 * Registered as the image's commit notifier; the image channel index
 * (zero-based, same base as the event payload) passes through directly.
 */
static void on_cfg_committed(void* ctx,
                             uint8_t ch,
                             const ch_config_t* cfg)
{
    modbus_ao_t* me = (modbus_ao_t*)ctx;
    config_set_evt_t* evt = Q_NEW(config_set_evt_t, CONFIG_SET_SIG);

    (void)me; /* the publish sender; referenced only in Q_SPY builds */

    evt->ch  = ch;
    evt->cfg = *cfg;
    QACTIVE_PUBLISH(&evt->super, Q_ACTIVE_UPCAST(me));
}

/** @brief Publish one configuration from the boot load. */
static void publish_boot_config(modbus_ao_t* me,
                                uint8_t ch,
                                const ch_config_t* cfg)
{
    config_set_evt_t* evt = Q_NEW(config_set_evt_t, CONFIG_SET_SIG);

    evt->ch  = ch;
    evt->cfg = *cfg;
    QACTIVE_PUBLISH(&evt->super, Q_ACTIVE_UPCAST(me));
}

/*----------------------------------------------------------------------------*/
/* ISR event port                                                             */
/*----------------------------------------------------------------------------*/

static void on_frame(void* ctx)
{
    modbus_ao_t* me = (modbus_ao_t*)ctx;

    QACTIVE_POST(Q_ACTIVE_UPCAST(me), &frameEvt, (void*)0);
}

static void on_sent(void* ctx)
{
    modbus_ao_t* me = (modbus_ao_t*)ctx;

    QACTIVE_POST(Q_ACTIVE_UPCAST(me), &txDoneEvt, (void*)0);
}

static void on_timeout(void* ctx)
{
    modbus_ao_t* me = (modbus_ao_t*)ctx;

    QACTIVE_POST(Q_ACTIVE_UPCAST(me), &txTimeoutEvt, (void*)0);
}

/*----------------------------------------------------------------------------*/
/* Construction                                                               */
/*----------------------------------------------------------------------------*/

/** @brief Report-Slave-ID payload; no per-instance state. */
static const uint8_t additional[] = "8CH-AI-RTU";

/** @brief Identity reported by function code 0x11. */
static const modbus_slave_identity_t identity = {
    0xFFU,                              /**< slave_id */
    1U,                                 /**< is_running */
    additional,                         /**< additional */
    (uint16_t)(sizeof(additional) - 1U) /**< additional_length */
};

void modbus_ao_ctor(modbus_ao_t* me)
{
    QActive_ctor(&me->super, Q_STATE_CAST(&modbus_ao_initial));

    modbus_image_init(&me->image);

    me->slave          = NULL;
    me->tx_done_cnt    = 0U;
    me->tx_timeout_cnt = 0U;
    me->diag_ctr       = 0U;

    /* The mirror lives in the image component; so do the callbacks. */
    modbus_image_set_cfg_notifier(&me->image, &on_cfg_committed, me);
}

const modbus_slave_callbacks_t* modbus_ao_callbacks(const modbus_ao_t* me)
{
    return modbus_image_callbacks(&((modbus_ao_t*)me)->image);
}

const modbus_slave_identity_t* modbus_ao_identity(void)
{
    return &identity;
}

void modbus_ao_attach(modbus_ao_t* me,
                      modbus_slave_t* slave)
{
    me->slave = slave;

    /* ISR events become AO events; attach requires the stopped state. */
    me->ev.ctx               = me;
    me->ev.on_frame_received = on_frame;
    me->ev.on_frame_sent     = on_sent;
    me->ev.on_tx_timeout     = on_timeout;
    (void)modbus_slave_set_event_port(me->slave, &me->ev);
}

/**
 * @brief Top-most initial transition: boot load, then serve.
 *
 * Runs inside QActive_start(), before QF_run(): the loaded configurations
 * are published while every channel is already started and subscribed.
 */
static QState modbus_ao_initial(modbus_ao_t* const me, void const* const par)
{
    param_boot_result_t res;
    uint8_t ch;

    Q_UNUSED_PAR(par);

    /* Subscriptions live in the initial transition (QP/C example style). */
    QActive_subscribe(Q_ACTIVE_UPCAST(me), MEASURE_SIG);
    QActive_subscribe(Q_ACTIVE_UPCAST(me), CJC_SIG);

    param_boot_load(&res);
    modbus_image_boot(&me->image, &res);

    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        if ( res.cfg_valid[ch] != 0U ) {
            publish_boot_config(me, ch, &res.cfg[ch]);
        }
    }

    return Q_TRAN(&modbus_ao_serving);
}

/**
 * @brief Serving: the resident service state after the core starts.
 */
static QState modbus_ao_serving(modbus_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            if ( modbus_slave_start(me->slave) != MODBUS_SLAVE_OK ) {
                /* Board-level defect; the port was vetted at construction. */
                Q_ERROR();
            }
            status = Q_HANDLED();
            break;
        }
        case FRAME_SIG: {
            (void)modbus_slave_advance(me->slave);
            status = Q_HANDLED();
            break;
        }
        case TX_DONE_SIG: {
            ++me->tx_done_cnt;
            status = Q_HANDLED();
            break;
        }
        case TX_TIMEOUT_SIG: {
            ++me->tx_timeout_cnt;
            status = Q_HANDLED();
            break;
        }
        case MEASURE_SIG: {
            measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

            modbus_image_set_measure(&me->image, m->ch, m->value, m->status);
            status = Q_HANDLED();
            break;
        }
        case CJC_SIG: {
            cjc_evt_t const* cjc = Q_EVT_CAST(cjc_evt_t);

            modbus_image_set_cjc(&me->image, cjc->temp_c, cjc->valid);

            /* The 1 Hz heartbeat doubles as the diagnostic cadence. */
            ++me->diag_ctr;
            if ( me->diag_ctr >= MODBUS_AO_DIAG_PERIOD_HEARTBEATS ) {
                me->diag_ctr = 0U;
                report_queue_watermarks();
            }
            status = Q_HANDLED();
            break;
        }
        default: {
            status = Q_SUPER(&QHsm_top);
            break;
        }
    }
    return status;
}
