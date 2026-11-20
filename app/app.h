#ifndef APP_H
#define APP_H

/**
 * @file app.h
 * @brief 应用层词汇：通道模型、信号与事件载荷。
 *
 * 供所有活动对象与纯寄存器映射共用。信号集合与载荷布局遵循
 * docs/qp-application-design.md 第 4 节（事件契约）。静态事件不带参数：
 * 无参事件为文件作用域 `QEvt const`（QEVT_INITIALIZER），时间事件为其
 * AO 的 QTimeEvt 成员（QP/C 示例风格）。
 */

#include <stdint.h>

#include "board_config.h"
#include "qpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*/
/* 通道模型                                                                   */
/*----------------------------------------------------------------------------*/

/** @brief 测量通道数量（板级硬件事实，不是应用层选择）。 */
#define CH_CONFIG_CHANNEL_COUNT BOARD_CHANNEL_COUNT

/** @brief 存放于 ch_config_t::type 的输入类型码。 */
typedef enum {
    CH_TYPE_TC     = 0, /**< 热电偶。 */
    CH_TYPE_PT100  = 1, /**< RTD（热电阻）。 */
    CH_TYPE_4_20MA = 2, /**< 电流输入。 */
    CH_TYPE_0_10V  = 3  /**< 电压输入。 */
} ch_type_t;

/** @brief 存放于 ch_config_t::tc_type 的热电偶分度类型码。 */
typedef enum {
    CH_TC_TYPE_B = 0,
    CH_TC_TYPE_E = 1,
    CH_TC_TYPE_J = 2,
    CH_TC_TYPE_K = 3,
    CH_TC_TYPE_N = 4,
    CH_TC_TYPE_R = 5,
    CH_TC_TYPE_S = 6,
    CH_TC_TYPE_T = 7
} ch_tc_type_t;

/** @brief 表示“不是热电偶”的 tc_type 值（归一化结果）。 */
#define CH_TC_TYPE_NONE 0xFFU

/**
 * @brief 各速率码对应的采样速率：每个速率码一行。
 *
 * X(code, millihertz)：名称列构成 rate_code_t，数值列构成驱动速率表
 * （其中引用板级档位常量，只在可见处展开）。
 */
#define APP_RATE_LIST(X)                                \
    X(CH_RATE_2_5_HZ, ANALOG_INPUT_BOARD_RATE_2_5_HZ)   \
    X(CH_RATE_5_HZ, ANALOG_INPUT_BOARD_RATE_5_HZ)       \
    X(CH_RATE_10_HZ, ANALOG_INPUT_BOARD_RATE_10_HZ)     \
    X(CH_RATE_12_5_HZ, ANALOG_INPUT_BOARD_RATE_12_5_HZ) \
    X(CH_RATE_15_HZ, ANALOG_INPUT_BOARD_RATE_15_HZ)     \
    X(CH_RATE_20_HZ, ANALOG_INPUT_BOARD_RATE_20_HZ)     \
    X(CH_RATE_25_HZ, ANALOG_INPUT_BOARD_RATE_25_HZ)     \
    X(CH_RATE_30_HZ, ANALOG_INPUT_BOARD_RATE_30_HZ)     \
    X(CH_RATE_40_HZ, ANALOG_INPUT_BOARD_RATE_40_HZ)     \
    X(CH_RATE_50_HZ, ANALOG_INPUT_BOARD_RATE_50_HZ)     \
    X(CH_RATE_60_HZ, ANALOG_INPUT_BOARD_RATE_60_HZ)     \
    X(CH_RATE_80_HZ, ANALOG_INPUT_BOARD_RATE_80_HZ)     \
    X(CH_RATE_100_HZ, ANALOG_INPUT_BOARD_RATE_100_HZ)

/** @brief 采样速率码；采样速率的寄存器编码。 */
typedef enum {
#define X(code, millihertz) code,
    APP_RATE_LIST(X)
#undef X
} rate_code_t;

/** @brief 最大有效速率码（含）；0..CH_CONFIG_RATE_CODE_MAX。 */
#define CH_CONFIG_RATE_CODE_MAX CH_RATE_100_HZ

/** @brief 采样速率表中的条目数。 */
#define CH_CONFIG_RATE_CODE_COUNT (CH_CONFIG_RATE_CODE_MAX + 1U)

/* 漂移守卫：该上界必须与 APP_RATE_LIST 的行数一致。 */
#define APP_RATE_COUNT_ONE(code, millihertz) +1U
Q_ASSERT_STATIC((0U APP_RATE_LIST(APP_RATE_COUNT_ONE)) == CH_CONFIG_RATE_CODE_COUNT);
#undef APP_RATE_COUNT_ONE

/**
 * @brief 单个通道的配置，20 字节，无填充，无版本字段。
 *
 * 该结构体本身就是 KV 值格式（键 "cfg/chN"），也是寄存器块编码的来源，
 * 因此布局必须保持稳定。校验与归一化是同一次操作：语义相同的配置必须
 * 逐字节相等（modbus_ao.c 写事务中的变化检查依赖 memcmp）。该契约由
 * modbus_ao.c 中的校验强制：type 必须为 CH_TYPE_*；热电偶的 tc_type 必须
 * 为 CH_TC_TYPE_B..T，其余情况归一化为 CH_TC_TYPE_NONE；rate_code 必须为
 * 0..CH_CONFIG_RATE_CODE_MAX；四个浮点数必须均为有限值；非线性输入类型的
 * linear_min/linear_max 归一化为 0.0。
 */
typedef struct {
    uint8_t type;       /**< 输入类型，ch_type_t。 */
    uint8_t tc_type;    /**< 热电偶分度类型（CH_TC_TYPE_*）或 CH_TC_TYPE_NONE。 */
    uint16_t rate_code; /**< 采样速率码，rate_code_t（0..12）。 */
    float linear_min;   /**< 线性输入下限，工程值。 */
    float linear_max;   /**< 线性输入上限，工程值。 */
    float offset;       /**< 校准偏置：value = raw * gain + offset。 */
    float gain;         /**< 校准增益。 */
} ch_config_t;

/* 编译期守卫：结构体内容就是 20 字节的持久化格式。 */
Q_ASSERT_STATIC(sizeof(ch_config_t) == 20U);

/** @brief 通道状态寄存器码（线上值冻结）。 */
typedef enum {
    CH_STATUS_OK               = 0, /**< 有效测量。 */
    CH_STATUS_UNCONFIGURED     = 1, /**< 无生效配置。 */
    CH_STATUS_TC_BREAK         = 2, /**< 热电偶回路开路。 */
    CH_STATUS_RTD_LEAD_BREAK   = 3, /**< PT100 激励线开路。 */
    CH_STATUS_RTD_RETURN_BREAK = 4, /**< PT100 回流线开路。 */
    CH_STATUS_NO_CJC           = 5, /**< 冷端无效（TC）。 */
    CH_STATUS_HW_ERROR         = 6, /**< 硬件或 IO 错误。 */
    CH_STATUS_OUT_OF_RANGE     = 7  /**< 转换超量程。 */
} ch_status_t;

/*----------------------------------------------------------------------------*/
/* 信号与事件载荷                                                             */
/*----------------------------------------------------------------------------*/

/** @brief 应用层信号。 */
typedef enum {
    CONFIG_SET_SIG = Q_USER_SIG, /**< {ch, cfg}：生效配置。 */
    APPLY_SIG,                   /**< 改配重入（静态）。 */
    CFG_APPLIED_SIG,             /**< {ch}：通道已接受其配置。 */
    MEASURE_SIG,                 /**< {ch, value, status}：一次采样。 */
    CJC_SIG,                     /**< {temp_c, valid}：冷端心跳。 */
    SAMPLE_SIG,                  /**< 采样节拍（时间事件）。 */
    SETTLE_SIG,                  /**< 稳定/检查期限（时间事件）。 */
    BREAK_WINDOW_SIG,            /**< 3 s 断线窗口（时间事件）。 */
    TICK_SIG,                    /**< 冷端 1 Hz 节拍（时间事件）。 */
    FRAME_SIG,                   /**< 捕获到 Modbus 帧（静态）。 */
    TX_DONE_SIG,                 /**< Modbus 应答已发送（静态）。 */
    TX_TIMEOUT_SIG,              /**< Modbus TX 看门狗触发（静态）。 */
    MAX_SIG                      /**< 信号总数（psInit 上界）。 */
} app_signal_t;

/** @brief CONFIG_SET_SIG 载荷（池 B）。 */
typedef struct {
    QEvt super;      /**< 事件头。 */
    uint8_t ch;      /**< 通道索引，0 基。 */
    ch_config_t cfg; /**< 新的生效配置。 */
} config_set_evt_t;

/** @brief MEASURE_SIG 载荷（池 A）。 */
typedef struct {
    QEvt super;      /**< 事件头。 */
    uint8_t ch;      /**< 通道索引，0 基。 */
    float value;     /**< 校准后的工程值（故障时冻结）。 */
    uint16_t status; /**< 通道状态码（ch_status_t）。 */
} measure_evt_t;

/** @brief CFG_APPLIED_SIG 载荷（池 A）。 */
typedef struct {
    QEvt super; /**< 事件头。 */
    uint8_t ch; /**< 通道索引，0 基。 */
} cfg_applied_evt_t;

/** @brief CJC_SIG 载荷（池 A）。 */
typedef struct {
    QEvt super;    /**< 事件头。 */
    float temp_c;  /**< 最近一次有效的冷端温度。 */
    uint8_t valid; /**< 非零表示 temp_c 可用。 */
} cjc_evt_t;

/* 池 A 存放 MEASURE_SIG、CFG_APPLIED_SIG 与 CJC_SIG；measure_evt_t 必须是
 * 最大的块。 */
Q_ASSERT_STATIC(sizeof(cfg_applied_evt_t) <= sizeof(measure_evt_t));
Q_ASSERT_STATIC(sizeof(cjc_evt_t) <= sizeof(measure_evt_t));

/*----------------------------------------------------------------------------*/
/* 活动对象优先级                                                             */
/*----------------------------------------------------------------------------*/

/** @brief 第一个 AoChannel 的优先级（各通道占用 first..first+N-1）。 */
#define APP_PRIO_CH_FIRST 2U

/** @brief AoColdJunction 的优先级。 */
#define APP_PRIO_CJC 10U

/**
 * @brief AoModbus 的优先级；必须始终是所有活动对象中最高的。
 *
 * docs/qp-application-design.md 第 7 节的 QV 容量论证依赖
 * APP_PRIO_MODBUS 最高：AoModbus 必须在其他任何 AO 产生下一个事件之前
 * 派发已发布的事件。
 */
#define APP_PRIO_MODBUS 11U

/*----------------------------------------------------------------------------*/
/* 事件池                                                                     */
/*----------------------------------------------------------------------------*/

/**
 * @brief 池 A 的块数量（MEASURE/CFG_APPLIED/CJC 事件）。
 *
 * 依据 docs/qp-application-design.md 第 7 节的容量表确定。
 */
#define APP_POOL_A_BLOCK_NUM 16U

/**
 * @brief 池 B 的块数量（CONFIG_SET 事件）。
 *
 * 依据 docs/qp-application-design.md 第 7 节的容量表确定。
 */
#define APP_POOL_B_BLOCK_NUM 16U

/*----------------------------------------------------------------------------*/
/* 板级位置漂移守卫                                                           */
/*----------------------------------------------------------------------------*/

/*
 * 位置列表本身及其派生数量（BOARD_POSITIONS、BOARD_POSITION_COUNT）属于
 * 板级事实，存放在 board_config.h 中；数量在那里由列表展开，因此不可能也
 * 不需要漂移守卫。这里只保留编译期守卫：它们需要 qpc.h 中的
 * Q_ASSERT_STATIC。
 */

/* 漂移守卫：SKU 通道数必须不超过板级位置列表；modbus_ao 的位掩码和 KV
 * 键表都按位置索引。 */
Q_ASSERT_STATIC(CH_CONFIG_CHANNEL_COUNT <= BOARD_POSITION_COUNT);

#ifdef __cplusplus
}
#endif

#endif
