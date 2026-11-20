/**
 * @file analog_input_fake.c
 * @brief 基于六槽 ops vtable 的模拟量输入假件。
 *
 * 调用日志是 AO 测试的轨迹证据：检测模式和测量模式的进入与退出以 enable/check/
 * disable 记录的形式可见，每条记录都盖有虚拟节拍和生效采样率，因此截止时间与
 * 过期代次的违规无需任何跟踪设施即可断言。
 */

#include "analog_input_fake.h"

#include "host_support.h"

/** @brief 追加一条记录；日志按场景确定大小，溢出即为缺陷。 */
static void record(analog_input_fake_t* f,
                   uint8_t op)
{
    analog_input_fake_call_t* c = &f->log[f->log_len];

    c->op   = op;
    c->tick = g_test_tick;
    c->rate = f->base.config.sample_rate_millihz;
    ++f->log_len;
}

static analog_input_status_t fake_configure(analog_input_t* self,
                                            const analog_input_config_t* config,
                                            uint32_t* first_conversion_delay_ms)
{
    analog_input_fake_t* f = (analog_input_fake_t*)self;

    /* 实现方拥有缓存的配置。 */
    f->base.config = *config;
    record(f, (uint8_t)ANALOG_INPUT_FAKE_CONFIGURE);
    *first_conversion_delay_ms = f->configure_delay_ms;
    return f->configure_status;
}

static analog_input_status_t fake_read_sample(analog_input_t* self,
                                              int32_t* sample_code)
{
    analog_input_fake_t* f = (analog_input_fake_t*)self;

    record(f, (uint8_t)ANALOG_INPUT_FAKE_READ_SAMPLE);
    *sample_code = f->sample_code;
    return f->read_status;
}

static analog_input_status_t fake_convert(analog_input_t* self,
                                          int32_t sample_code,
                                          const analog_input_conversion_context_t* context,
                                          float* measurement)
{
    analog_input_fake_t* f = (analog_input_fake_t*)self;
    float value;

    (void)sample_code;
    record(f, (uint8_t)ANALOG_INPUT_FAKE_CONVERT);
    value = f->convert_raw;
    if ( context != NULL ) {
        value = (value * context->gain) + context->offset;
    }
    *measurement = value;
    return f->convert_status;
}

static analog_input_status_t fake_enable_break_detection(analog_input_t* self,
                                                         uint32_t* first_check_delay_ms)
{
    analog_input_fake_t* f = (analog_input_fake_t*)self;

    record(f, (uint8_t)ANALOG_INPUT_FAKE_ENABLE_BREAK);
    *first_check_delay_ms = f->enable_delay_ms;
    return f->enable_status;
}

static analog_input_status_t fake_check_wire_break(analog_input_t* self,
                                                   analog_input_wire_status_t* wire_status)
{
    analog_input_fake_t* f = (analog_input_fake_t*)self;

    record(f, (uint8_t)ANALOG_INPUT_FAKE_CHECK_WIRE);
    *wire_status = f->check_wire;
    return f->check_status;
}

static analog_input_status_t fake_disable_break_detection(analog_input_t* self,
                                                          uint32_t* first_conversion_delay_ms)
{
    analog_input_fake_t* f = (analog_input_fake_t*)self;

    record(f, (uint8_t)ANALOG_INPUT_FAKE_DISABLE_BREAK);
    *first_conversion_delay_ms = f->disable_delay_ms;
    return f->disable_status;
}

/** @brief 每个假件实例都安装的那一张操作表。 */
static const analog_input_ops_t ops = {
    &fake_configure,
    &fake_read_sample,
    &fake_convert,
    &fake_enable_break_detection,
    &fake_disable_break_detection,
    &fake_check_wire_break};

void analog_input_fake_init(analog_input_fake_t* f)
{
    (void)analog_input_ctor(&f->base, &ops);

    f->log_len            = 0U;
    f->configure_status   = ANALOG_INPUT_OK;
    f->configure_delay_ms = 200U;
    f->read_status        = ANALOG_INPUT_OK;
    f->sample_code        = 1000;
    f->convert_status     = ANALOG_INPUT_OK;
    f->convert_raw        = 25.0f;
    f->enable_status      = ANALOG_INPUT_OK;
    f->enable_delay_ms    = 100U;
    f->check_status       = ANALOG_INPUT_OK;
    f->check_wire         = ANALOG_INPUT_WIRE_NORMAL;
    f->disable_status     = ANALOG_INPUT_OK;
    f->disable_delay_ms   = 200U;
}

void analog_input_fake_reset_log(analog_input_fake_t* f)
{
    f->log_len = 0U;
}

uint16_t analog_input_fake_count(const analog_input_fake_t* f,
                                 uint8_t op)
{
    uint16_t n = 0U;
    uint16_t i;

    for ( i = 0U; i < f->log_len; ++i ) {
        if ( f->log[i].op == op ) {
            ++n;
        }
    }
    return n;
}

const analog_input_fake_call_t* analog_input_fake_at(const analog_input_fake_t* f,
                                                     uint16_t index)
{
    if ( index >= f->log_len ) {
        return NULL;
    }
    return &f->log[index];
}
