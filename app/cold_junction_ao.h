#ifndef COLD_JUNCTION_AO_H
#define COLD_JUNCTION_AO_H

/**
 * @file cold_junction_ao.h
 * @brief 冷端温度活动对象（TMP75）。
 *
 * TMP75 与共享软 I2C 总线的唯一所有者；每秒发布一次 CJC_SIG 心跳。参见
 * docs/ao/cold_junction_ao.md。
 */

#include "qpc.h"

#include "tmp75.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 队列深度（1 Hz 节拍加裕量）。 */
#define COLD_JUNCTION_AO_QUEUE_LEN 4U

/** @brief 冷端温度合理下限。 */
#define COLD_JUNCTION_AO_MIN_TEMP_C (-30.0f)

/** @brief 冷端温度合理上限。 */
#define COLD_JUNCTION_AO_MAX_TEMP_C 85.0f

/** @brief 使读数失效的连续失败次数。 */
#define COLD_JUNCTION_AO_ERR_LIMIT 3U

/** @brief 冷端心跳周期，单位 tick。 */
#define COLD_JUNCTION_AO_TICK_MS 1000U

/** @brief AoColdJunction 活动对象。 */
typedef struct {
    QActive super;        /**< 活动对象基类。 */
    QTimeEvt te_tick;     /**< 1 Hz 心跳定时器。 */
    tmp75_t* dev;         /**< TMP75 驱动对象；借用，其生命周期必须长于本 AO。 */
    float temp_c;         /**< 最近一次有效温度。 */
    uint8_t err_cnt;      /**< 连续读取失败次数，饱和计数。 */
    uint8_t have_reading; /**< 一旦有读取成功即非零。 */
} cold_junction_ao_t;

/**
 * @brief 构造冷端活动对象。
 *
 * @param me 调用者拥有的存储。
 * @param dev TMP75 驱动对象；借用，其生命周期必须长于本 AO。
 */
void cold_junction_ao_ctor(cold_junction_ao_t* me,
                           tmp75_t* dev);

#ifdef __cplusplus
}
#endif

#endif
