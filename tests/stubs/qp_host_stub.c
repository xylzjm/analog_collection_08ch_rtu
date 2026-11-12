#include "qp_host_stub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_app.h"

#define TEST_POSTED_MAX 128U
#define TEST_DEFERRED_MAX 16U

typedef struct {
    QActive* target;
    QEvt const* event;
} posted_event_t;

static posted_event_t posted[TEST_POSTED_MAX];
static uint16_t posted_count;

static QEvt const* deferred[TEST_DEFERRED_MAX];
static uint16_t deferred_count;

static enum_t subscribed[TEST_DEFERRED_MAX];
static uint16_t subscribed_count;

QEvt const* test_qp_pop(QActive* target)
{
    uint16_t index;

    for ( index = 0U; index < posted_count; ++index ) {
        if ( posted[index].target == target ) {
            QEvt const* event = posted[index].event;

            memmove(&posted[index], &posted[index + 1U],
                    (size_t)(posted_count - index - 1U) * sizeof(posted[0]));
            --posted_count;
            return event;
        }
    }
    return NULL;
}

uint16_t test_qp_count(QActive* target)
{
    uint16_t index;
    uint16_t count = 0U;

    for ( index = 0U; index < posted_count; ++index ) {
        if ( posted[index].target == target ) {
            ++count;
        }
    }
    return count;
}

uint16_t test_qp_deferred_count(void)
{
    return deferred_count;
}

void test_qp_clear(void)
{
    posted_count     = 0U;
    deferred_count   = 0U;
    subscribed_count = 0U;
}

bool test_qp_is_subscribed(enum_t sig)
{
    uint16_t index;

    for ( index = 0U; index < subscribed_count; ++index ) {
        if ( subscribed[index] == sig ) {
            return true;
        }
    }
    return false;
}

void QActive_subscribe(QActive const* const me, enum_t const sig)
{
    (void)me;

    if ( subscribed_count < TEST_DEFERRED_MAX ) {
        subscribed[subscribed_count] = sig;
        ++subscribed_count;
    }
}

bool QActive_post_(QActive* const me,
                   QEvt const* const e,
                   uint_fast16_t const margin,
                   void const* const sender)
{
    (void)margin;
    (void)sender;

    if ( posted_count >= TEST_POSTED_MAX ) {
        return false;
    }
    posted[posted_count].target = me;
    posted[posted_count].event  = e;
    ++posted_count;
    return true;
}

bool QActive_defer(QActive const* const me,
                   struct QEQueue* const eq,
                   QEvt const* const e)
{
    (void)me;
    (void)eq;

    if ( deferred_count >= TEST_DEFERRED_MAX ) {
        return false;
    }
    deferred[deferred_count] = e;
    ++deferred_count;
    return true;
}

bool QActive_recall(QActive* const me, struct QEQueue* const eq)
{
    (void)eq;

    if ( deferred_count == 0U ) {
        return false;
    }
    --deferred_count;
    (void)QActive_post_(me, deferred[deferred_count], QF_NO_MARGIN, NULL);
    return true;
}

void QEQueue_init(QEQueue* const me,
                  QEvtPtr* const sto,
                  uint_fast16_t const size)
{
    (void)me;
    (void)sto;
    (void)size;
}

void QActive_ctor(QActive* const me, QStateHandler const initial)
{
    QHsm_ctor((QHsm*)me, initial);
}

void QF_gc(QEvt const* const e)
{
    (void)e;
}

QEvt* QF_newX_(uint_fast16_t const evtSize,
               uint_fast16_t const margin,
               enum_t const sig)
{
    static cal_save_evt_t event;

    (void)margin;
    if ( evtSize > sizeof(event) ) {
        return NULL;
    }
    memset(&event, 0, sizeof(event));
    event.super.sig = (QSignal)sig;
    return &event.super;
}

void QTimeEvt_ctorX(QTimeEvt* const me,
                    QActive* const act,
                    enum_t const sig,
                    uint_fast8_t const tickRate)
{
    me->super.sig = (QSignal)sig;
    me->act       = act;
    me->ctr       = 0U;
    me->interval  = 0U;
    me->tickRate  = tickRate;
}

void QTimeEvt_armX(QTimeEvt* const me,
                   uint32_t const nTicks,
                   uint32_t const interval)
{
#ifndef Q_UNSAFE
    if ( me->ctr != 0U ) {
        fprintf(stderr, "QTimeEvt_armX while armed: sig=%d ctr=%u\n",
                (int)me->super.sig, (unsigned)me->ctr);
        abort();
    }
#endif
    me->ctr      = (QTimeEvtCtr)nTicks;
    me->interval = (QTimeEvtCtr)interval;
}

bool QTimeEvt_disarm(QTimeEvt* const me)
{
    bool const was_armed = (me->ctr != 0U) ? true : false;

    me->ctr = 0U;
    return was_armed;
}

bool QTimeEvt_rearm(QTimeEvt* const me, uint32_t const nTicks)
{
    bool const was_armed = (me->ctr != 0U) ? true : false;

    me->ctr = (QTimeEvtCtr)nTicks;
    return was_armed;
}

QTimeEvtCtr QTimeEvt_getCtr(QTimeEvt const* const me)
{
    return me->ctr;
}

void elog_warn(char const* tag, char const* format, ...)
{
    (void)tag;
    (void)format;
}

void elog_error(char const* tag, char const* format, ...)
{
    (void)tag;
    (void)format;
}

/* Safe host builds compile qep_hsm.c without Q_UNSAFE, so the QP FuSa
 * assertions need these port hooks. Unsafe builds simply never call them. */
Q_NORETURN Q_onError(char const* module, int_t id)
{
    fprintf(stderr, "Q_onError: %s (%d)\n",
            (module != NULL) ? module : "?", (int)id);
    abort();
}

void QF_crit_entry_(void)
{
}

void QF_crit_exit_(void)
{
}

void QF_int_disable_(void)
{
}

void QF_int_enable_(void)
{
}
