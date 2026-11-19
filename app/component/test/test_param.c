/**
 * @file test_param.c
 * @brief Host tests of the param module over an in-memory KV store.
 *
 * Covers the EX 00 read-back invariant, the no-change short-circuit, the
 * boot load failure paths and the init failure path. The real param module
 * runs; only the storage backend is replaced at the kv_store seam.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kv_store_mem.h"
#include "param.h"
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

/** @brief Backing store of the real param module. */
static kv_store_mem_t kv;

/** @brief Reset the store and bind it to param. */
static void bind_store(void)
{
    kv_store_mem_reset(&kv);
    param_bind_store(kv_store_mem_ops(), &kv);
}

/** @brief Build one normalized configuration for a save test. */
static ch_config_t make_cfg(uint8_t type,
                            uint16_t rate_code)
{
    ch_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.type       = type;
    cfg.tc_type    = (type == (uint8_t)CH_TYPE_TC) ? (uint8_t)CH_TC_TYPE_K
                                                   : CH_TC_TYPE_NONE;
    cfg.rate_code  = rate_code;
    cfg.linear_min = 4.0f;
    cfg.linear_max = 20.0f;
    cfg.offset     = 0.5f;
    cfg.gain       = 2.0f;
    return cfg;
}

/** @brief An empty store materializes the factory defaults. */
static void test_boot_defaults(void)
{
    param_boot_result_t res;
    uint8_t ch;

    bind_store();
    param_boot_load(&res);

    CHECK(res.db_ok == 1U);
    CHECK(kv.init_calls == 1U);
    CHECK(kv.set_calls == 0U); /* materialization is not a commit */
    CHECK(res.dev_addr == REG_MAP_DEV_ADDR_MIN);
    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        CHECK(res.cfg_valid[ch] != 0U);
        CHECK(res.cfg_load_failed[ch] == 0U);
        CHECK(res.cfg[ch].type == (uint8_t)CH_TYPE_TC);
        CHECK(res.cfg[ch].rate_code == 2U);
    }
}

/** @brief A changed value commits; the same value short-circuits. */
static void test_save_and_no_change(void)
{
    param_boot_result_t res;
    ch_config_t cfg = make_cfg((uint8_t)CH_TYPE_4_20MA, 9U);
    ch_config_t saved;

    bind_store();
    param_boot_load(&res);
    CHECK(res.db_ok == 1U);

    CHECK(param_save_cfg(0U, &cfg) == PARAM_OK);
    CHECK(kv.set_calls == 1U);
    CHECK(strcmp(kv.last_key, "cfg/ch0") == 0);
    CHECK(kv.last_len == sizeof(cfg));
    memcpy(&saved, kv.last_value, sizeof(saved));
    CHECK(memcmp(&saved, &cfg, sizeof(cfg)) == 0);

    CHECK(param_save_cfg(0U, &cfg) == PARAM_NO_CHANGE);
    CHECK(kv.set_calls == 1U); /* no second write */
}

/** @brief A wrong read-back is not committed, and a retry can succeed. */
static void test_readback_mismatch(void)
{
    param_boot_result_t res;
    ch_config_t cfg = make_cfg((uint8_t)CH_TYPE_4_20MA, 8U);

    bind_store();
    param_boot_load(&res);

    kv.corrupt_read = 1U;
    CHECK(param_save_cfg(1U, &cfg) == PARAM_ERR_WRITE);
    kv.corrupt_read = 0U;

    /* The snapshot was not updated, so the retry still writes. */
    CHECK(param_save_cfg(1U, &cfg) == PARAM_OK);
    CHECK(kv.set_calls == 2U);
}

/** @brief A failed write is reported and can be retried. */
static void test_write_failure(void)
{
    param_boot_result_t res;
    ch_config_t cfg = make_cfg((uint8_t)CH_TYPE_0_10V, 10U);

    bind_store();
    param_boot_load(&res);

    kv.fail_write = 1U;
    CHECK(param_save_cfg(2U, &cfg) == PARAM_ERR_WRITE);
    kv.fail_write = 0U;

    CHECK(param_save_cfg(2U, &cfg) == PARAM_OK);
    CHECK(kv.set_calls == 2U);
}

/** @brief A wrong-length key fails only that channel. */
static void test_boot_load_failure(void)
{
    param_boot_result_t res;
    uint8_t bad[4] = {0};
    uint8_t ch;

    bind_store();
    kv_store_mem_put(&kv, "cfg/ch3", bad, sizeof(bad));
    param_boot_load(&res);

    CHECK(res.db_ok == 1U);
    CHECK(res.cfg_load_failed[3] != 0U);
    CHECK(res.cfg_valid[3] == 0U);
    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        if ( ch != 3U ) {
            CHECK(res.cfg_valid[ch] != 0U);
        }
    }
    CHECK(res.dev_addr == REG_MAP_DEV_ADDR_MIN);
}

/** @brief A bad address falls back to 1 and reports the failure. */
static void test_bad_address(void)
{
    param_boot_result_t res;
    uint16_t zero = 0U;

    bind_store();
    kv_store_mem_put(&kv, "dev/addr", &zero, sizeof(zero));
    param_boot_load(&res);

    CHECK(res.addr_load_failed != 0U);
    CHECK(res.dev_addr == REG_MAP_DEV_ADDR_MIN);
}

/** @brief An init failure reports db_ok = 0 and loads nothing. */
static void test_init_failure(void)
{
    param_boot_result_t res;
    uint8_t ch;

    bind_store();
    kv.fail_init = 1U;
    param_boot_load(&res);

    CHECK(res.db_ok == 0U);
    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        CHECK(res.cfg_valid[ch] == 0U);
    }
    CHECK(res.dev_addr == REG_MAP_DEV_ADDR_MIN);

    /* The next boot is healthy again. */
    kv.fail_init = 0U;
    param_boot_load(&res);
    CHECK(res.db_ok == 1U);
}

/** @brief set_default_all reloads the factory state into the snapshot. */
static void test_default_reset(void)
{
    param_boot_result_t res;
    ch_config_t cfg = make_cfg((uint8_t)CH_TYPE_4_20MA, 9U);

    bind_store();
    param_boot_load(&res);
    CHECK(param_save_cfg(0U, &cfg) == PARAM_OK);

    param_set_default_all();
    CHECK(kv.reset_calls == 1U);

    /* The snapshot now holds the default, so the same cfg is a change. */
    CHECK(param_save_cfg(0U, &cfg) == PARAM_OK);
    CHECK(param_save_cfg(0U, &cfg) == PARAM_NO_CHANGE);
}

/** @brief Address saves follow the same commit and retry discipline. */
static void test_address_save(void)
{
    param_boot_result_t res;

    bind_store();
    param_boot_load(&res);

    CHECK(param_save_addr(247U) == PARAM_OK);
    CHECK(param_save_addr(247U) == PARAM_NO_CHANGE);

    kv.corrupt_read = 1U;
    CHECK(param_save_addr(200U) == PARAM_ERR_WRITE);
    kv.corrupt_read = 0U;

    CHECK(param_save_addr(200U) == PARAM_OK);
    CHECK(param_save_addr(0U) == PARAM_ERR_WRITE);
}

/** @brief Invalid arguments and an unbound store are rejected. */
static void test_invalid_and_unbound(void)
{
    param_boot_result_t res;
    ch_config_t cfg = make_cfg((uint8_t)CH_TYPE_4_20MA, 9U);

    bind_store();
    param_boot_load(&res);

    CHECK(param_save_cfg(0U, NULL) == PARAM_ERR_WRITE);
    CHECK(param_save_cfg(CH_CONFIG_CHANNEL_COUNT, &cfg) == PARAM_ERR_WRITE);

    param_bind_store(NULL, NULL);
    param_boot_load(&res);
    CHECK(res.db_ok == 0U);
}

int main(void)
{
    test_boot_defaults();
    test_save_and_no_change();
    test_readback_mismatch();
    test_write_failure();
    test_boot_load_failure();
    test_bad_address();
    test_init_failure();
    test_default_reset();
    test_address_save();
    test_invalid_and_unbound();

    printf("test_param: %d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
