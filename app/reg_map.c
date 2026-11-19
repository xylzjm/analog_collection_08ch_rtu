/**
 * @file reg_map.c
 * @brief Modbus data-space map: span table, addressing and register codecs.
 */

#include "reg_map.h"

#include <stdbool.h>
#include <string.h>

/*
 * The one and only register map. Segment order follows reg_space_t so a
 * space with no spans (coils, discrete inputs) answers "unmapped" from the
 * table itself. Holding: 8 channel configuration blocks at 16n + device
 * register 128. Input: 8 channel telemetry blocks at 8n + system block 64.
 * The channel rows derive from the board position list in app.h.
 */
/** @brief Channel field of a span that does not belong to one channel. */
#define SPAN_NO_CHANNEL 0xFFU

/** @brief Address stride between channel configuration blocks (holding). */
#define SPAN_HOLDING_STRIDE 16U

static const reg_span_t spans[] = {
#define X(pos) {REG_SPACE_HOLDING, (uint16_t)((pos) * SPAN_HOLDING_STRIDE), REG_MAP_CH_CFG_LEN, REG_SPAN_CH_CFG, (uint8_t)(pos)},
    APP_BOARD_POSITIONS(X)
#undef X
    {REG_SPACE_HOLDING, REG_MAP_DEV_BASE, 1U, REG_SPAN_DEV, SPAN_NO_CHANNEL},
#define X(pos) {REG_SPACE_INPUT, (uint16_t)((pos) * REG_MAP_CH_IN_STRIDE), REG_MAP_CH_IN_LEN, REG_SPAN_CH_IN, (uint8_t)(pos)},
    APP_BOARD_POSITIONS(X)
#undef X
    {REG_SPACE_INPUT, REG_MAP_SYS_BASE, REG_MAP_CH_IN_LEN, REG_SPAN_SYS, SPAN_NO_CHANNEL}};

/* Compile-time guard: two rows per board position plus the two device rows. */
typedef char span_count_check[((sizeof(spans) / sizeof(spans[0]))
                               == (2U * APP_BOARD_POSITION_COUNT + 2U))
                                  ? 1
                                  : -1];

/**
 * @brief Write one big-endian float32 as two registers (high word first).
 */
static void put_float(uint8_t* buf, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    modbus_slave_put_u16(&buf[0], (uint16_t)(bits >> 16));
    modbus_slave_put_u16(&buf[2], (uint16_t)(bits & 0xFFFFU));
}

/**
 * @brief Read one big-endian float32 from two registers (high word first).
 */
static float get_float(const uint8_t* buf)
{
    uint32_t bits;
    float value;

    bits = ((uint32_t)modbus_slave_get_u16(&buf[0]) << 16)
           | (uint32_t)modbus_slave_get_u16(&buf[2]);
    memcpy(&value, &bits, sizeof(value));
    return value;
}

const reg_span_t* reg_map_find(reg_space_t space,
                               uint16_t start,
                               uint16_t count)
{
    uint32_t end;
    size_t i;

    if ( count == 0U ) {
        return NULL;
    }
    end = (uint32_t)start + (uint32_t)count;

    for ( i = 0U; i < (sizeof(spans) / sizeof(spans[0])); ++i ) {
        const reg_span_t* span = &spans[i];

        if ( span->space != space ) {
            continue;
        }
        /* Unpopulated channel positions are not part of the map: the table
         * describes the 8-position board, the SKU population filters it. */
        if ( ((span->kind == REG_SPAN_CH_CFG) || (span->kind == REG_SPAN_CH_IN))
             && (span->channel >= CH_CONFIG_CHANNEL_COUNT) ) {
            continue;
        }
        if ( ((uint32_t)span->base <= (uint32_t)start)
             && (end <= ((uint32_t)span->base + (uint32_t)span->n)) ) {
            return span;
        }
    }
    return NULL;
}

modbus_slave_exception_t reg_map_check(reg_space_t space,
                                       uint16_t start,
                                       uint16_t count,
                                       const reg_span_t** out)
{
    const reg_span_t* span;
    bool space_mapped = false;
    size_t i;

    for ( i = 0U; i < (sizeof(spans) / sizeof(spans[0])); ++i ) {
        if ( spans[i].space == space ) {
            space_mapped = true;
            break;
        }
    }
    if ( !space_mapped ) {
        return MODBUS_SLAVE_EX_ILLEGAL_FUNCTION;
    }

    span = reg_map_find(space, start, count);
    if ( span == NULL ) {
        return MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS;
    }
    if ( out != NULL ) {
        *out = span;
    }
    return MODBUS_SLAVE_EX_NONE;
}

void reg_map_ch_encode(const ch_config_t* cfg,
                       uint8_t* buf)
{
    uint16_t code;

    switch ( cfg->type ) {
        case (uint8_t)CH_TYPE_TC:
            code = (uint16_t)cfg->tc_type;
            break;
        case (uint8_t)CH_TYPE_PT100:
            code = 8U;
            break;
        case (uint8_t)CH_TYPE_4_20MA:
            code = 9U;
            break;
        case (uint8_t)CH_TYPE_0_10V:
            code = 10U;
            break;
        default:
            code = 11U; /* out of range on purpose; reads back as invalid */
            break;
    }

    modbus_slave_put_u16(&buf[0], code);
    modbus_slave_put_u16(&buf[2], cfg->rate_code);
    put_float(&buf[4], cfg->linear_min);
    put_float(&buf[8], cfg->linear_max);
    put_float(&buf[12], cfg->offset);
    put_float(&buf[16], cfg->gain);
}

void reg_map_ch_decode(const uint8_t* buf,
                       ch_config_t* cfg)
{
    uint16_t code = modbus_slave_get_u16(&buf[0]);

    cfg->rate_code  = modbus_slave_get_u16(&buf[2]);
    cfg->linear_min = get_float(&buf[4]);
    cfg->linear_max = get_float(&buf[8]);
    cfg->offset     = get_float(&buf[12]);
    cfg->gain       = get_float(&buf[16]);

    if ( code < 8U ) {
        cfg->type    = (uint8_t)CH_TYPE_TC;
        cfg->tc_type = (uint8_t)code;
    } else if ( code == 8U ) {
        cfg->type    = (uint8_t)CH_TYPE_PT100;
        cfg->tc_type = CH_TC_TYPE_NONE;
    } else if ( code == 9U ) {
        cfg->type    = (uint8_t)CH_TYPE_4_20MA;
        cfg->tc_type = CH_TC_TYPE_NONE;
    } else if ( code == 10U ) {
        cfg->type    = (uint8_t)CH_TYPE_0_10V;
        cfg->tc_type = CH_TC_TYPE_NONE;
    } else {
        /* Layout only: unknown codes become a sentinel the validator rejects. */
        cfg->type    = 0xFFU;
        cfg->tc_type = CH_TC_TYPE_NONE;
    }
}

void reg_map_dev_encode(uint16_t addr,
                        uint8_t* buf)
{
    modbus_slave_put_u16(&buf[0], addr);
}

uint16_t reg_map_dev_decode(const uint8_t* buf)
{
    return modbus_slave_get_u16(&buf[0]);
}

void reg_map_input_encode(const float* value,
                          const uint16_t* status,
                          float cjc_temp,
                          uint16_t fault_word,
                          uint16_t start,
                          uint16_t count,
                          uint8_t* buf)
{
    uint16_t i;

    for ( i = 0U; i < count; ++i ) {
        uint16_t addr = (uint16_t)(start + i);
        uint16_t word = 0U;

        if ( addr < REG_MAP_SYS_BASE ) {
            uint16_t ch  = (uint16_t)(addr / REG_MAP_CH_IN_STRIDE);
            uint16_t off = (uint16_t)(addr % REG_MAP_CH_IN_STRIDE);

            if ( off == 0U ) {
                uint32_t bits;

                memcpy(&bits, &value[ch], sizeof(bits));
                word = (uint16_t)(bits >> 16);
            } else if ( off == 1U ) {
                uint32_t bits;

                memcpy(&bits, &value[ch], sizeof(bits));
                word = (uint16_t)(bits & 0xFFFFU);
            } else if ( off == 2U ) {
                word = status[ch];
            } else {
                word = 0U; /* reserved */
            }
        } else if ( addr < (REG_MAP_SYS_BASE + REG_MAP_CH_IN_LEN) ) {
            uint16_t off = (uint16_t)(addr - REG_MAP_SYS_BASE);

            if ( off == 0U ) {
                uint32_t bits;

                memcpy(&bits, &cjc_temp, sizeof(bits));
                word = (uint16_t)(bits >> 16);
            } else if ( off == 1U ) {
                uint32_t bits;

                memcpy(&bits, &cjc_temp, sizeof(bits));
                word = (uint16_t)(bits & 0xFFFFU);
            } else if ( off == 4U ) {
                word = fault_word;
            } else {
                word = 0U; /* reserved */
            }
        } else {
            word = 0U; /* defensive: outside every span */
        }

        modbus_slave_put_u16(&buf[2U * i], word);
    }
}

uint16_t reg_map_fault_word(uint8_t cjc_valid,
                            uint16_t latched)
{
    uint16_t word = (uint16_t)(latched & REG_MAP_FAULT_LATCHED_MASK);

    if ( cjc_valid == 0U ) {
        word |= REG_MAP_FAULT_CJC;
    }
    return word;
}
