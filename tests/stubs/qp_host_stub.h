#ifndef QP_HOST_STUB_H
#define QP_HOST_STUB_H

#include "qpc.h"

/**
 * @brief Pop the oldest event posted to one active object.
 *
 * @param target Active object whose queue is inspected.
 * @return The event, or NULL when the queue is empty.
 */
QEvt const* test_qp_pop(QActive* target);

/** @brief Drop every queued event and every deferred event. */
void test_qp_clear(void);

/**
 * @brief Count events queued for one active object.
 *
 * @param target Active object whose queue is inspected.
 * @return Number of queued events.
 */
uint16_t test_qp_count(QActive* target);

/** @brief Number of events currently in the test defer queue. */
uint16_t test_qp_deferred_count(void);

/** @brief Test whether one signal was subscribed during the test. */
bool test_qp_is_subscribed(enum_t sig);

#endif
