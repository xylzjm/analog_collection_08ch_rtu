#ifndef MODBUS_SLAVE_BOARD_H
#define MODBUS_SLAVE_BOARD_H

#include "modbus_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 从站构造配置中的平台部分（端口、缓冲区、波特率）。
 *        回调与身份字段保持清零，由 Modbus AO 填充。
 * @return 指向文件内静态 const 配置的指针。
 */
const modbus_slave_config_t* modbus_slave_board_config(void);

/** @brief 返回板级 Modbus 从站实例。 */
modbus_slave_t* modbus_slave_board_get(void);

#ifdef __cplusplus
}
#endif

#endif
