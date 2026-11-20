#ifndef ANALOG_INPUT_FAKE_H_
#define ANALOG_INPUT_FAKE_H_

#include <stdint.h>

#include "analog_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 记录的操作种类，每个 vtable 槽对应一个枚举值。 */
typedef enum {
    ANALOG_INPUT_FAKE_CONFIGURE = 0,
    ANALOG_INPUT_FAKE_READ_SAMPLE,
    ANALOG_INPUT_FAKE_CONVERT,
    ANALOG_INPUT_FAKE_ENABLE_BREAK,
    ANALOG_INPUT_FAKE_CHECK_WIRE,
    ANALOG_INPUT_FAKE_DISABLE_BREAK,
    ANALOG_INPUT_FAKE_OP_COUNT
} analog_input_fake_op_t;

/** @brief 一个场景中被记录调用的上限。 */
#define ANALOG_INPUT_FAKE_LOG_MAX 64U

/** @brief 一条盖有虚拟节拍和采样率的调用记录。 */
typedef struct {
    uint8_t op;    /**< 该调用的 analog_input_fake_op_t。 */
    uint32_t tick; /**< 调用发生时的 g_test_tick。 */
    uint32_t rate; /**< 调用时生效配置的采样率（mHz）。 */
} analog_input_fake_call_t;

/** @brief 模拟量输入假件：结果可编排，记录每一次调用。 */
typedef struct {
    analog_input_t base; /**< 接口基对象；必须位于首位。 */
    analog_input_fake_call_t log[ANALOG_INPUT_FAKE_LOG_MAX];
    uint16_t log_len;
    /* 脚本 */
    analog_input_status_t configure_status;
    uint32_t configure_delay_ms;
    analog_input_status_t read_status;
    int32_t sample_code;
    analog_input_status_t convert_status;
    float convert_raw;
    analog_input_status_t enable_status;
    uint32_t enable_delay_ms;
    analog_input_status_t check_status;
    analog_input_wire_status_t check_wire;
    analog_input_status_t disable_status;
    uint32_t disable_delay_ms;
} analog_input_fake_t;

/**
 * @brief 构造假件并安装操作表。
 *
 * 默认值模拟一条完好的通道：每个操作都成功，configure 报告 200 ms 稳定时间
 *（10 Hz 下的两次转换），check 报告回路闭合，首个采样解码为 25.0。
 */
void analog_input_fake_init(analog_input_fake_t* f);

/** @brief 清空调用日志和脚本计数器；保持对象仍然有效。 */
void analog_input_fake_reset_log(analog_input_fake_t* f);

/** @brief 统计某一操作种类的已记录调用数。 */
uint16_t analog_input_fake_count(const analog_input_fake_t* f,
                                 uint8_t op);

/** @brief 按索引访问调用日志；越界时返回 NULL。 */
const analog_input_fake_call_t* analog_input_fake_at(const analog_input_fake_t* f,
                                                     uint16_t index);

#ifdef __cplusplus
}
#endif

#endif
