/**
 * @file test_modbus_image.c
 * @brief Host tests of the typed mirror and the write transaction.
 *
 * Drives the data-space callbacks directly with the image as their ctx
 * - no slave core, no QP - so the whole transaction discipline runs
 * exactly as the core would invoke it: validate, commit (through the real
 * param module over an in-memory KV store), then mirror, then notify.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "modbus_image.h"

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

/*----------------------------------------------------------------------------*/
/* Notifier capture                                                          */
/*----------------------------------------------------------------------------*/

static uint8_t notified_ch;
static uint8_t notified_count;
static ch_config_t notified_cfg;

/** @brief Backing store of the real param module. */
static kv_store_mem_t kv;

static void notifier(void* ctx,
                     uint8_t ch,
                     const ch_config_t* cfg)
{
    (void)ctx;
    notified_ch  = ch;
    notified_cfg = *cfg;
    ++notified_count;
}

/** @brief The callback table of a fresh image, as the core would get it. */
static const modbus_slave_callbacks_t* wiring(modbus_image_t* img)
{
    const modbus_slave_callbacks_t* cb;
    param_boot_result_t boot;

    kv_store_mem_reset(&kv);
    param_bind_store(kv_store_mem_ops(), &kv);
    param_boot_load(&boot);
    CHECK(boot.db_ok != 0U);
    notified_count = 0U;
    modbus_image_init(img);
    modbus_image_set_cfg_notifier(img, &notifier, NULL);
    cb = modbus_image_callbacks(img);
    CHECK(cb->ctx == img);
    CHECK(cb->holding_registers != NULL);
    CHECK(cb->input_registers != NULL);
    CHECK(cb->coils != NULL);
    CHECK(cb->discrete_inputs != NULL);
    CHECK(cb->address != NULL);
    return cb;
}

/** @brief Write @p count registers starting at @p start into a block. */
static modbus_slave_exception_t write_regs(const modbus_slave_callbacks_t* cb,
                                           uint16_t start,
                                           uint16_t count,
                                           const uint8_t* bytes)
{
    return cb->holding_registers(cb->ctx, start, count,
                                 (uint8_t*)bytes, MODBUS_SLAVE_REG_WRITE);
}

/** @brief Read @p count registers starting at @p start. */
static modbus_slave_exception_t read_regs(const modbus_slave_callbacks_t* cb,
                                          uint16_t start,
                                          uint16_t count,
                                          uint8_t* bytes)
{
    return cb->holding_registers(cb->ctx, start, count,
                                 bytes, MODBUS_SLAVE_REG_READ);
}

/** @brief Encode a type K configuration with the given rate code. */
static void tc_block(uint16_t rate_code,
                     uint8_t* block)
{
    ch_config_t cfg;

    cfg.type       = (uint8_t)CH_TYPE_TC;
    cfg.tc_type    = (uint8_t)CH_TC_TYPE_K;
    cfg.rate_code  = rate_code;
    cfg.linear_min = 0.0f;
    cfg.linear_max = 0.0f;
    cfg.offset     = 0.0f;
    cfg.gain       = 1.0f;
    reg_map_ch_encode(&cfg, block);
}

/** @brief Decode the body of the last KV write as a channel configuration. */
static ch_config_t last_cfg(void)
{
    ch_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    if ( kv.last_len <= sizeof(cfg) ) {
        memcpy(&cfg, kv.last_value, kv.last_len);
    }
    return cfg;
}

/*----------------------------------------------------------------------------*/

static void test_init_defaults(void)
{
    modbus_image_t img;
    const modbus_slave_callbacks_t* cb = wiring(&img);
    uint8_t ch;

    CHECK(img.addr_image == REG_MAP_DEV_ADDR_MIN);
    CHECK(img.cjc_valid == 0U);
    CHECK(img.b1 == 0U);
    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        CHECK(img.status[ch] == (uint16_t)REG_MAP_STATUS_UNCONFIGURED);
        CHECK(img.load_failed[ch] == 0U);
        CHECK(img.applied[ch] == 0U);
    }

    /* The address callback answers from the mirror. */
    CHECK(cb->address(cb->ctx) == (uint8_t)REG_MAP_DEV_ADDR_MIN);

    /* Unmapped spaces stay honest. */
    CHECK(cb->coils(cb->ctx, 0U, 1U, NULL, MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_ILLEGAL_FUNCTION);
    CHECK(cb->discrete_inputs(cb->ctx, 0U, 1U, NULL, MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_ILLEGAL_FUNCTION);
    {
        uint8_t block[REG_MAP_CH_CFG_LEN * 2U];

        CHECK(read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
              == MODBUS_SLAVE_EX_NONE);
    }
}

static void test_boot_adopt(void)
{
    modbus_image_t img;
    param_boot_result_t res;

    wiring(&img);
    memset(&res, 0, sizeof(res));
    res.cfg_valid[0]       = 1U;
    res.cfg[0].type        = (uint8_t)CH_TYPE_4_20MA;
    res.cfg[0].rate_code   = 9U;
    res.cfg_load_failed[2] = 1U;
    res.db_ok              = 1U;
    res.dev_addr           = 17U;

    modbus_image_boot(&img, &res);
    CHECK(img.addr_image == 17U);
    CHECK(img.load_failed[2] != 0U);
    CHECK(img.cfg[0].type == (uint8_t)CH_TYPE_4_20MA);
    CHECK(img.cfg[2].type == 0U); /* never adopted, never defaulted */

    modbus_image_init(&img);
    CHECK(img.addr_image == REG_MAP_DEV_ADDR_MIN);
    CHECK(img.status[0] == (uint16_t)REG_MAP_STATUS_UNCONFIGURED);
}

/**
 * @brief A full-block configuration write: EX 00, one commit, mirror and
 *        notifier agree, and the next read returns what was written.
 */
static void test_write_transaction_commit(void)
{
    modbus_image_t img;
    const modbus_slave_callbacks_t* cb = wiring(&img);
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t echo[REG_MAP_CH_CFG_LEN * 2U];
    ch_config_t next;

    next.type       = (uint8_t)CH_TYPE_4_20MA;
    next.tc_type    = CH_TC_TYPE_NONE;
    next.rate_code  = 9U;
    next.linear_min = 4.0f;
    next.linear_max = 20.0f;
    next.offset     = 0.5f;
    next.gain       = 2.0f;
    reg_map_ch_encode(&next, block);

    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
          == MODBUS_SLAVE_EX_NONE);
    CHECK(kv.set_calls == 1U);
    CHECK(strcmp(kv.last_key, "cfg/ch0") == 0);
    {
        ch_config_t saved = last_cfg();

        CHECK(saved.rate_code == 9U);
        CHECK(saved.linear_max == 20.0f);
    }
    CHECK(img.cfg[0].type == (uint8_t)CH_TYPE_4_20MA);
    CHECK(img.applied[0] == 0U); /* persisted, not yet applied */
    CHECK(img.b1 == 0U);
    CHECK(notified_count == 1U);
    CHECK(notified_ch == 0U);
    CHECK(notified_cfg.gain == 2.0f);

    CHECK(read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, echo)
          == MODBUS_SLAVE_EX_NONE);
    CHECK(memcmp(echo, block, sizeof(block)) == 0);

    /* Notifier context and channel are per-instance state. */
    CHECK(img.cfg_notifier == &notifier);
}

/** @brief An invalid configuration value rejects the whole frame. */
static void test_write_transaction_reject(void)
{
    modbus_image_t img;
    const modbus_slave_callbacks_t* cb = wiring(&img);
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t before[REG_MAP_CH_CFG_LEN * 2U];

    tc_block(9U, block);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
          == MODBUS_SLAVE_EX_NONE);
    CHECK(notified_count == 1U);

    read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, before);
    block[1] = 0x63U; /* type code 99: no such input type. */
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);
    CHECK(kv.set_calls == 1U); /* only the valid frame above */
    read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block);
    CHECK(memcmp(block, before, sizeof(block)) == 0);
    CHECK(notified_count == 1U);
    CHECK(img.b1 == 0U);
}

/** @brief A failing commit answers EX 04, latches b1, mirrors nothing. */
static void test_write_transaction_commit_failure(void)
{
    modbus_image_t img;
    const modbus_slave_callbacks_t* cb = wiring(&img);
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t next_block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t before[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t reply[2U];
    ch_config_t next;

    /* First write is a change from the loaded default, so it commits. */
    tc_block(9U, block);
    CHECK(write_regs(cb, 16U, REG_MAP_CH_CFG_LEN, block)
          == MODBUS_SLAVE_EX_NONE); /* channel 2 (zero-based 1) */
    CHECK(notified_count == 1U);

    read_regs(cb, 16U, REG_MAP_CH_CFG_LEN, before);

    /* A second, different value attempts a write whose read-back is wrong. */
    next      = last_cfg();
    next.gain = 3.0f;
    reg_map_ch_encode(&next, next_block);
    kv.corrupt_read = 1U;
    CHECK(write_regs(cb, 16U, REG_MAP_CH_CFG_LEN, next_block)
          == MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE);
    kv.corrupt_read = 0U;
    CHECK(img.b1 != 0U);
    read_regs(cb, 16U, REG_MAP_CH_CFG_LEN, block);
    CHECK(memcmp(block, before, sizeof(block)) == 0);
    CHECK(notified_count == 1U);

    /* The latched bit is visible through the fault word (b0 also set). */
    CHECK(cb->input_registers(cb->ctx, 68U, 1U, reply,
                              MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_NONE);
    CHECK(modbus_slave_get_u16(reply)
          == (uint16_t)(REG_MAP_FAULT_PARAM_WRITE | REG_MAP_FAULT_CJC));
}

/** @brief The same-value write commits nothing and notifies nobody. */
static void test_write_transaction_no_change(void)
{
    modbus_image_t img;
    const modbus_slave_callbacks_t* cb = wiring(&img);
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];

    /* A change from the loaded default commits once. */
    tc_block(9U, block);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
          == MODBUS_SLAVE_EX_NONE);
    CHECK(notified_count == 1U);
    CHECK(kv.set_calls == 1U);

    /* The channel accepted it: the same value is now a true no-op. */
    modbus_image_set_applied(&img, 0U, 1U);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
          == MODBUS_SLAVE_EX_NONE);
    CHECK(notified_count == 1U); /* unchanged: nobody notified */
    CHECK(kv.set_calls == 1U);
    CHECK(img.b1 == 0U);
}

/** @brief A failed apply keeps the same-value write as a retry trigger. */
static void test_write_retry_until_applied(void)
{
    modbus_image_t img;
    const modbus_slave_callbacks_t* cb = wiring(&img);
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];

    tc_block(9U, block);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
          == MODBUS_SLAVE_EX_NONE);
    CHECK(notified_count == 1U);
    CHECK(img.applied[0] == 0U);

    /* Same value while not applied: re-notify so the channel retries. */
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
          == MODBUS_SLAVE_EX_NONE);
    CHECK(notified_count == 2U);
    CHECK(kv.set_calls == 1U); /* persisted once */

    /* Once applied, the same value is silent again. */
    modbus_image_set_applied(&img, 0U, 1U);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
          == MODBUS_SLAVE_EX_NONE);
    CHECK(notified_count == 2U);
    CHECK(kv.set_calls == 1U);
}

/** @brief Single-register writes land through the read-modify-write. */
static void test_write_single_register(void)
{
    modbus_image_t img;
    const modbus_slave_callbacks_t* cb = wiring(&img);
    uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t one[2U];
    uint8_t echo[REG_MAP_CH_CFG_LEN * 2U];

    /* Start from 50 Hz (a change from the loaded 10 Hz default). */
    tc_block(9U, block);
    CHECK(write_regs(cb, 0U, REG_MAP_CH_CFG_LEN, block)
          == MODBUS_SLAVE_EX_NONE);

    /* Register 1 is the rate code: 10 Hz (code 2). */
    modbus_slave_put_u16(one, 2U);
    CHECK(write_regs(cb, 1U, 1U, one) == MODBUS_SLAVE_EX_NONE);
    CHECK(img.cfg[0].rate_code == 2U);
    CHECK(img.cfg[0].type == (uint8_t)CH_TYPE_TC);

    CHECK(read_regs(cb, 0U, REG_MAP_CH_CFG_LEN, echo)
          == MODBUS_SLAVE_EX_NONE);
    CHECK(modbus_slave_get_u16(&echo[2]) == 2U);
    CHECK(notified_count == 2U);
}

/** @brief Device register writes move the mirror's address image. */
static void test_address_register(void)
{
    modbus_image_t img;
    const modbus_slave_callbacks_t* cb = wiring(&img);
    uint8_t one[2U];
    uint8_t echo[2U];

    modbus_slave_put_u16(one, 247U);
    CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one)
          == MODBUS_SLAVE_EX_NONE);
    CHECK(img.addr_image == 247U);
    CHECK(cb->address(cb->ctx) == 247U);
    CHECK(kv.set_calls == 1U);
    CHECK(strcmp(kv.last_key, "dev/addr") == 0);
    CHECK(notified_count == 0U); /* address writes never notify */

    modbus_slave_put_u16(one, 0U);
    CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);
    modbus_slave_put_u16(one, 248U);
    CHECK(write_regs(cb, REG_MAP_DEV_BASE, 1U, one)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);

    CHECK(read_regs(cb, REG_MAP_DEV_BASE, 1U, echo)
          == MODBUS_SLAVE_EX_NONE);
    CHECK(modbus_slave_get_u16(echo) == 247U);
}

/** @brief Telemetry mirror encodes through the input callback. */
static void test_telemetry(void)
{
    modbus_image_t img;
    const modbus_slave_callbacks_t* cb = wiring(&img);
    uint8_t reply[8U];
    uint32_t bits;
    float value;

    modbus_image_set_measure(&img, 1U, 42.5f,
                             (uint16_t)REG_MAP_STATUS_OK);
    modbus_image_set_cjc(&img, 25.0f, 1U);
    CHECK(img.status[1] == (uint16_t)REG_MAP_STATUS_OK);
    CHECK(img.cjc_valid == 1U);

    /* Channel 2 telemetry block: registers 8..10. */
    CHECK(cb->input_registers(cb->ctx, 8U, 3U, reply,
                              MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_NONE);
    bits = ((uint32_t)modbus_slave_get_u16(&reply[0]) << 16)
           | (uint32_t)modbus_slave_get_u16(&reply[2]);
    memcpy(&value, &bits, sizeof(value));
    CHECK(value == 42.5f);
    CHECK(modbus_slave_get_u16(&reply[4]) == (uint16_t)REG_MAP_STATUS_OK);

    /* Input registers are read-only. */
    CHECK(cb->input_registers(cb->ctx, 8U, 1U, reply,
                              MODBUS_SLAVE_REG_WRITE)
          == MODBUS_SLAVE_EX_ILLEGAL_FUNCTION);
}

/** @brief Out-of-range and cross-block addresses answer EX 02. */
static void test_address_bounds(void)
{
    modbus_image_t img;
    const modbus_slave_callbacks_t* cb = wiring(&img);

    CHECK(write_regs(cb, 10U, 1U, NULL)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
    CHECK(write_regs(cb, 0U, 11U, NULL)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
    CHECK(cb->input_registers(cb->ctx, 72U, 1U, NULL,
                              MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
}

/*----------------------------------------------------------------------------*/

int main(void)
{
    test_init_defaults();
    test_boot_adopt();
    test_write_transaction_commit();
    test_write_transaction_reject();
    test_write_transaction_commit_failure();
    test_write_transaction_no_change();
    test_write_retry_until_applied();
    test_write_single_register();
    test_address_register();
    test_telemetry();
    test_address_bounds();

    printf("test_modbus_image: %d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
