#ifndef MODBUS_PORT_FAKE_H_
#define MODBUS_PORT_FAKE_H_

#include <stdint.h>

#include "modbus_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 假线路记录的最大应答（标识类功能码可能更长）。 */
#define MODBUS_PORT_FAKE_TX_MAX 280U

/** @brief 假 RS485 线路、单次定时器与可嵌套临界区。 */
typedef struct {
    /* 线路 */
    uint8_t rx_enabled;                        /**< 上一次 direction() 的 rx 实参。 */
    uint8_t tx_enabled;                        /**< 上一次 direction() 的 tx 实参。 */
    uint16_t tx_len;                           /**< 经 put_byte() 推入的字节数。 */
    uint8_t tx_bytes[MODBUS_PORT_FAKE_TX_MAX]; /**< 应答捕获。 */
    /* 定时器：只做功能模拟，不记录任何内容 */
    /* 临界区 */
    uint16_t enter_calls; /**< enter() 调用次数。 */
    uint16_t exit_calls;  /**< exit() 调用次数。 */
    int nest;             /**< 嵌套计数；任何临界区之外为 0。 */
} modbus_port_fake_t;

/** @brief 清空脚本和捕获。 */
void modbus_port_fake_init(modbus_port_fake_t* f);

/** @brief 把假线路回调接到 @p out。 */
void modbus_port_fake_line(modbus_port_fake_t* f,
                           modbus_slave_port_line_t* out);

/** @brief 把假定时器回调接到 @p out。 */
void modbus_port_fake_timer(modbus_port_fake_t* f,
                            modbus_slave_port_timer_t* out);

/** @brief 把假的可嵌套临界区接到 @p out。 */
void modbus_port_fake_critical(modbus_port_fake_t* f,
                               modbus_slave_port_critical_t* out);

/** @brief 用本假件填充从站配置的平台部分（波特率取自 BOARD_MODBUS_BAUDRATE，
 *  使用给定的 rx/tx 缓冲区，以及假件的端口三件套）。 */
void modbus_port_fake_config(modbus_port_fake_t* f,
                             modbus_slave_config_t* cfg,
                             uint8_t* rx, uint16_t rx_cap,
                             uint8_t* tx, uint16_t tx_cap);

#ifdef __cplusplus
}
#endif

#endif
