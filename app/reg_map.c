/**
 * @file reg_map.c
 * @brief Modbus 数据空间映射：跨度表、寻址与寄存器编解码。
 */

#include "reg_map.h"

#include <stdbool.h>
#include <string.h>

/*
 * 唯一的寄存器映射。分段顺序遵循 reg_space_t，因此没有跨度的空间（线圈、
 * 离散输入）直接由表本身给出“未映射”。保持寄存器：8 个通道配置块，位于
 * 16n，外加设备寄存器 128。输入寄存器：8 个通道遥测块，位于 8n，外加
 * 系统块 64。通道行由 board_config.h 中的板级位置列表派生。
 */
/** @brief 不属于任何通道的跨度的通道字段。 */
#define SPAN_NO_CHANNEL 0xFFU

/** @brief 通道配置块之间的地址步进（保持寄存器）。 */
#define SPAN_HOLDING_STRIDE 16U

static const reg_span_t spans[] = {
#define X(pos)                                                   \
    {REG_SPACE_HOLDING, (uint16_t)((pos) * SPAN_HOLDING_STRIDE), \
     REG_MAP_CH_CFG_LEN, REG_SPAN_CH_CFG, (uint8_t)(pos)},
    BOARD_POSITIONS(X)
#undef X
        {REG_SPACE_HOLDING, REG_MAP_DEV_BASE, 1U, REG_SPAN_DEV, SPAN_NO_CHANNEL},
#define X(pos)                                                  \
    {REG_SPACE_INPUT, (uint16_t)((pos) * REG_MAP_CH_IN_STRIDE), \
     REG_MAP_CH_IN_LEN, REG_SPAN_CH_IN, (uint8_t)(pos)},
    BOARD_POSITIONS(X)
#undef X
        {REG_SPACE_INPUT, REG_MAP_SYS_BASE, REG_MAP_CH_IN_LEN, REG_SPAN_SYS, SPAN_NO_CHANNEL},
};

/* 编译期守卫：每个板级位置两行，再加两行设备行。 */
Q_ASSERT_STATIC((sizeof(spans) / sizeof(spans[0]))
                == (2U * BOARD_POSITION_COUNT + 2U));

/**
 * @brief 把一个大端 float32 写成两个寄存器（高字在前）。
 */
static void put_float(uint8_t* buf, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    modbus_slave_put_u16(&buf[0], (uint16_t)(bits >> 16));
    modbus_slave_put_u16(&buf[2], (uint16_t)(bits & 0xFFFFU));
}

/**
 * @brief 从两个寄存器读出一个大端 float32（高字在前）。
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

/**
 * @brief 查找完整包含某个地址范围的跨度。
 *
 * @param space 要搜索的数据空间。
 * @param start 首地址（0 基）。
 * @param count 寄存器或位的数量；必须 >= 1。
 * @return 包含该范围的跨度；范围为空、跨越块边界、落入未映射空档或超出
 *         数据空间时返回 NULL。
 */
static const reg_span_t* reg_map_find(reg_space_t space,
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
        /* 未装配的通道位置不属于映射：表描述的是 8 位置板级布局，由 SKU
         * 装配情况过滤。 */
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
            code = 11U; /* 有意越界；回读为非法值 */
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
        /* 只做布局：未知码变为校验器会拒绝的哨兵值。 */
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

/**
 * @brief 返回大端 float32 寄存器对中的一个字。
 *
 * @param value 待拆分的值。
 * @param index 0 为高字，1 为低字。
 */
static uint16_t float_word(float value,
                           uint16_t index)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return (uint16_t)((index == 0U) ? (bits >> 16) : (bits & 0xFFFFU));
}

void reg_map_input_encode(const float* value,
                          const uint16_t* status,
                          float cjc_temp,
                          uint16_t fault_word,
                          const reg_span_t* span,
                          uint16_t start,
                          uint16_t count,
                          uint8_t* buf)
{
    uint16_t i;

    for ( i = 0U; i < count; ++i ) {
        uint16_t off  = (uint16_t)(start + i - span->base);
        uint16_t word = 0U;

        if ( span->kind == REG_SPAN_CH_IN ) {
            if ( off == 0U ) {
                word = float_word(value[span->channel], 0U);
            } else if ( off == 1U ) {
                word = float_word(value[span->channel], 1U);
            } else if ( off == 2U ) {
                word = status[span->channel];
            } else {
                word = 0U; /* 保留 */
            }
        } else if ( span->kind == REG_SPAN_SYS ) {
            if ( off == 0U ) {
                word = float_word(cjc_temp, 0U);
            } else if ( off == 1U ) {
                word = float_word(cjc_temp, 1U);
            } else if ( off == 4U ) {
                word = fault_word;
            } else {
                word = 0U; /* 保留 */
            }
        } else {
            word = 0U; /* 防御：保持寄存器种类不会进入本编解码 */
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
