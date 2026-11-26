#ifndef REG_MAP_H
#define REG_MAP_H

/**
 * @file reg_map.h
 * @brief Modbus 数据空间映射：寻址、跨度表与寄存器编解码。
 *
 * 纯函数，无状态。类型化镜像与写事务位于 modbus_ao.c（由 AoModbus 拥有）。
 * 数据定义（地址、编码）遵循 docs/ao/modbus_ao.md 第 4 节。
 */

#include <stdint.h>

#include "app.h"
#include "modbus_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*/
/* 几何                                                                       */
/*----------------------------------------------------------------------------*/

/** @brief 每个通道配置块的寄存器数（保持寄存器）。 */
#define REG_MAP_CH_CFG_LEN 10U

/** @brief 设备寄存器块的基址（保持寄存器）。 */
#define REG_MAP_DEV_BASE 128U

/** @brief 每个通道遥测块的寄存器数（输入寄存器）。 */
#define REG_MAP_CH_IN_LEN 8U

/** @brief 通道遥测块之间的地址步进（输入寄存器）。 */
#define REG_MAP_CH_IN_STRIDE 8U

/** @brief 系统块的基址（输入寄存器）。 */
#define REG_MAP_SYS_BASE 64U

/** @brief 出厂默认 Modbus 从站地址（单一来源：核心）。 */
#define REG_MAP_DEFAULT_DEV_ADDR MODBUS_SLAVE_ADDRESS_MIN

/*----------------------------------------------------------------------------*/
/* 数据空间与跨度                                                             */
/*----------------------------------------------------------------------------*/

/** @brief Modbus 四个数据空间。 */
typedef enum {
    REG_SPACE_COIL = 0, /**< 线圈（FC01/05/15），位读写。 */
    REG_SPACE_DISCRETE, /**< 离散输入（FC02），仅位读。 */
    REG_SPACE_HOLDING,  /**< 保持寄存器（FC03/06/16），寄存器读写。 */
    REG_SPACE_INPUT     /**< 输入寄存器（FC04），仅寄存器读。 */
} reg_space_t;

/** @brief 跨度种类：驱动编解码派发。 */
typedef enum {
    REG_SPAN_CH_CFG = 0, /**< 通道配置块（10 个寄存器）。 */
    REG_SPAN_DEV,        /**< 设备寄存器（从站地址）。 */
    REG_SPAN_CH_IN,      /**< 通道遥测块（8 个寄存器）。 */
    REG_SPAN_SYS         /**< 系统块（CJC + 故障字）。 */
} reg_span_kind_t;

/** @brief 一个连续的已映射区域（跨度）。 */
typedef struct {
    reg_space_t space;    /**< 本跨度所属的数据空间。 */
    uint16_t base;        /**< 首地址（0 基）。 */
    uint16_t n;           /**< 长度，单位寄存器或位。 */
    reg_span_kind_t kind; /**< 跨度种类；驱动编解码派发。 */
    uint8_t channel;      /**< 通道号，0 基；非通道跨度为 0xFF。 */
} reg_span_t;

/**
 * @brief 统一回调入口：异常码加命中的跨度。
 *
 * @param space 被访问的数据空间。
 * @param start 首地址（0 基）。
 * @param count 寄存器或位的数量。
 * @param out 成功时接收命中的跨度；可为 NULL。
 * @return 命中返回 MODBUS_SLAVE_EX_NONE；范围不在单个跨度内返回
 *         MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS（02）；空间完全没有跨度
 *         时返回 MODBUS_SLAVE_EX_ILLEGAL_FUNCTION（01）（本产品：线圈与
 *         离散输入）。
 */
modbus_slave_exception_t reg_map_check(reg_space_t space,
                                       uint16_t start,
                                       uint16_t count,
                                       const reg_span_t** out);

/*----------------------------------------------------------------------------*/
/* 通道配置块编解码（10 个寄存器）                                            */
/*----------------------------------------------------------------------------*/

/**
 * @brief 把类型化配置编码为 10 个大端寄存器。
 *
 * @param cfg 待编码的配置；无需已校验或已归一化。
 * @param buf 接收 REG_MAP_CH_CFG_LEN * 2 字节，按线上字节序。
 */
void reg_map_ch_encode(const ch_config_t* cfg,
                       uint8_t* buf);

/**
 * @brief 把 10 个大端寄存器解码为类型化配置。
 *
 * 只做布局：大于 10 的输入类型码解码为 0xFF，以便 modbus_ao.c 中的校验
 * 拒绝它；语义校验仍留在那里（契约记录在 app.h 的 ch_config_t 上）。
 *
 * @param buf REG_MAP_CH_CFG_LEN * 2 字节，按线上字节序。
 * @param cfg 接收解码后的配置。
 */
void reg_map_ch_decode(const uint8_t* buf,
                       ch_config_t* cfg);

/*----------------------------------------------------------------------------*/
/* 设备寄存器编解码（1 个寄存器）                                             */
/*----------------------------------------------------------------------------*/

/** @brief 编码从站地址寄存器。 */
void reg_map_dev_encode(uint16_t addr,
                        uint8_t* buf);

/** @brief 解码从站地址寄存器。 */
uint16_t reg_map_dev_decode(const uint8_t* buf);

/*----------------------------------------------------------------------------*/
/* 输入区                                                                     */
/*----------------------------------------------------------------------------*/

/**
 * @brief 由输入区状态组装输入寄存器。
 *
 * 各参数列表就是输入区的原始状态（调用者拥有的镜像）；保留寄存器回读为
 * 0，无需状态。派发与保持寄存器路径完全一致，按 span->kind 进行：
 * REG_SPAN_CH_IN 使用 span->channel 及相对 span->base 的偏移，
 * REG_SPAN_SYS 组装 CJC 与故障字；其他种类一律回读为 0。
 *
 * @param value 每通道最近测量值（BOARD_CHANNEL_COUNT 项）。
 * @param status 每通道状态码。
 * @param cjc_temp 冷端温度。
 * @param fault_word 设备故障字（寄存器 68）。
 * @param span 该范围命中的跨度，由 reg_map_check() 给出；必须包含
 *        [start, start + count)。
 * @param start 首寄存器地址（0 基）。
 * @param count 寄存器数量。
 * @param buf 接收 count * 2 字节，按线上字节序。
 */
void reg_map_input_encode(const float* value,
                          const uint16_t* status,
                          float cjc_temp,
                          uint16_t fault_word,
                          const reg_span_t* span,
                          uint16_t start,
                          uint16_t count,
                          uint8_t* buf);

/*----------------------------------------------------------------------------*/
/* 设备故障字                                                                 */
/*----------------------------------------------------------------------------*/

/** @brief 故障字 b0：冷端无效或未就绪。 */
#define REG_MAP_FAULT_CJC 0x0001U

/** @brief 故障字 b1：参数写入失败（锁存）。 */
#define REG_MAP_FAULT_PARAM_WRITE 0x0002U

/** @brief 故障字 b2：参数加载失败（锁存）。 */
#define REG_MAP_FAULT_PARAM_LOAD 0x0004U

/** @brief 故障字 b3：FlashDB 初始化失败（锁存）。 */
#define REG_MAP_FAULT_FDB_INIT 0x0008U

/** @brief 调用者可传入的锁存故障位掩码。 */
#define REG_MAP_FAULT_LATCHED_MASK \
    (REG_MAP_FAULT_PARAM_WRITE | REG_MAP_FAULT_PARAM_LOAD | REG_MAP_FAULT_FDB_INIT)

/**
 * @brief 合成设备故障字寄存器的值。
 *
 * @param cjc_valid 冷端读数有效时非零。
 * @param latched 锁存位，可为 REG_MAP_FAULT_PARAM_WRITE、
 *        REG_MAP_FAULT_PARAM_LOAD 与 REG_MAP_FAULT_FDB_INIT 的任意组合。
 * @return 寄存器值；b0 实时反映冷端有效性，b1..b3 携带锁存状态。
 *         b4..b15 恒为 0。
 */
uint16_t reg_map_fault_word(uint8_t cjc_valid,
                            uint16_t latched);

#ifdef __cplusplus
}
#endif

#endif
