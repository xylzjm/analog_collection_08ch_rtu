#ifndef MODBUS_AO_H
#define MODBUS_AO_H

/**
 * @file modbus_ao.h
 * @brief Modbus RTU 从站活动对象：路由、镜像与持久化。
 *
 * AoModbus 拥有数据空间的类型化镜像、保持寄存器写事务（校验、带回读校验
 * 的同步固化，然后更新镜像）以及 FlashDB 参数数据库中的参数持久化。
 * reg_map 是它调用的纯线上映射。参见 docs/ao/modbus_ao.md。
 */

#include "app.h"
#include "modbus_slave.h"
#include "qpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 队列深度（一轮遥测加一帧加裕量）。 */
#define MODBUS_AO_QUEUE_LEN 16U

/** @brief 配置区镜像：已固化配置与地址（变化检查基准；只在 AoModbus 的
 *  RTC 内写入）。 */
typedef struct {
    ch_config_t cfg[CH_CONFIG_CHANNEL_COUNT]; /**< 每通道最近一次已固化配置。 */
    uint8_t cfg_ok[CH_CONFIG_CHANNEL_COUNT];  /**< cfg[ch] 已固化且可用。 */
    uint8_t applied[CH_CONFIG_CHANNEL_COUNT]; /**< 通道已接受 cfg[ch]（CFG_APPLIED_SIG）。 */
    uint16_t addr;                            /**< 生效从站地址。 */
    uint8_t addr_ok;                          /**< addr 已固化且可用。 */
} modbus_ao_config_image_t;

/** @brief 遥测区镜像：馈送输入寄存器的最近遥测。 */
typedef struct {
    float value[CH_CONFIG_CHANNEL_COUNT];     /**< 最近测量值。 */
    uint16_t status[CH_CONFIG_CHANNEL_COUNT]; /**< 通道状态码（ch_status_t）。 */
    float cjc_temp;                           /**< 最近冷端温度。 */
    uint8_t cjc_valid;                        /**< 故障字 b0 来源。 */
} modbus_ao_telemetry_image_t;

/** @brief 参数持久化结果状态。 */
typedef struct {
    uint8_t db_ok;         /**< 参数数据库已打开；故障字 b3 = !db_ok。 */
    uint8_t commit_failed; /**< 锁存：最近一次固化失败（故障字 b1）。 */
} modbus_ao_store_t;

/** @brief 运行期诊断（队列水位 / TX 计数）。 */
typedef struct {
    uint16_t tx_done_cnt;    /**< 已发送应答数。 */
    uint16_t tx_timeout_cnt; /**< TX 看门狗事件数。 */
    uint16_t hb_ctr;         /**< 自上次水位上报以来的 CJC 心跳数。 */
} modbus_ao_diag_t;

/** @brief AoModbus 活动对象：协议路由、镜像与持久化。 */
typedef struct {
    QActive super;                         /**< 活动对象基类。 */
    modbus_slave_t* slave;                 /**< 从站核心；在构造函数中构建。 */
    modbus_ao_config_image_t config;       /**< 配置区镜像。 */
    modbus_ao_telemetry_image_t telemetry; /**< 遥测区镜像。 */
    modbus_ao_store_t store;               /**< 持久化结果状态。 */
    modbus_ao_diag_t diag;                 /**< 诊断。 */
} modbus_ao_t;

/**
 * @brief 一步构造 Modbus 活动对象及其从站核心。
 *
 * 参数数据库在本次调用内打开（在 QActive_start() 与 QF_run() 之前）；
 * 打开失败会使 `store.db_ok` = 0（故障 b3），此后每次固化都安全失败。
 *
 * @param me 调用者拥有的存储。
 * @param slave 调用者拥有的从站存储（板级静态实例）；必需。
 * @param slave_cfg modbus_slave_config_t 的平台半边：线路/定时器/临界区
 *        端口、rx/tx 缓冲、波特率；必需。回调与身份字段由本模块覆盖，
 *        其入参值被忽略。
 */
void modbus_ao_ctor(modbus_ao_t* me,
                    modbus_slave_t* slave,
                    const modbus_slave_config_t* slave_cfg);

#ifdef __cplusplus
}
#endif

#endif
