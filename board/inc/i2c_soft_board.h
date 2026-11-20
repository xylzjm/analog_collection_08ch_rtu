#ifndef I2C_SOFT_BOARD_H
#define I2C_SOFT_BOARD_H

#include <stdint.h>

#include "i2c_soft.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 构建板级软件模拟（bit-bang）I2C 总线并检查两条信号线。
 *
 * @return I2C_SOFT_OK，或失败的构造函数/总线检查状态。
 * @note 要求 wk_gpio_config()、wk_timebase_init() 与板级临界区已就绪。
 *       仅在任务上下文调用，绝不可在 ISR 中调用：该单实例非线程安全，
 *       调用方需串行化每次事务。首次事务前 i2c_soft_init() 必须成功。
 */
i2c_soft_status_t i2c_soft_board_init(void);

/** @brief 返回板级软件 I2C 总线实例。 */
i2c_soft_t* i2c_soft_board_get(void);

#ifdef __cplusplus
}
#endif

#endif
