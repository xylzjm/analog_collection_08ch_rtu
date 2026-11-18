/**
 * @file channel_ao.c
 * @brief Channel acquisition active object state machine (QHsm, hand coded).
 *
 * Reconfiguration is structurally isolated (docs/ao/channel_ao.md section 6):
 * CONFIG_SET_SIG disarms the timers first, caches the pending configuration,
 * posts APPLY_SIG and leaves for Unconfigured; the real configure() runs on
 * the next dispatch, so every late timer event lands in Unconfigured and is
 * ignored there.
 */

#include "channel_ao.h"

#include "app_events.h"
#include "elog.h"
#include "reg_map.h"

Q_DEFINE_THIS_MODULE("channel_ao")

/** @brief Reconfiguration re-entry event; shared by all eight instances. */
static QEvt const applyEvt = QEVT_INITIALIZER(APPLY_SIG);

/* reg_map_wire_status_code() relies on this wire result encoding. */
Q_ASSERT_STATIC(ANALOG_INPUT_WIRE_NORMAL == 0);
Q_ASSERT_STATIC(ANALOG_INPUT_WIRE_BROKEN == 1);
Q_ASSERT_STATIC(ANALOG_INPUT_WIRE_BROKEN_LEAD == 2);
Q_ASSERT_STATIC(ANALOG_INPUT_WIRE_BROKEN_RETURN == 3);

/* The channel type codes must match the analog_input_type_t encoding. */
Q_ASSERT_STATIC((int)CH_TYPE_TC == (int)ANALOG_INPUT_TYPE_THERMOCOUPLE);
Q_ASSERT_STATIC((int)CH_TYPE_PT100 == (int)ANALOG_INPUT_TYPE_PT100);
Q_ASSERT_STATIC((int)CH_TYPE_4_20MA == (int)ANALOG_INPUT_TYPE_4_20MA);
Q_ASSERT_STATIC((int)CH_TYPE_0_10V == (int)ANALOG_INPUT_TYPE_0_10V);

static QState channel_ao_initial(channel_ao_t* const me, void const* const par);
static QState channel_ao_root(channel_ao_t* const me, QEvt const* const e);
static QState channel_ao_unconfigured(channel_ao_t* const me, QEvt const* const e);
static QState channel_ao_working(channel_ao_t* const me, QEvt const* const e);
static QState channel_ao_detecting(channel_ao_t* const me, QEvt const* const e);
static QState channel_ao_settling(channel_ao_t* const me, QEvt const* const e);
static QState channel_ao_measuring(channel_ao_t* const me, QEvt const* const e);

/**
 * @brief Report whether the input type runs the wire break detection loop.
 */
static bool detectable(const channel_ao_t* me)
{
    return (me->cfg.type == (uint8_t)CH_TYPE_TC)
           || (me->cfg.type == (uint8_t)CH_TYPE_PT100);
}

/**
 * @brief Publish one MEASURE_SIG event (frozen value on fault codes).
 */
static void publish(channel_ao_t* me, float value, uint16_t status)
{
    measure_evt_t* evt = Q_NEW(measure_evt_t, MEASURE_SIG);

    evt->ch     = me->ch;
    evt->value  = value;
    evt->status = status;
    QACTIVE_PUBLISH(&evt->super, Q_ACTIVE_UPCAST(me));
}

/**
 * @brief One sampling step: read, convert, calibrate, publish.
 */
static void do_sample(channel_ao_t* me)
{
    analog_input_conversion_context_t ctx;
    int32_t code;
    float raw;
    float value;

    if ( me->wire != (uint8_t)ANALOG_INPUT_WIRE_NORMAL ) {
        /* Frozen value, frozen wire verdict until the next check. */
        publish(me, me->last_value,
                           reg_map_wire_status_code(me->cfg.type, me->wire));
        return;
    }

    if ( analog_input_read_sample(me->input, &code) != ANALOG_INPUT_OK ) {
        publish(me, me->last_value, (uint16_t)REG_MAP_STATUS_HW_ERROR);
        return;
    }

    if ( (me->cfg.type == (uint8_t)CH_TYPE_TC) && (me->cjc_valid == 0U) ) {
        publish(me, me->last_value, (uint16_t)REG_MAP_STATUS_NO_CJC);
        return;
    }

    ctx.cold_junction = me->cjc_temp;
    ctx.linear_min    = me->cfg.linear_min;
    ctx.linear_max    = me->cfg.linear_max;

    if ( analog_input_convert(me->input, code, &ctx, &raw) != ANALOG_INPUT_OK ) {
        publish(me, me->last_value, (uint16_t)REG_MAP_STATUS_OUT_OF_RANGE);
        return;
    }

    value         = (raw * me->cfg.gain) + me->cfg.offset;
    me->last_value = value;
    publish(me, value, (uint16_t)REG_MAP_STATUS_OK);
}

/**
 * @brief Apply the pending configuration: the APPLY_SIG re-entry step.
 *
 * @return true when the driver accepted the configuration.
 */
static bool apply_pending(channel_ao_t* me)
{
    analog_input_config_t acfg;
    uint32_t delay = 0U;

    acfg.type                  = (analog_input_type_t)me->pending.type;
    acfg.thermocouple_type     = (me->pending.type == (uint8_t)CH_TYPE_TC)
        ? (analog_input_thermocouple_type_t)me->pending.tc_type
        : ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE;
    acfg.sample_rate_millihz   = ch_config_rate_millihz(me->pending.rate_code);

    if ( analog_input_configure(me->input, &acfg, &delay) != ANALOG_INPUT_OK ) {
        /* Legal exception: report and stay unconfigured until rewritten. */
        publish(me, me->last_value, (uint16_t)REG_MAP_STATUS_HW_ERROR);
        return false;
    }

    me->cfg       = me->pending;
    me->settle_ms = delay;
    elog_debug("channel_ao", "ch%u configured: type=%u rate=%u",
               (unsigned)me->ch, (unsigned)me->cfg.type,
               (unsigned)me->cfg.rate_code);
    return true;
}

void channel_ao_ctor(channel_ao_t* me,
                     uint8_t ch,
                     analog_input_t* input)
{
    QActive_ctor(&me->super, Q_STATE_CAST(&channel_ao_initial));
    QTimeEvt_ctorX(&me->te_sample, Q_ACTIVE_UPCAST(me), SAMPLE_SIG, 0U);
    QTimeEvt_ctorX(&me->te_settle, Q_ACTIVE_UPCAST(me), SETTLE_SIG, 0U);
    QTimeEvt_ctorX(&me->te_break, Q_ACTIVE_UPCAST(me), BREAK_WINDOW_SIG, 0U);

    me->input = input;
    Q_ASSERT(me->input != NULL);

    me->ch        = ch;
    me->cjc_temp  = 0.0f;
    me->last_value = 0.0f;
    me->settle_ms = 0U;
    me->cjc_valid = 0U;
    me->wire      = (uint8_t)ANALOG_INPUT_WIRE_NORMAL;

    /* Filled by CONFIG_SET_SIG before the measurement path is ever used. */
    me->cfg.type       = (uint8_t)CH_TYPE_TC;
    me->cfg.tc_type    = CH_TC_TYPE_NONE;
    me->cfg.rate_code  = 0U;
    me->cfg.linear_min = 0.0f;
    me->cfg.linear_max = 0.0f;
    me->cfg.offset     = 0.0f;
    me->cfg.gain       = 1.0f;
    me->pending        = me->cfg;
}

/**
 * @brief Top-most initial transition: subscribe and park.
 *
 * No status-1 report here: the boot publish would precede the AoModbus
 * subscription and be dropped. The mirror initial value (status = 1) is the
 * mechanism (docs/ao/channel_ao.md).
 */
static QState channel_ao_initial(channel_ao_t* const me, void const* const par)
{
    Q_UNUSED_PAR(par);

    /* Subscriptions live in the initial transition (QP/C example style). */
    QActive_subscribe(Q_ACTIVE_UPCAST(me), CONFIG_SET_SIG);
    QActive_subscribe(Q_ACTIVE_UPCAST(me), CJC_SIG);

    return Q_TRAN(&channel_ao_unconfigured);
}

/**
 * @brief Root state: common handling present in every state.
 */
static QState channel_ao_root(channel_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case CJC_SIG: {
            cjc_evt_t const* cjc = Q_EVT_CAST(cjc_evt_t);

            me->cjc_temp  = cjc->temp_c;
            me->cjc_valid = cjc->valid;
            status        = Q_HANDLED();
            break;
        }
        default: {
            status = Q_SUPER(&QHsm_top);
            break;
        }
    }
    return status;
}

/**
 * @brief Unconfigured: no effective configuration, the reconfiguration gate.
 *
 * The only exit is APPLY_SIG. No timer is ever armed here, so a late timer
 * event always lands in this state and is ignored (channel_ao.md section 6).
 */
static QState channel_ao_unconfigured(channel_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            /* Unconditional disarm; disarming a stopped timer is harmless. */
            (void)QTimeEvt_disarm(&me->te_sample);
            (void)QTimeEvt_disarm(&me->te_settle);
            (void)QTimeEvt_disarm(&me->te_break);
            status = Q_HANDLED();
            break;
        }
        case CONFIG_SET_SIG: {
            if ( Q_EVT_CAST(config_set_evt_t)->ch == me->ch ) {
                me->pending = Q_EVT_CAST(config_set_evt_t)->cfg;
                QACTIVE_POST(Q_ACTIVE_UPCAST(me), &applyEvt, Q_ACTIVE_UPCAST(me));
            }
            /* Consecutive reconfigurations merge into the latest pending. */
            status = Q_HANDLED();
            break;
        }
        case APPLY_SIG: {
            if ( apply_pending(me) ) {
                /* Success re-enters the measurement path via Working. */
                status = Q_TRAN(&channel_ao_working);
            }
            else {
                /* Failure: report status 6 (done inside) and wait. */
                status = Q_HANDLED();
            }
            break;
        }
        default: {
            status = Q_SUPER(&channel_ao_root);
            break;
        }
    }
    return status;
}

/**
 * @brief Working: configured and running; common reconfiguration entry.
 */
static QState channel_ao_working(channel_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_INIT_SIG: {
            /* Initial target is decided by the configured input type. */
            status = detectable(me)
                ? Q_TRAN(&channel_ao_detecting)
                : Q_TRAN(&channel_ao_settling);
            break;
        }
        case CONFIG_SET_SIG: {
            if ( Q_EVT_CAST(config_set_evt_t)->ch == me->ch ) {
                /*
                 * Reconfiguration isolation. Disarm before posting APPLY_SIG:
                 * a tick landing between post and disarm could queue a late
                 * SETTLE behind APPLY_SIG and reach the re-entered leaf
                 * (channel_ao.md section 6).
                 */
                (void)QTimeEvt_disarm(&me->te_sample);
                (void)QTimeEvt_disarm(&me->te_settle);
                (void)QTimeEvt_disarm(&me->te_break);
                me->pending = Q_EVT_CAST(config_set_evt_t)->cfg;
                QACTIVE_POST(Q_ACTIVE_UPCAST(me), &applyEvt, Q_ACTIVE_UPCAST(me));
                status = Q_TRAN(&channel_ao_unconfigured);
            }
            else {
                status = Q_HANDLED();
            }
            break;
        }
        default: {
            status = Q_SUPER(&channel_ao_root);
            break;
        }
    }
    return status;
}

/**
 * @brief Detecting: hardware in wire break detection mode, value frozen.
 */
static QState channel_ao_detecting(channel_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            uint32_t delay = 0U;

            /* No publish here: the reveal happens when the check completes. */
            (void)analog_input_enable_break_detection(me->input, &delay);
            QTimeEvt_armX(&me->te_settle, (delay != 0U) ? delay : 1U, 0U);
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            (void)QTimeEvt_disarm(&me->te_settle);
            status = Q_HANDLED();
            break;
        }
        case SETTLE_SIG: {
            analog_input_wire_status_t ws = (analog_input_wire_status_t)me->wire;
            uint32_t delay = 0U;

            if ( analog_input_check_wire_break(me->input, &ws) == ANALOG_INPUT_OK ) {
                uint16_t code;

                me->wire = (uint8_t)ws;
                code = reg_map_wire_status_code(me->cfg.type, me->wire);
                if ( code != (uint16_t)REG_MAP_STATUS_OK ) {
                    /* A break is revealed the moment the check completes. */
                    publish(me, me->last_value, code);
                    elog_debug("channel_ao", "ch%u wire break: %u",
                               (unsigned)me->ch, (unsigned)code);
                }
                /* Intact: status 0 travels with the first measurement. */
            }
            else {
                /* IO failure: report and retry on the following beats. */
                publish(me, me->last_value, (uint16_t)REG_MAP_STATUS_HW_ERROR);
            }

            (void)analog_input_disable_break_detection(me->input, &delay);
            me->settle_ms = delay;
            status = Q_TRAN(&channel_ao_settling);
            break;
        }
        default: {
            status = Q_SUPER(&channel_ao_working);
            break;
        }
    }
    return status;
}

/**
 * @brief Settling: measurement mode, waiting for the first valid conversion.
 */
static QState channel_ao_settling(channel_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            QTimeEvt_armX(&me->te_settle,
                          (me->settle_ms != 0U) ? me->settle_ms : 1U, 0U);
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            (void)QTimeEvt_disarm(&me->te_settle);
            status = Q_HANDLED();
            break;
        }
        case SETTLE_SIG: {
            /* The first sample lands exactly on the settle point. */
            do_sample(me);
            status = Q_TRAN(&channel_ao_measuring);
            break;
        }
        default: {
            status = Q_SUPER(&channel_ao_working);
            break;
        }
    }
    return status;
}

/**
 * @brief Measuring: periodic sampling, plus the 3 s wire break window.
 */
static QState channel_ao_measuring(channel_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            uint32_t ticks = ch_config_rate_ticks(me->cfg.rate_code);

            QTimeEvt_armX(&me->te_sample, ticks, ticks);
            if ( detectable(me) ) {
                QTimeEvt_armX(&me->te_break, CHANNEL_AO_BREAK_WINDOW_MS,
                              CHANNEL_AO_BREAK_WINDOW_MS);
            }
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            (void)QTimeEvt_disarm(&me->te_sample);
            (void)QTimeEvt_disarm(&me->te_break);
            status = Q_HANDLED();
            break;
        }
        case SAMPLE_SIG: {
            /* Periodic timer: nothing to re-arm. */
            do_sample(me);
            status = Q_HANDLED();
            break;
        }
        case BREAK_WINDOW_SIG: {
            /* Mode switch happens in Detecting's entry action. */
            status = Q_TRAN(&channel_ao_detecting);
            break;
        }
        default: {
            status = Q_SUPER(&channel_ao_working);
            break;
        }
    }
    return status;
}
