/**
 * @file ao_cold_junction.c
 * @brief Cold junction temperature active object (TMP75, 1 Hz heartbeat).
 */

#include "ao_cold_junction.h"

#include "ao_events.h"
#include "tmp75_board.h"

Q_DEFINE_THIS_MODULE("ao_cold_junction")

static QState ao_cold_junction_initial(ao_cold_junction_t* const me,
                                       void const* const par);
static QState ao_cold_junction_starting(ao_cold_junction_t* const me,
                                        QEvt const* const e);
static QState ao_cold_junction_sampling(ao_cold_junction_t* const me,
                                        QEvt const* const e);

/**
 * @brief Read the TMP75 once and publish the SIG_CJC heartbeat.
 *
 * Validity follows docs/ao/ao_cold_junction.md section 2: valid requires a
 * successful reading to exist and fewer than AO_COLD_JUNCTION_ERR_LIMIT
 * consecutive failures.
 */
static void ao_cold_junction_sample(ao_cold_junction_t* me)
{
    tmp75_t* dev = tmp75_board_get_device();
    float temp = 0.0f;
    uint8_t valid;

    Q_ASSERT(dev != NULL);

    if ( (tmp75_read_temperature(dev, &temp) == TMP75_OK)
         && (temp >= AO_COLD_JUNCTION_MIN_TEMP_C)
         && (temp <= AO_COLD_JUNCTION_MAX_TEMP_C) ) {
        me->err_cnt      = 0U;
        me->have_reading = 1U;
        me->temp_c       = temp;
    }
    else if ( me->err_cnt < 0xFFU ) {
        /* Saturating counter: past the limit the value never matters. */
        ++me->err_cnt;
    }

    valid = (uint8_t)((me->have_reading != 0U)
                      && (me->err_cnt < AO_COLD_JUNCTION_ERR_LIMIT));

    {
        cjc_evt_t* evt = Q_NEW(cjc_evt_t, SIG_CJC);

        evt->temp_c = me->temp_c;
        evt->valid  = valid;
        QACTIVE_PUBLISH(&evt->super, &me->super);
    }
}

void ao_cold_junction_ctor(ao_cold_junction_t* me)
{
    QActive_ctor(&me->super, Q_STATE_CAST(&ao_cold_junction_initial));
    QTimeEvt_ctorX(&me->te_tick, &me->super, SIG_TICK, 0U);
    me->temp_c       = 0.0f;
    me->err_cnt      = 0U;
    me->have_reading = 0U;
}

/**
 * @brief Top-most initial transition: boot into the Starting transient.
 */
static QState ao_cold_junction_initial(ao_cold_junction_t* const me,
                                       void const* const par)
{
    Q_UNUSED_PAR(par);
    return Q_TRAN(&ao_cold_junction_starting);
}

/**
 * @brief Starting: one first reading, then the steady state.
 *
 * The nested initial transition into Sampling runs as soon as the entry
 * action completes (QHsm nested initial transition).
 */
static QState ao_cold_junction_starting(ao_cold_junction_t* const me,
                                        QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            ao_cold_junction_sample(me);
            QTimeEvt_armX(&me->te_tick, AO_COLD_JUNCTION_TICK_MS,
                          AO_COLD_JUNCTION_TICK_MS);
            status = Q_HANDLED();
            break;
        }
        case Q_INIT_SIG: {
            status = Q_TRAN(&ao_cold_junction_sampling);
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
static QState ao_cold_junction_sampling(ao_cold_junction_t* const me,
                                        QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case SIG_TICK: {
            ao_cold_junction_sample(me);
            status = Q_HANDLED();
            break;
        }
        default: {
            /* Starting is the superstate of Sampling (nested initial). */
            status = Q_SUPER(&ao_cold_junction_starting);
            break;
        }
    }
    return status;
}
