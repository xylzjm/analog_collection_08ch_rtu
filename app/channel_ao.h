#ifndef CHANNEL_AO_H
#define CHANNEL_AO_H

/**
 * @file channel_ao.h
 * @brief 通道采集活动对象（每通道一个实例）。
 *
 * 拥有单个通道的整个采集生命周期：配置应用、采样、校准、断线检测循环
 * 以及通道状态。参见 docs/ao/channel_ao.md。
 */

#include "analog_input.h"
#include "qpc.h"

#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 队列深度（启动突发 + CJC + 节拍 + APPLY 事件 = 13；不设显式裕量，
 *        安全性来自 QV 排空特性）。
 */
#define CHANNEL_AO_QUEUE_LEN 13U

/** @brief 断线检查周期，单位 tick（3 s）。 */
#define CHANNEL_AO_BREAK_WINDOW_MS 3000U

/** @brief AoChannel 活动对象。 */
typedef struct {
    QActive super;         /**< 活动对象基类。 */
    QTimeEvt te_sample;    /**< 采样周期定时器。 */
    QTimeEvt te_settle;    /**< 稳定/检查期限定时器。 */
    QTimeEvt te_break;     /**< 断线窗口定时器。 */
    analog_input_t* input; /**< 板级通道对象（借用）。 */
    ch_config_t cfg;       /**< 生效配置。 */
    ch_config_t pending;   /**< 等待 APPLY_SIG 的配置。 */
    float cjc_temp;        /**< 缓存的冷端温度。 */
    float last_value;      /**< 最近发布的值（故障时冻结）。 */
    uint32_t settle_ms;    /**< 下一次进入 Settling 的延迟。 */
    uint8_t cjc_valid;     /**< 缓存的冷端有效性。 */
    uint8_t wire;          /**< 最近一次检查的 analog_input_wire_status_t。 */
    uint8_t ch;            /**< 通道索引，0 基（0..COUNT-1）；与事件载荷和
                                镜像/KV 存储同基。 */
} channel_ao_t;

/**
 * @brief 构造一个通道活动对象。
 *
 * @param me 调用者拥有的存储。
 * @param ch 通道索引，0 基（0..BOARD_CHANNEL_COUNT-1）。
 * @param input 要接管的板级通道对象；借用，其生命周期必须长于本 AO。
 */
void channel_ao_ctor(channel_ao_t* me,
                     uint8_t ch,
                     analog_input_t* input);

#ifdef __cplusplus
}
#endif

#endif
