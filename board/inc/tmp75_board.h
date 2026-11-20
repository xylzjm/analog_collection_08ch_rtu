#ifndef TMP75_BOARD_H
#define TMP75_BOARD_H

#include <stdint.h>

#include "tmp75.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 唯一 TMP75 的 7 位从站地址；A2..A0 接地。 */
#define TMP75_BOARD_ADDRESS TMP75_ADDRESS_0 /* 0x48 */

/**
 * @brief 构建板级 TMP75 驱动对象。
 *
 * @return TMP75_OK，或失败的构造函数状态。
 * @note 构造不执行事务，也不要求总线就绪；首次事务前
 *       i2c_soft_board_init() 必须已成功。共享总线上的事务仍由调用方
 *       负责串行化。
 */
tmp75_status_t tmp75_board_ctor(void);

/** @brief 返回板级 TMP75 驱动对象。 */
tmp75_t* tmp75_board_get_device(void);

#ifdef __cplusplus
}
#endif

#endif
