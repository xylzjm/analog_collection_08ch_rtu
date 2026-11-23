#ifndef SIG24131_BOARD_H
#define SIG24131_BOARD_H

#include <stdint.h>

#include "sig24131.h"

#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 已装配的 SIG24131 器件数量（每个通道位置一个）。 */
#define SIG24131_BOARD_INSTANCE_COUNT BOARD_CHANNEL_COUNT

/**
 * @brief 构造某个通道的驱动实例。
 *
 * @param channel 从 0 开始的通道号。
 * @return SIG24131_OK；通道无效时返回 SIG24131_ERR_ARG。
 * @note SPI1 与片选 GPIO 必须已初始化。
 */
sig24131_status_t sig24131_board_ctor(uint8_t channel);

/** @brief 构造每个已装配通道的驱动实例。 */
sig24131_status_t sig24131_board_ctor_all(void);

/**
 * @brief 复位单个器件，不等待其稳定。
 *
 * @param channel 从 0 开始的通道号。
 * @return SIG24131_OK；通道无效时返回 SIG24131_ERR_ARG。
 * @note 下一次寄存器访问前需等待 SIG24131_RESET_DELAY_MS。
 */
sig24131_status_t sig24131_board_reset(uint8_t channel);

/** @brief 复位所有器件并统一等待一次其稳定。 */
sig24131_status_t sig24131_board_reset_all(void);

/** @brief 返回某个通道的驱动对象。 */
sig24131_t* sig24131_board_get_device(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif
