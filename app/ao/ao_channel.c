/**
 * @file ao_channel.c
 * @brief Channel acquisition active object state machine (QHsm, hand coded).
 *
 * Reconfiguration is structurally isolated (docs/ao/ao_channel.md section 6):
 * SIG_CONFIG_SET only caches the pending configuration, posts SIG_APPLY and
 * leaves for Unconfigured; the real configure() runs on the next dispatch,
 * so every late timer event lands in Unconfigured and is ignored there.
 */

#include "ao_channel.h"

#include "analog_input_board.h"

#include "ao_events.h"
#include "elog.h"
#include "reg_map.h"

Q_DEFINE_THIS_MODULE("ao_channel")

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

static QState ao_channel_initial(ao_channel_t* const me,
                                 void const* const par);
static QState ao_channel_root(ao_channel_t* const me,
                              QEvt const* const e);
static QState ao_channel_unconfigured(ao_channel_t* const me,
                                      QEvt const* const e);
static QState ao_channel_working(ao_channel_t* const me,
                                 QEvt const* const e);
static QState ao_channel_detecting(ao_channel_t* const me,
                                   QEvt const* const e);
static QState ao_channel_settling(ao_channel_t* const me,
                                  QEvt const* const e);
static QState ao_channel_measuring(ao_channel_t* const me,
                                   QEvt const* const e);

/**
 * @brief Report whether the input type runs the wire break detection loop.
 */
static bool ao_channel_detectable(const ao_channel_t* me)
{
    return (me->cfg.type == (uint8_t)CH_TYPE_TC)
           || (me->cfg.type == (uint8_t)CH_TYPE_PT100);
}

/**
 * @brief Publish one SIG_MEASURE event (frozen value on fault codes).
 */
static void ao_channel_publish(ao_channel_t* me,
                               float value,
                               uint16_t status)
{
    measure_evt_t* evt = Q_NEW(measure_evt_t, SIG_MEASURE);

    evt->ch     = me->ch;
    evt->value  = value;
    evt->status = status;
    QACTIVE_PUBLISH(&evt->super, &me->super);
}

/**
 * @brief One sampling step: read, convert, calibrate, publish.
 */
static void ao_channel_do_sample(ao_channel_t* me)
{
    analog_input_conversion_context_t ctx;
    int32_t code;
    float raw;
    float value;

    if ( me->wire != (uint8_t)ANALOG_INPUT_WIRE_NORMAL ) {
        /* Frozen value, frozen wire verdict until the next check. */
        ao_channel_publish(me, me->last_value,
                           reg_map_wire_status_code(me->cfg.type, me->wire));
        return;
    }

    if ( analog_input_read_sample(me->input, &code) != ANALOG_INPUT_OK ) {
        ao_channel_publish(me, me->last_value, REG_MAP_STATUS_HW_ERROR);
        return;
    }

    if ( (me->cfg.type == (uint8_t)CH_TYPE_TC) && (me->cjc_valid == 0U) ) {
        ao_channel_publish(me, me->last_value, REG_MAP_STATUS_NO_CJC);
        return;
    }

    ctx.cold_junction = me->cjc_temp;
    ctx.linear_min    = me->cfg.linear_min;
    ctx.linear_max    = me->cfg.linear_max;

    if ( analog_input_convert(me->input, code, &ctx, &raw) != ANALOG_INPUT_OK ) {
        ao_channel_publish(me, me->last_value, REG_MAP_STATUS_OUT_OF_RANGE);
        return;
    }

    value         = (raw * me->cfg.gain) + me->cfg.offset;
    me->last_value = value;
    ao_channel_publish(me, value, (uint16_t)REG_MAP_STATUS_OK);
}

/**
 * @brief Apply the pending configuration: the SIG_APPLY re-entry step.
 *
 * @return true when the driver accepted the configuration.
 */
static bool ao_channel_apply_pending(ao_channel_t* me)
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
        ao_channel_publish(me, me->last_value, REG_MAP_STATUS_HW_ERROR);
        return false;
    }

    me->cfg       = me->pending;
    me->settle_ms = delay;
    elog_debug("ao_channel", "ch%u configured: type=%u rate=%u",
               (unsigned)me->ch, (unsigned)me->cfg.type,
               (unsigned)me->cfg.rate_code);
    return true;
}

void ao_channel_ctor(ao_channel_t* me,
                     uint8_t ch)
{
    QActive_ctor(&me->super, Q_STATE_CAST(&ao_channel_initial));
    QTimeEvt_ctorX(&me->te_sample, &me->super, SIG_SAMPLE, 0U);
    QTimeEvt_ctorX(&me->te_settle, &me->super, SIG_SETTLE, 0U);
    QTimeEvt_ctorX(&me->te_break, &me->super, SIG_BREAK_WINDOW, 0U);
    QEvt_ctor(&me->applyEvt, SIG_APPLY);

    me->input = analog_input_board_get(ch);
    Q_ASSERT(me->input != NULL);

    me->ch        = ch;
    me->cjc_temp  = 0.0f;
    me->last_value = 0.0f;
    me->settle_ms = 0U;
    me->cjc_valid = 0U;
    me->wire      = (uint8_t)ANALOG_INPUT_WIRE_NORMAL;

    /* Filled by SIG_CONFIG_SET before the measurement path is ever used. */
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
 * @brief Top-most initial transition: report status 1 once, park.
 */
static QState ao_channel_initial(ao_channel_t* const me,
                                 void const* const par)
{
    Q_UNUSED_PAR(par);

    /* Initial transition action: exactly one status-1 report. */
    ao_channel_publish(me, 0.0f, (uint16_t)REG_MAP_STATUS_UNCONFIGURED);
    return Q_TRAN(&ao_channel_unconfigured);
}

/**
 * @brief Root state: common handling present in every state.
 */
static QState ao_channel_root(ao_channel_t* const me,
                              QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case SIG_CJC: {
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
 * The only exit is SIG_APPLY. No timer is ever armed here, so a late timer
 * event always lands in this state and is ignored (ao_channel.md section 6).
 */
static QState ao_channel_unconfigured(ao_channel_t* const me,
                                      QEvt const* const e)
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
        case SIG_CONFIG_SET: {
            if ( Q_EVT_CAST(config_set_evt_t)->ch == me->ch ) {
                me->pending = Q_EVT_CAST(config_set_evt_t)->cfg;
                QACTIVE_POST(&me->super, &me->applyEvt, &me->super);
            }
            /* Consecutive reconfigurations merge into the latest pending. */
            status = Q_HANDLED();
            break;
        }
        case SIG_APPLY: {
            if ( ao_channel_apply_pending(me) ) {
                /* Success re-enters the measurement path via Working. */
                status = Q_TRAN(&ao_channel_working);
            }
            else {
                /* Failure: report status 6 (done inside) and wait. */
                status = Q_HANDLED();
            }
            break;
        }
        default: {
            status = Q_SUPER(&ao_channel_root);
            break;
        }
    }
    return status;
}

/**
 * @brief Working: configured and running; common reconfiguration entry.
 */
static QState ao_channel_working(ao_channel_t* const me,
                                 QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_INIT_SIG: {
            /* Initial target is decided by the configured input type. */
            status = ao_channel_detectable(me)
                ? Q_TRAN(&ao_channel_detecting)
                : Q_TRAN(&ao_channel_settling);
            break;
        }
        case SIG_CONFIG_SET: {
            if ( Q_EVT_CAST(config_set_evt_t)->ch == me->ch ) {
                /* Reconfiguration isolation: cache, post, leave. */
                me->pending = Q_EVT_CAST(config_set_evt_t)->cfg;
                QACTIVE_POST(&me->super, &me->applyEvt, &me->super);
                status = Q_TRAN(&ao_channel_unconfigured);
            }
            else {
                status = Q_HANDLED();
            }
            break;
        }
        default: {
            status = Q_SUPER(&ao_channel_root);
            break;
        }
    }
    return status;
}

/**
 * @brief Detecting: hardware in wire break detection mode, value frozen.
 */
static QState ao_channel_detecting(ao_channel_t* const me,
                                   QEvt const* const e)
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
        case SIG_SETTLE: {
            analog_input_wire_status_t ws = (analog_input_wire_status_t)me->wire;
            uint32_t delay = 0U;

            if ( analog_input_check_wire_break(me->input, &ws) == ANALOG_INPUT_OK ) {
                uint16_t code;

                me->wire = (uint8_t)ws;
                code = reg_map_wire_status_code(me->cfg.type, me->wire);
                if ( code != (uint16_t)REG_MAP_STATUS_OK ) {
                    /* A break is revealed the moment the check completes. */
                    ao_channel_publish(me, me->last_value, code);
                    elog_debug("ao_channel", "ch%u wire break: %u",
                               (unsigned)me->ch, (unsigned)code);
                }
                /* Intact: status 0 travels with the first measurement. */
            }
            else {
                /* IO failure: report and retry on the following beats. */
                ao_channel_publish(me, me->last_value, REG_MAP_STATUS_HW_ERROR);
            }

            (void)analog_input_disable_break_detection(me->input, &delay);
            me->settle_ms = delay;
            status = Q_TRAN(&ao_channel_settling);
            break;
        }
        default: {
            status = Q_SUPER(&ao_channel_working);
            break;
        }
    }
    return status;
}

/**
 * @brief Settling: measurement mode, waiting for the first valid conversion.
 */
static QState ao_channel_settling(ao_channel_t* const me,
                                  QEvt const* const e)
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
        case SIG_SETTLE: {
            /* The first sample lands exactly on the settle point. */
            ao_channel_do_sample(me);
            status = Q_TRAN(&ao_channel_measuring);
            break;
        }
        default: {
            status = Q_SUPER(&ao_channel_working);
            break;
        }
    }
    return status;
}

/**
 * @brief Measuring: periodic sampling, plus the 3 s wire break window.
 */
static QState ao_channel_measuring(ao_channel_t* const me,
                                   QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            uint32_t ticks = ch_config_rate_ticks(me->cfg.rate_code);

            QTimeEvt_armX(&me->te_sample, ticks, ticks);
            if ( ao_channel_detectable(me) ) {
                QTimeEvt_armX(&me->te_break, AO_CHANNEL_BREAK_WINDOW_MS,
                              AO_CHANNEL_BREAK_WINDOW_MS);
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
        case SIG_SAMPLE: {
            /* Periodic timer: nothing to re-arm. */
            ao_channel_do_sample(me);
            status = Q_HANDLED();
            break;
        }
        case SIG_BREAK_WINDOW: {
            /* Mode switch happens in Detecting's entry action. */
            status = Q_TRAN(&ao_channel_detecting);
            break;
        }
        default: {
            status = Q_SUPER(&ao_channel_working);
            break;
        }
    }
    return status;
}
