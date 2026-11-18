/**
 * @file cold_junction_ao.c
 * @brief Cold junction temperature active object (TMP75, 1 Hz heartbeat).
 */

#include "cold_junction_ao.h"

#include "app_events.h"

Q_DEFINE_THIS_MODULE("cold_junction_ao")

static QState cold_junction_ao_initial(cold_junction_ao_t* const me, void const* const par);
static QState cold_junction_ao_starting(cold_junction_ao_t* const me, QEvt const* const e);
static QState cold_junction_ao_sampling(cold_junction_ao_t* const me, QEvt const* const e);

/**
 * @brief Read the TMP75 once and publish the CJC_SIG heartbeat.
 *
 * Validity follows docs/ao/cold_junction_ao.md section 2: valid requires a
 * successful reading to exist and fewer than COLD_JUNCTION_AO_ERR_LIMIT
 * consecutive failures.
 */
static void sample(cold_junction_ao_t* me)
{
    float temp = 0.0f;
    uint8_t valid;

    if ( (tmp75_read_temperature(me->dev, &temp) == TMP75_OK)
         && (temp >= COLD_JUNCTION_AO_MIN_TEMP_C)
         && (temp <= COLD_JUNCTION_AO_MAX_TEMP_C) ) {
        me->err_cnt      = 0U;
        me->have_reading = 1U;
        me->temp_c       = temp;
    }
    else if ( me->err_cnt < 0xFFU ) {
        /* Saturating counter: past the limit the value never matters. */
        ++me->err_cnt;
    }

    valid = (uint8_t)((me->have_reading != 0U)
                      && (me->err_cnt < COLD_JUNCTION_AO_ERR_LIMIT));

    {
        cjc_evt_t* evt = Q_NEW(cjc_evt_t, CJC_SIG);

        evt->temp_c = me->temp_c;
        evt->valid  = valid;
        QACTIVE_PUBLISH(&evt->super, Q_ACTIVE_UPCAST(me));
    }
}

void cold_junction_ao_ctor(cold_junction_ao_t* me,
                           tmp75_t* dev)
{
    QActive_ctor(&me->super, Q_STATE_CAST(&cold_junction_ao_initial));
    QTimeEvt_ctorX(&me->te_tick, Q_ACTIVE_UPCAST(me), TICK_SIG, 0U);
    me->dev          = dev;
    Q_ASSERT(me->dev != NULL);
    me->temp_c       = 0.0f;
    me->err_cnt      = 0U;
    me->have_reading = 0U;
}

/**
 * @brief Top-most initial transition: boot into the Starting transient.
 */
static QState cold_junction_ao_initial(cold_junction_ao_t* const me, void const* const par)
{
    Q_UNUSED_PAR(par);
    return Q_TRAN(&cold_junction_ao_starting);
}

/**
 * @brief Starting: one first reading, then the steady state.
 *
 * The nested initial transition into Sampling runs as soon as the entry
 * action completes (QHsm nested initial transition).
 */
static QState cold_junction_ao_starting(cold_junction_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            sample(me);
            QTimeEvt_armX(&me->te_tick, COLD_JUNCTION_AO_TICK_MS,
                          COLD_JUNCTION_AO_TICK_MS);
            status = Q_HANDLED();
            break;
        }
        case Q_INIT_SIG: {
            status = Q_TRAN(&cold_junction_ao_sampling);
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
 * @brief Sampling: one reading per 1 Hz tick.
 */
static QState cold_junction_ao_sampling(cold_junction_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case TICK_SIG: {
            sample(me);
            status = Q_HANDLED();
            break;
        }
        default: {
            /* Starting is the superstate of Sampling (nested initial). */
            status = Q_SUPER(&cold_junction_ao_starting);
            break;
        }
    }
    return status;
}
