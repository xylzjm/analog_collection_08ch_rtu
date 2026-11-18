/**
 * @file test_reconfig_race.c
 * @brief Host model test for the reconfiguration race (channel_ao.md section 6).
 *
 * The model enumerates every interleaving of timer expiry around the
 * CONFIG_SET_SIG handling phases and asserts that a timer event of a
 * cancelled generation never reaches a state that handles SETTLE_SIG.
 * The pre-fix "post before disarm" ordering runs as a negative control and
 * must produce violations, proving the test has teeth.
 */

#include <stdio.h>

static int checks;
static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        ++checks;                                                       \
        if ( !(cond) ) {                                                \
            ++failures;                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                               \
    } while (0)

enum { SETTLE_SIG_M = 1, APPLY_SIG_M = 2 };

typedef enum {
    ST_UNCONFIG = 0,
    ST_DETECTING,
    ST_SETTLING,
    ST_MEASURING
} state_t;

typedef struct {
    int      sig;
    unsigned gen;
} evt_t;

static state_t st;
static unsigned armed_gen;
static unsigned current_gen;
static evt_t queue_[32];
static unsigned q_head;
static unsigned q_tail;
static int stale_dispatch;

static void reset(state_t start)
{
    st             = start;
    current_gen    = 1U;
    armed_gen      = (start == ST_DETECTING || start == ST_SETTLING) ? 1U : 0U;
    q_head         = 0U;
    q_tail         = 0U;
    stale_dispatch = 0;
}

static void post(int sig, unsigned gen)
{
    queue_[q_tail].sig = sig;
    queue_[q_tail].gen = gen;
    ++q_tail;
}

static void tick_expire(void)
{
    if ( armed_gen != 0U ) {
        post(SETTLE_SIG_M, armed_gen);
    }
}

static void dispatch(void)
{
    evt_t e = queue_[q_head];
    ++q_head;

    if ( e.sig == APPLY_SIG_M ) {
        if ( st == ST_UNCONFIG ) {
            /* The re-entry step: configure and arm the next generation. */
            ++current_gen;
            armed_gen = current_gen;
            st        = ST_DETECTING;
        }
        /* APPLY reaching a working leaf is ignored (redundant post). */
        return;
    }

    /* SETTLE_SIG: handled in Detecting and Settling only. */
    if ( st == ST_DETECTING || st == ST_SETTLING ) {
        if ( e.gen == armed_gen ) {
            /* Genuine deadline: advance the pipeline. */
            if ( st == ST_DETECTING ) {
                ++current_gen;
                armed_gen = current_gen;
                st        = ST_SETTLING;
            }
            else {
                armed_gen = 0U;
                st        = ST_MEASURING;
            }
        }
        else {
            ++stale_dispatch; /* the violation this test hunts */
        }
    }
    /* Landing in Unconfigured or Measuring: ignored by design. */
}

static void drain(void)
{
    while ( q_head < q_tail ) {
        dispatch();
    }
}

/* One CONFIG_SET_SIG handling, with timer expiries interleaved at each gap. */
static int run_cfg(int b1_order, unsigned t0, unsigned t1, unsigned t2, unsigned t3, unsigned t4)
{
    unsigned i;

    for ( i = 0U; i < t0; ++i ) { tick_expire(); } /* CONFIG queued .. dispatched */
    for ( i = 0U; i < t1; ++i ) { tick_expire(); } /* begin .. first action */
    if ( b1_order != 0 ) {
        /* Fixed ordering (channel_ao.md section 6): disarm first. */
        armed_gen = 0U;
        for ( i = 0U; i < t2; ++i ) { tick_expire(); } /* disarm .. post */
        post(APPLY_SIG_M, 0U);
    }
    else {
        /* Pre-fix ordering: post first (the bug this test guards against). */
        post(APPLY_SIG_M, 0U);
        for ( i = 0U; i < t2; ++i ) { tick_expire(); } /* post .. disarm */
        armed_gen = 0U;
    }
    for ( i = 0U; i < t3; ++i ) { tick_expire(); } /* last action .. done */
    st = ST_UNCONFIG;
    for ( i = 0U; i < t4; ++i ) { tick_expire(); } /* after the transition */
    drain();
    return stale_dispatch;
}

/* Exhaustive: 3^5 expiries x 4 start states x 2 orderings. */
static int sweep(int b1_order, state_t start)
{
    unsigned a;
    unsigned b;
    unsigned c;
    unsigned d;
    unsigned e;
    int bad = 0;

    for ( a = 0U; a <= 2U; ++a ) {
        for ( b = 0U; b <= 2U; ++b ) {
            for ( c = 0U; c <= 2U; ++c ) {
                for ( d = 0U; d <= 2U; ++d ) {
                    for ( e = 0U; e <= 2U; ++e ) {
                        reset(start);
                        if ( run_cfg(b1_order, a, b, c, d, e) != 0 ) {
                            ++bad;
                        }
                    }
                }
            }
        }
    }
    return bad;
}

int main(void)
{
    /* Fixed ordering: no interleaving may leak a cancelled generation. */
    CHECK(sweep(1, ST_DETECTING) == 0);
    CHECK(sweep(1, ST_SETTLING) == 0);
    CHECK(sweep(1, ST_MEASURING) == 0);
    CHECK(sweep(1, ST_UNCONFIG) == 0);

    /* Negative control: the pre-fix ordering must produce violations. */
    CHECK(sweep(0, ST_DETECTING) > 0);

    /* Two consecutive reconfigurations: the late event of generation 1 must
     * land in Unconfigured, and the second APPLY must be ignored. */
    reset(ST_DETECTING);
    tick_expire();           /* generation 1 expiry queued */
    armed_gen = 0U;          /* CONFIG1 disarm (fixed ordering) */
    post(APPLY_SIG_M, 0U);   /* CONFIG1 post */
    st = ST_UNCONFIG;        /* CONFIG1 done */
    post(APPLY_SIG_M, 0U);   /* CONFIG2 post (internal, pending merged) */
    drain();
    CHECK(stale_dispatch == 0);
    CHECK(st == ST_DETECTING); /* exactly one re-entry */

    /* A deadline already queued before the cancel is dispatched first and
     * must be consumed as genuine by the old state. */
    reset(ST_DETECTING);
    post(SETTLE_SIG_M, 1U);
    dispatch();
    CHECK(stale_dispatch == 0);
    CHECK(st == ST_SETTLING);
    post(APPLY_SIG_M, 0U);
    armed_gen = 0U;
    st        = ST_UNCONFIG;
    drain();
    CHECK(stale_dispatch == 0);

    printf("%d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
