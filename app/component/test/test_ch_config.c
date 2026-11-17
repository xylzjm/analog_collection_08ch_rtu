/* SPDX-License-Identifier: MIT */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ch_config.h"

static int checks;
static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if ( !(cond) ) {                                                     \
            failures++;                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while ( 0 )

static ch_config_t make_cfg(uint8_t type,
                            uint8_t tc_type,
                            uint16_t rate_code,
                            float linear_min,
                            float linear_max,
                            float offset,
                            float gain)
{
    ch_config_t cfg;

    cfg.type       = type;
    cfg.tc_type    = tc_type;
    cfg.rate_code  = rate_code;
    cfg.linear_min = linear_min;
    cfg.linear_max = linear_max;
    cfg.offset     = offset;
    cfg.gain       = gain;
    return cfg;
}

static void test_layout(void)
{
    CHECK(sizeof(ch_config_t) == 20U);
    CHECK(CH_CONFIG_CHANNEL_COUNT == ANALOG_INPUT_BOARD_CHANNEL_COUNT);
    CHECK(CH_CONFIG_RATE_CODE_COUNT == 13U);
}

static void test_rate_tables(void)
{
    static const uint32_t expect_mhz[13] = {
        2500U, 5000U, 10000U, 12500U, 15000U, 20000U, 25000U,
        30000U, 40000U, 50000U, 60000U, 80000U, 100000U
    };
    static const uint32_t expect_ticks[13] = {
        400U, 200U, 100U, 80U, 67U, 50U, 40U, 34U, 25U, 20U, 17U, 13U, 10U
    };
    uint16_t code;

    for ( code = 0U; code < 13U; ++code ) {
        uint32_t mhz   = ch_config_rate_millihz(code);
        uint32_t ticks = ch_config_rate_ticks(code);

        CHECK(mhz == expect_mhz[code]);
        CHECK(ticks == expect_ticks[code]);

        /* Ticks are the rounded-up period: no over-sampling, no jitter. */
        CHECK(ticks == ((1000000U + mhz - 1U) / mhz));

        /*
         * Realized rate is 1e6/ticks mHz; the product mhz * ticks is 1e6 on
         * the exact codes and grows with the rounding loss. It must stay at
         * or above 1e6 (rate never exceeds nominal) and at most 4% above it.
         */
        CHECK((mhz * ticks) >= 1000000U);
        CHECK((mhz * ticks) <= 1041666U);
    }

    CHECK(ch_config_rate_millihz(13U) == 0U);
    CHECK(ch_config_rate_ticks(13U) == 0U);
    CHECK(ch_config_rate_millihz(0xFFFFU) == 0U);
    CHECK(ch_config_rate_ticks(0xFFFFU) == 0U);
}

static void test_validate_accept_and_normalize(void)
{
    ch_config_t cfg;
    ch_config_t again;

    /* Factory shape: thermocouple K, 10 Hz, unit calibration. */
    cfg = make_cfg((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U,
                   0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(ch_config_validate(&cfg));
    CHECK(cfg.type == (uint8_t)CH_TYPE_TC);
    CHECK(cfg.tc_type == (uint8_t)CH_TC_TYPE_K);
    CHECK(cfg.rate_code == 2U);
    CHECK(cfg.gain == 1.0f);

    /* Non-thermocouple types normalize tc_type away. PT100 is not a linear
     * type, so the linear limits normalize away as well. */
    cfg = make_cfg((uint8_t)CH_TYPE_PT100, 5U, 0U,
                   -200.0f, 850.0f, 0.5f, 1.0f);
    CHECK(ch_config_validate(&cfg));
    CHECK(cfg.tc_type == CH_TC_TYPE_NONE);
    CHECK(cfg.linear_min == 0.0f);
    CHECK(cfg.linear_max == 0.0f);
    CHECK(cfg.offset == 0.5f);

    /* Linear types keep both limits and normalize tc_type away. */
    cfg = make_cfg((uint8_t)CH_TYPE_4_20MA, 3U, 7U,
                   4.0f, 20.0f, 0.0f, 1.0f);
    CHECK(ch_config_validate(&cfg));
    CHECK(cfg.tc_type == CH_TC_TYPE_NONE);
    CHECK(cfg.linear_min == 4.0f);
    CHECK(cfg.linear_max == 20.0f);

    /* Non-linear types normalize the linear limits away. */
    cfg = make_cfg((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_T, 12U,
                   -1.0f, 9.0f, -0.25f, 0.99f);
    CHECK(ch_config_validate(&cfg));
    CHECK(cfg.linear_min == 0.0f);
    CHECK(cfg.linear_max == 0.0f);
    CHECK(cfg.offset == -0.25f);
    CHECK(cfg.gain == 0.99f);
    CHECK(cfg.rate_code == 12U);

    /* Normalization is idempotent (memcmp change check stays stable). */
    cfg = make_cfg((uint8_t)CH_TYPE_4_20MA, 3U, 7U,
                   4.0f, 20.0f, 0.0f, 1.0f);
    CHECK(ch_config_validate(&cfg));
    again = cfg;
    CHECK(ch_config_validate(&again));
    CHECK(memcmp(&cfg, &again, sizeof(cfg)) == 0);
}

static void test_validate_reject(void)
{
    ch_config_t cfg;

    cfg = make_cfg(4U, 0U, 2U, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(!ch_config_validate(&cfg));

    /* Sentinel produced by reg_map_ch_decode() for unknown type codes. */
    cfg = make_cfg(0xFFU, 0xFFU, 2U, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(!ch_config_validate(&cfg));

    cfg = make_cfg((uint8_t)CH_TYPE_TC, 8U, 2U, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(!ch_config_validate(&cfg));

    cfg = make_cfg((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 13U,
                   0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(!ch_config_validate(&cfg));

    cfg = make_cfg((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 0xFFFFU,
                   0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(!ch_config_validate(&cfg));

    cfg = make_cfg((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U,
                   NAN, 0.0f, 0.0f, 1.0f);
    CHECK(!ch_config_validate(&cfg));

    cfg = make_cfg((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U,
                   0.0f, INFINITY, 0.0f, 1.0f);
    CHECK(!ch_config_validate(&cfg));

    cfg = make_cfg((uint8_t)CH_TYPE_0_10V, 0xFFU, 2U,
                   0.0f, 10.0f, -INFINITY, 1.0f);
    CHECK(!ch_config_validate(&cfg));

    cfg = make_cfg((uint8_t)CH_TYPE_0_10V, 0xFFU, 2U,
                   0.0f, 10.0f, 0.0f, NAN);
    CHECK(!ch_config_validate(&cfg));

    CHECK(!ch_config_validate(NULL));

    /* A rejected configuration must be left untouched. */
    cfg = make_cfg(9U, 3U, 2U, 5.0f, 6.0f, 7.0f, 8.0f);
    CHECK(!ch_config_validate(&cfg));
    CHECK(cfg.type == 9U);
    CHECK(cfg.tc_type == 3U);
    CHECK(cfg.linear_min == 5.0f);
    CHECK(cfg.gain == 8.0f);
}

int main(void)
{
    test_layout();
    test_rate_tables();
    test_validate_accept_and_normalize();
    test_validate_reject();

    printf("%d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
