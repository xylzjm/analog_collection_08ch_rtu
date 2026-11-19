/* SPDX-License-Identifier: MIT */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "reg_map.h"

static int checks;
static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        checks++;                                                  \
        if ( !(cond) ) {                                           \
            failures++;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while ( 0 )

/* Model verdicts of reg_map_check(), independent of the built-in table. */
#define MODEL_EX_NONE 0
#define MODEL_EX_01   1
#define MODEL_EX_02   2

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

static float bits_to_float(uint32_t bits)
{
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t float_to_bits(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/*----------------------------------------------------------------------------*/
/* Round trip                                                                 */
/*----------------------------------------------------------------------------*/

static void test_roundtrip(void)
{
    static const ch_config_t cases[] = {
        {(uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U, 0.0f, 0.0f, 0.0f, 1.0f},
        {(uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_B, 12U, -3.0f, 9.0f, 0.125f, 0.99f},
        {(uint8_t)CH_TYPE_PT100, 5U, 0U, -200.0f, 850.0f, -0.5f, 1.005f},
        {(uint8_t)CH_TYPE_4_20MA, 0xFFU, 7U, 4.0f, 20.0f, 0.0f, 1.0f},
        {(uint8_t)CH_TYPE_0_10V, 2U, 3U, -10.0f, 10.0f, 1.5f, 2.5f}};
    size_t i;

    for ( i = 0U; i < (sizeof(cases) / sizeof(cases[0])); ++i ) {
        ch_config_t expected = cases[i];
        ch_config_t decoded;
        uint8_t buf[REG_MAP_CH_CFG_LEN * 2U];

        CHECK(ch_config_validate(&expected));

        /* decode(encode(x)) is equivalent to normalize(x). */
        reg_map_ch_encode(&cases[i], buf);
        reg_map_ch_decode(buf, &decoded);
        CHECK(ch_config_validate(&decoded));
        CHECK(memcmp(&expected, &decoded, sizeof(decoded)) == 0);

        /* A normalized input round trips byte for byte. */
        reg_map_ch_encode(&expected, buf);
        reg_map_ch_decode(buf, &decoded);
        CHECK(memcmp(&expected, &decoded, sizeof(decoded)) == 0);
    }
}

/*----------------------------------------------------------------------------*/
/* Sub-block read-modify-write                                                */
/*----------------------------------------------------------------------------*/

/**
 * @brief Overlay one register over an encoded block and decode the result.
 *
 * Mirrors the write pipeline: encode current, overlay frame, decode whole
 * block, validate with normalization. Returns the validation result.
 */
static bool merge_word(const ch_config_t* base,
                       uint16_t reg_off,
                       uint16_t word,
                       ch_config_t* out)
{
    uint8_t buf[REG_MAP_CH_CFG_LEN * 2U];

    reg_map_ch_encode(base, buf);
    modbus_slave_put_u16(&buf[2U * reg_off], word);
    reg_map_ch_decode(buf, out);
    return ch_config_validate(out);
}

/**
 * @brief Overlay one float (two registers) over an encoded block.
 */
static bool merge_float(const ch_config_t* base,
                        uint16_t reg_off,
                        float value,
                        ch_config_t* out)
{
    uint8_t buf[REG_MAP_CH_CFG_LEN * 2U];
    uint8_t tmp[4];
    uint32_t bits = float_to_bits(value);

    reg_map_ch_encode(base, buf);
    modbus_slave_put_u16(&tmp[0], (uint16_t)(bits >> 16));
    modbus_slave_put_u16(&tmp[2], (uint16_t)(bits & 0xFFFFU));
    memcpy(&buf[2U * reg_off], tmp, sizeof(tmp));
    reg_map_ch_decode(buf, out);
    return ch_config_validate(out);
}

static void test_subblock_merge(void)
{
    ch_config_t base = make_cfg((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K,
                                2U, 0.0f, 0.0f, 0.0f, 1.0f);
    ch_config_t got;
    ch_config_t want;

    /* Single register writes keep every untouched field. */
    CHECK(merge_word(&base, 1U, 7U, &got));
    want           = base;
    want.rate_code = 7U;
    CHECK(memcmp(&got, &want, sizeof(got)) == 0);

    /* A type write re-normalizes tc_type along with it. */
    CHECK(merge_word(&base, 0U, 8U, &got));
    want = make_cfg((uint8_t)CH_TYPE_PT100, CH_TC_TYPE_NONE, 2U,
                    0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(memcmp(&got, &want, sizeof(got)) == 0);

    CHECK(merge_float(&base, 6U, -1.5f, &got));
    want        = base;
    want.offset = -1.5f;
    CHECK(memcmp(&got, &want, sizeof(got)) == 0);

    CHECK(merge_float(&base, 8U, 2.5f, &got));
    want      = base;
    want.gain = 2.5f;
    CHECK(memcmp(&got, &want, sizeof(got)) == 0);

    /* Non-linear base: linear limit writes normalize back to 0.0. */
    CHECK(merge_float(&base, 2U, 5.0f, &got));
    CHECK(got.linear_min == 0.0f);
    CHECK(got.linear_max == 0.0f);

    /* An illegal overlay rejects the whole candidate (EX 03 path). */
    CHECK(!merge_word(&base, 1U, 13U, &got));

    base = make_cfg((uint8_t)CH_TYPE_4_20MA, CH_TC_TYPE_NONE, 2U,
                    4.0f, 20.0f, 0.0f, 1.0f);
    CHECK(merge_float(&base, 2U, 0.0f, &got));
    want            = base;
    want.linear_min = 0.0f;
    CHECK(memcmp(&got, &want, sizeof(got)) == 0);

    CHECK(merge_float(&base, 4U, 100.0f, &got));
    want            = base;
    want.linear_max = 100.0f;
    CHECK(memcmp(&got, &want, sizeof(got)) == 0);

    /* Both halves of one float must survive the merge. */
    CHECK(merge_float(&base, 8U, bits_to_float(0x3FC00000U), &got));
    CHECK(got.gain == 1.5f);
    CHECK(got.offset == 0.0f);
    CHECK(got.linear_min == 4.0f);
    CHECK(got.linear_max == 20.0f);
}

/*----------------------------------------------------------------------------*/
/* Span exhaustive scan                                                       */
/*----------------------------------------------------------------------------*/

static int model_check(reg_space_t space,
                       uint16_t start,
                       uint16_t count)
{
    static const uint16_t holding_base[9] = {0U, 16U, 32U, 48U, 64U, 80U, 96U, 112U, 128U};
    static const uint16_t holding_len[9]  = {10U, 10U, 10U, 10U, 10U, 10U, 10U, 10U, 1U};
    static const uint16_t input_base[9]   = {0U, 8U, 16U, 24U, 32U, 40U, 48U, 56U, 64U};
    const uint16_t* base;
    const uint16_t* len;
    uint32_t end;
    int i;

    if ( (space == REG_SPACE_COIL) || (space == REG_SPACE_DISCRETE) ) {
        return MODEL_EX_01;
    }
    if ( count == 0U ) {
        return MODEL_EX_02;
    }

    if ( space == REG_SPACE_HOLDING ) {
        base = holding_base;
        len  = holding_len;
    } else {
        base = input_base;
        len  = NULL;
    }

    end = (uint32_t)start + (uint32_t)count;
    for ( i = 0; i < 9; ++i ) {
        uint32_t b = (space == REG_SPACE_HOLDING) ? (uint32_t)base[i]
                                                  : (uint32_t)(i * 8);
        uint32_t n = (space == REG_SPACE_HOLDING) ? (uint32_t)len[i] : 8U;

        if ( (b <= (uint32_t)start) && (end <= (b + n)) ) {
            return MODEL_EX_NONE;
        }
    }
    return MODEL_EX_02;
}

static int code_of(modbus_slave_exception_t ex)
{
    if ( ex == MODBUS_SLAVE_EX_NONE ) {
        return MODEL_EX_NONE;
    }
    if ( ex == MODBUS_SLAVE_EX_ILLEGAL_FUNCTION ) {
        return MODEL_EX_01;
    }
    return MODEL_EX_02;
}

static void test_span_exhaustive(void)
{
    int space;
    uint32_t start;
    uint32_t count;

    for ( space = 0; space < 4; ++space ) {
        for ( start = 0U; start < 160U; ++start ) {
            for ( count = 1U; count <= 20U; ++count ) {
                const reg_span_t* span = NULL;
                modbus_slave_exception_t ex;

                ex = reg_map_check((reg_space_t)space, (uint16_t)start,
                                   (uint16_t)count, &span);
                if ( code_of(ex) != model_check((reg_space_t)space, (uint16_t)start, (uint16_t)count) ) {
                    CHECK(0);
                    printf("  space=%d start=%u count=%u ex=%d\n",
                           space, (unsigned)start, (unsigned)count, (int)ex);
                } else {
                    CHECK(1);
                }

                if ( ex == MODBUS_SLAVE_EX_NONE ) {
                    CHECK(span != NULL);
                    if ( span != NULL ) {
                        CHECK(span->space == (reg_space_t)space);
                        CHECK(span->base <= start);
                        CHECK((start + count) <= ((uint32_t)span->base + span->n));
                    }
                }
            }
        }
    }

    /* Zero-length requests never hit. */
    CHECK(reg_map_find(REG_SPACE_HOLDING, 0U, 0U) == NULL);
    CHECK(reg_map_check(REG_SPACE_HOLDING, 128U, 0U, NULL)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);

    /* A multi-register read on the single device register crosses a gap. */
    CHECK(reg_map_check(REG_SPACE_HOLDING, 128U, 2U, NULL)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);

    /* Reading across the channel-1 boundary (block gap) is refused. */
    CHECK(reg_map_check(REG_SPACE_HOLDING, 8U, 4U, NULL)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);

    /* Reading across input blocks 8 / system block is refused. */
    CHECK(reg_map_check(REG_SPACE_INPUT, 60U, 8U, NULL)
          == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);

    /* Coil and discrete spaces have no data space at all. */
    CHECK(reg_map_check(REG_SPACE_COIL, 0U, 1U, NULL)
          == MODBUS_SLAVE_EX_ILLEGAL_FUNCTION);
    CHECK(reg_map_check(REG_SPACE_DISCRETE, 0U, 1U, NULL)
          == MODBUS_SLAVE_EX_ILLEGAL_FUNCTION);
    CHECK(reg_map_find(REG_SPACE_COIL, 0U, 1U) == NULL);
    CHECK(reg_map_find(REG_SPACE_DISCRETE, 0U, 1U) == NULL);
}

/** @brief A channel span carries its identity; non-channel spans use 0xFF. */
static void test_span_identity(void)
{
    const reg_span_t* span;

    span = reg_map_find(REG_SPACE_HOLDING, 0U, 1U);
    CHECK((span != NULL) && (span->channel == 0U));
    span = reg_map_find(REG_SPACE_HOLDING, 16U, 1U);
    CHECK((span != NULL) && (span->channel == 1U));
    span = reg_map_find(REG_SPACE_HOLDING, 112U, 1U);
    CHECK((span != NULL) && (span->channel == 7U));
    span = reg_map_find(REG_SPACE_HOLDING, REG_MAP_DEV_BASE, 1U);
    CHECK((span != NULL) && (span->channel == 0xFFU));

    span = reg_map_find(REG_SPACE_INPUT, 56U, 1U);
    CHECK((span != NULL) && (span->channel == 7U));
    span = reg_map_find(REG_SPACE_INPUT, REG_MAP_SYS_BASE, 1U);
    CHECK((span != NULL) && (span->channel == 0xFFU));
}

/*----------------------------------------------------------------------------*/
/* Semantic rejects (EX 03 path)                                              */
/*----------------------------------------------------------------------------*/

static void test_semantic_illegal(void)
{
    ch_config_t cfg;
    ch_config_t decoded;
    uint8_t buf[REG_MAP_CH_CFG_LEN * 2U];

    /* Unknown composite type codes decode to the validator's reject. */
    cfg = make_cfg((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U,
                   0.0f, 0.0f, 0.0f, 1.0f);
    reg_map_ch_encode(&cfg, buf);
    modbus_slave_put_u16(&buf[0], 11U);
    reg_map_ch_decode(buf, &decoded);
    CHECK(!ch_config_validate(&decoded));

    modbus_slave_put_u16(&buf[0], 0xFFFFU);
    reg_map_ch_decode(buf, &decoded);
    CHECK(!ch_config_validate(&decoded));

    /* Out-of-range rate codes decode and then fail validation. */
    reg_map_ch_encode(&cfg, buf);
    modbus_slave_put_u16(&buf[2], 13U);
    reg_map_ch_decode(buf, &decoded);
    CHECK(decoded.rate_code == 13U);
    CHECK(!ch_config_validate(&decoded));

    /* NaN / Inf survive the codec bit for bit and fail validation. */
    reg_map_ch_encode(&cfg, buf);
    modbus_slave_put_u16(&buf[4], 0x7FC0U);
    modbus_slave_put_u16(&buf[6], 0x0000U);
    reg_map_ch_decode(buf, &decoded);
    CHECK(decoded.linear_min != decoded.linear_min);
    CHECK(!ch_config_validate(&decoded));

    reg_map_ch_encode(&cfg, buf);
    modbus_slave_put_u16(&buf[12], 0x7F80U);
    modbus_slave_put_u16(&buf[14], 0x0000U);
    reg_map_ch_decode(buf, &decoded);
    CHECK(!ch_config_validate(&decoded));

    reg_map_ch_encode(&cfg, buf);
    modbus_slave_put_u16(&buf[16], 0xFF80U);
    modbus_slave_put_u16(&buf[18], 0x0000U);
    reg_map_ch_decode(buf, &decoded);
    CHECK(!ch_config_validate(&decoded));

    /* Slave address legality (register 128 write validation). */
    CHECK(!reg_map_dev_addr_valid(0U));
    CHECK(reg_map_dev_addr_valid(REG_MAP_DEV_ADDR_MIN));
    CHECK(reg_map_dev_addr_valid(247U));
    CHECK(!reg_map_dev_addr_valid(248U));
    CHECK(!reg_map_dev_addr_valid(0xFFFFU));
}

/*----------------------------------------------------------------------------*/
/* Input area layout                                                          */
/*----------------------------------------------------------------------------*/

static uint16_t expect_input_word(const reg_map_input_image_t* in,
                                  uint16_t addr)
{
    if ( addr < 64U ) {
        uint16_t ch  = (uint16_t)(addr / 8U);
        uint16_t off = (uint16_t)(addr % 8U);

        if ( off == 0U ) {
            return (uint16_t)(float_to_bits(in->value[ch]) >> 16);
        }
        if ( off == 1U ) {
            return (uint16_t)(float_to_bits(in->value[ch]) & 0xFFFFU);
        }
        if ( off == 2U ) {
            return in->status[ch];
        }
        return 0U;
    }
    if ( addr < 72U ) {
        uint16_t off = (uint16_t)(addr - 64U);

        if ( off == 0U ) {
            return (uint16_t)(float_to_bits(in->cjc_temp) >> 16);
        }
        if ( off == 1U ) {
            return (uint16_t)(float_to_bits(in->cjc_temp) & 0xFFFFU);
        }
        if ( off == 4U ) {
            return in->fault_word;
        }
        return 0U;
    }
    return 0U;
}

static void test_input_layout(void)
{
    reg_map_input_image_t in;
    uint8_t buf[72U * 2U];
    uint16_t i;

    for ( i = 0U; i < CH_CONFIG_CHANNEL_COUNT; ++i ) {
        in.value[i]  = (float)(i + 1U) * 1.5f;
        in.status[i] = (uint16_t)(100U + i);
    }
    in.cjc_temp   = -12.5f;
    in.fault_word = 0x000FU;

    reg_map_input_encode(&in, 0U, 72U, buf);
    for ( i = 0U; i < 72U; ++i ) {
        if ( modbus_slave_get_u16(&buf[2U * i]) != expect_input_word(&in, i) ) {
            CHECK(0);
            printf("  addr=%u got=%u want=%u\n", (unsigned)i,
                   (unsigned)modbus_slave_get_u16(&buf[2U * i]),
                   (unsigned)expect_input_word(&in, i));
        } else {
            CHECK(1);
        }
    }

    /* Spot checks against hand-split words. */
    CHECK(modbus_slave_get_u16(&buf[0]) == (uint16_t)(float_to_bits(1.5f) >> 16));
    CHECK(modbus_slave_get_u16(&buf[2]) == (uint16_t)(float_to_bits(1.5f) & 0xFFFFU));
    CHECK(modbus_slave_get_u16(&buf[4]) == 100U);     /* ch1 status, addr 2 */
    CHECK(modbus_slave_get_u16(&buf[20]) == 101U);    /* ch2 status, addr 10 */
    CHECK(modbus_slave_get_u16(&buf[12]) == 0U);      /* ch1 reserved, addr 6 */
    CHECK(modbus_slave_get_u16(&buf[6U * 2U]) == 0U); /* reserved */
    CHECK(modbus_slave_get_u16(&buf[64U * 2U]) == (uint16_t)(float_to_bits(-12.5f) >> 16));
    CHECK(modbus_slave_get_u16(&buf[65U * 2U]) == (uint16_t)(float_to_bits(-12.5f) & 0xFFFFU));
    CHECK(modbus_slave_get_u16(&buf[66U * 2U]) == 0U);
    CHECK(modbus_slave_get_u16(&buf[68U * 2U]) == 0x000FU);
    CHECK(modbus_slave_get_u16(&buf[69U * 2U]) == 0U);

    /* Partial windows assemble the same registers. */
    reg_map_input_encode(&in, 64U, 8U, buf);
    for ( i = 64U; i < 72U; ++i ) {
        CHECK(modbus_slave_get_u16(&buf[2U * (i - 64U)]) == expect_input_word(&in, i));
    }

    reg_map_input_encode(&in, 10U, 3U, buf);
    for ( i = 10U; i < 13U; ++i ) {
        CHECK(modbus_slave_get_u16(&buf[2U * (i - 10U)]) == expect_input_word(&in, i));
    }

    reg_map_input_encode(&in, 63U, 1U, buf);
    CHECK(modbus_slave_get_u16(&buf[0]) == expect_input_word(&in, 63U));
}

/*----------------------------------------------------------------------------*/
/* Fault word and wire break codes                                            */
/*----------------------------------------------------------------------------*/

static void test_fault_word(void)
{
    CHECK(reg_map_fault_word(1U, 0U) == 0x0000U);
    CHECK(reg_map_fault_word(0U, 0U) == REG_MAP_FAULT_CJC);
    CHECK(reg_map_fault_word(0U, 0x000EU) == 0x000FU);
    CHECK(reg_map_fault_word(1U, 0x000EU) == 0x000EU);

    /* Bits above b3 are reserved and never appear. */
    CHECK(reg_map_fault_word(1U, 0xFFFFU) == 0x000EU);
    CHECK(reg_map_fault_word(0U, 0xFFFFU) == 0x000FU);
}

static void test_wire_code(void)
{
    /* A normal circuit is always status 0. */
    CHECK(reg_map_wire_status_code((uint8_t)CH_TYPE_TC, 0U) == 0U);
    CHECK(reg_map_wire_status_code((uint8_t)CH_TYPE_PT100, 0U) == 0U);

    /* Thermocouple: any open loop is status 2 (leads not resolvable). */
    CHECK(reg_map_wire_status_code((uint8_t)CH_TYPE_TC, 1U) == 2U);
    CHECK(reg_map_wire_status_code((uint8_t)CH_TYPE_TC, 2U) == 2U);
    CHECK(reg_map_wire_status_code((uint8_t)CH_TYPE_TC, 3U) == 2U);

    /* PT100: resolvable leads report 3 / 4. */
    CHECK(reg_map_wire_status_code((uint8_t)CH_TYPE_PT100, 2U) == 3U);
    CHECK(reg_map_wire_status_code((uint8_t)CH_TYPE_PT100, 3U) == 4U);

    /* Unresolvable PT100 break: front-end contract violation, status 6. */
    CHECK(reg_map_wire_status_code((uint8_t)CH_TYPE_PT100, 1U) == 6U);

    /* Non-detectable types and unknown wire codes: status 6. */
    CHECK(reg_map_wire_status_code((uint8_t)CH_TYPE_4_20MA, 1U) == 6U);
    CHECK(reg_map_wire_status_code((uint8_t)CH_TYPE_0_10V, 2U) == 6U);
    CHECK(reg_map_wire_status_code((uint8_t)CH_TYPE_PT100, 7U) == 6U);
    CHECK(reg_map_wire_status_code((uint8_t)CH_TYPE_0_10V, 7U) == 6U);
}

int main(void)
{
    test_roundtrip();
    test_subblock_merge();
    test_span_exhaustive();
    test_span_identity();
    test_semantic_illegal();
    test_input_layout();
    test_fault_word();
    test_wire_code();

    printf("%d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
