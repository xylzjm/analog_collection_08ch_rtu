#ifndef TMP75_PORT_FAKE_H_
#define TMP75_PORT_FAKE_H_

#include <stddef.h>
#include <stdint.h>

#include "tmp75.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 真实 TMP75 驱动背后的可编排 I2C 传输。 */
typedef struct {
    tmp75_status_t status; /**< 要返回的传输结果；TMP75_OK 时交出 rx_frame。 */
    uint16_t calls;        /**< 已服务的传输次数。 */
    uint8_t rx_frame[2];   /**< 请求读取时返回的字节。 */
} tmp75_port_fake_t;

/** @brief 默认脚本：传输成功且读到 25.0 degC。 */
void tmp75_port_fake_init(tmp75_port_fake_t* f);

/**
 * @brief 编排后续的温度读取。
 *
 * @param f 要编程的假件。
 * @param temp_c 传输读取时交出的温度。
 */
void tmp75_port_fake_set_temperature(tmp75_port_fake_t* f,
                                     float temp_c);

/** @brief 编排后续每一次传输都以 TMP75_ERR_IO 失败。 */
void tmp75_port_fake_fail(tmp75_port_fake_t* f);

/** @brief 填充一个绑定到本假件的端口结构。 */
void tmp75_port_fake_port(tmp75_port_fake_t* f,
                          tmp75_port_t* out);

#ifdef __cplusplus
}
#endif

#endif
