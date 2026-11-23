#include "analog_input_board.h"

#include "analog_input_convert.h"

#include "at32f421_wk_config.h"
#include "wk_system.h"

static analog_input_status_t map_status(sig24131_status_t status)
{
    switch ( status ) {
        case SIG24131_OK:
            return ANALOG_INPUT_OK;
        case SIG24131_ERR_ARG:
            return ANALOG_INPUT_ERR_ARG;
        case SIG24131_ERR_IO:
            return ANALOG_INPUT_ERR_IO;
        case SIG24131_ERR_VERIFY:
            return ANALOG_INPUT_ERR_IO;
    }
    return ANALOG_INPUT_ERR_IO;
}

/** @brief 一个模拟通道的 CV/COM 开关引脚。 */
typedef struct {
    gpio_type* cv_port;
    uint16_t cv_pin;
    gpio_type* com_port;
    uint16_t com_pin;
} front_end_switch_t;

/* 可选通道随 SIG24131_BOARD_INSTANCE_COUNT 一同编译剔除。 */
static const front_end_switch_t switches[SIG24131_BOARD_INSTANCE_COUNT] = {
    {CTRL_CV_1_GPIO_PORT, CTRL_CV_1_PIN, CTRL_COM_1_GPIO_PORT, CTRL_COM_1_PIN},
    {CTRL_CV_2_GPIO_PORT, CTRL_CV_2_PIN, CTRL_COM_2_GPIO_PORT, CTRL_COM_2_PIN},
    {CTRL_CV_3_GPIO_PORT, CTRL_CV_3_PIN, CTRL_COM_3_GPIO_PORT, CTRL_COM_3_PIN},
    {CTRL_CV_4_GPIO_PORT, CTRL_CV_4_PIN, CTRL_COM_4_GPIO_PORT, CTRL_COM_4_PIN},
#if SIG24131_BOARD_INSTANCE_COUNT >= 5U
    {CTRL_CV_5_GPIO_PORT, CTRL_CV_5_PIN, CTRL_COM_5_GPIO_PORT, CTRL_COM_5_PIN},
#endif
#if SIG24131_BOARD_INSTANCE_COUNT >= 6U
    {CTRL_CV_6_GPIO_PORT, CTRL_CV_6_PIN, CTRL_COM_6_GPIO_PORT, CTRL_COM_6_PIN},
#endif
#if SIG24131_BOARD_INSTANCE_COUNT >= 7U
    {CTRL_CV_7_GPIO_PORT, CTRL_CV_7_PIN, CTRL_COM_7_GPIO_PORT, CTRL_COM_7_PIN},
#endif
#if SIG24131_BOARD_INSTANCE_COUNT >= 8U
    {CTRL_CV_8_GPIO_PORT, CTRL_CV_8_PIN, CTRL_COM_8_GPIO_PORT, CTRL_COM_8_PIN},
#endif
};
static void write_switch(gpio_type* port,
                         uint16_t pin,
                         uint8_t level)
{
    if ( level != 0U ) {
        gpio_bits_set(port, pin);
    } else {
        gpio_bits_reset(port, pin);
    }
}

static void set_front_end(const analog_input_board_t* instance,
                          analog_input_type_t type)
{
    const front_end_switch_t* entry;
    uint8_t com;
    uint8_t cv;

    entry = &switches[instance->channel];
    com   = (uint8_t)((type == ANALOG_INPUT_TYPE_PT100) ? 0U : 1U);
    cv    = (uint8_t)((type == ANALOG_INPUT_TYPE_4_20MA) ? 1U : 0U);

    write_switch(entry->com_port, entry->com_pin, com);
    write_switch(entry->cv_port, entry->cv_pin, cv);
}

static void fill_config(sig24131_config_t* adc,
                        analog_input_type_t type)
{
    sig24131_default_config(adc);

    switch ( type ) {
        case ANALOG_INPUT_TYPE_THERMOCOUPLE:

            adc->vbias_enable              = 1U;
            adc->reference                 = SIG24131_REF_INTERNAL_2V5;
            adc->internal_reference_enable = 1U;
            adc->gain                      = ANALOG_INPUT_BOARD_THERMOCOUPLE_GAIN;
            adc->mux_p                     = ANALOG_INPUT_BOARD_SENSOR_MUX_P;
            adc->mux_n                     = ANALOG_INPUT_BOARD_SENSOR_MUX_N;
            break;

        case ANALOG_INPUT_TYPE_PT100:

            adc->reference        = SIG24131_REF_EXTERNAL;
            adc->gain             = ANALOG_INPUT_BOARD_RTD_GAIN;
            adc->mux_p            = ANALOG_INPUT_BOARD_SENSOR_MUX_P;
            adc->mux_n            = ANALOG_INPUT_BOARD_SENSOR_MUX_N;
            adc->idac_current     = ANALOG_INPUT_BOARD_RTD_IDAC_CURRENT;
            adc->idac1_mux        = ANALOG_INPUT_BOARD_RTD_IDAC1_MUX;
            adc->idac2_mux        = ANALOG_INPUT_BOARD_RTD_IDAC2_MUX;
            adc->idac_pair_enable = 1U;
            break;

        case ANALOG_INPUT_TYPE_4_20MA:
            adc->reference                 = SIG24131_REF_INTERNAL_2V5;
            adc->internal_reference_enable = 1U;
            adc->gain                      = ANALOG_INPUT_BOARD_CURRENT_GAIN;
            adc->mux_p                     = ANALOG_INPUT_BOARD_LINEAR_MUX_P;
            adc->mux_n                     = ANALOG_INPUT_BOARD_LINEAR_MUX_N;
            break;

        case ANALOG_INPUT_TYPE_0_10V:
            adc->reference                 = SIG24131_REF_INTERNAL_2V5;
            adc->internal_reference_enable = 1U;
            adc->gain                      = ANALOG_INPUT_BOARD_VOLTAGE_GAIN;
            adc->mux_p                     = ANALOG_INPUT_BOARD_LINEAR_MUX_P;
            adc->mux_n                     = ANALOG_INPUT_BOARD_LINEAR_MUX_N;
            break;
    }
}

static void fill_break_config(sig24131_config_t* adc,
                              analog_input_type_t type)
{
    fill_config(adc, type);

    switch ( type ) {
        case ANALOG_INPUT_TYPE_THERMOCOUPLE:
            adc->gain                = SIG24131_GAIN_1;
            adc->input_buffer_enable = 1U;
            adc->burnout_enable      = 1U;
            break;

        case ANALOG_INPUT_TYPE_PT100:
            adc->reference = SIG24131_REF_INTERNAL_2V5;
            /* REF_EN 必须保持关闭：否则它会驱动 REFP 而不是测量 REFP。 */
            adc->internal_reference_enable = 0U;
            adc->gain                      = SIG24131_GAIN_1;
            adc->input_buffer_enable       = 1U;
            adc->mux_p                     = SIG24131_MUX_REF;
            adc->mux_n                     = SIG24131_MUX_REF;
            break;

        case ANALOG_INPUT_TYPE_4_20MA:
        case ANALOG_INPUT_TYPE_0_10V:

            break;
    }
}

static uint8_t is_configured(const analog_input_board_t* instance)
{
    /* THERMOCOUPLE + NONE 是构造函数留下的无效哨兵值。 */
    if ( instance->base.config.type != ANALOG_INPUT_TYPE_THERMOCOUPLE ) {
        return 1U;
    }
    return (instance->base.config.thermocouple_type
            != ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE)
               ? 1U
               : 0U;
}

static void mark_unconfigured(analog_input_t* self)
{
    self->config.type                = ANALOG_INPUT_TYPE_THERMOCOUPLE;
    self->config.thermocouple_type   = ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE;
    self->config.sample_rate_millihz = 0U;
}

static uint8_t has_break_detection(analog_input_type_t type)
{
    return (type == ANALOG_INPUT_TYPE_THERMOCOUPLE
            || type == ANALOG_INPUT_TYPE_PT100)
               ? 1U
               : 0U;
}

static analog_input_status_t configure(analog_input_t* self,
                                       const analog_input_config_t* config,
                                       uint32_t* first_conversion_delay_ms)
{
    analog_input_board_t* instance;
    sig24131_config_t adc;
    sig24131_data_rate_t data_rate;
    analog_input_status_t status;
    uint32_t rate_millihz;

    instance = (analog_input_board_t*)self;

    rate_millihz = (config->sample_rate_millihz == 0U)
                       ? ANALOG_INPUT_BOARD_DEFAULT_SAMPLE_RATE_MILLIHZ
                       : config->sample_rate_millihz;

    /* 本板规格上限为 100 Hz，尽管 SIG24131 可以更高。 */
    if ( rate_millihz > ANALOG_INPUT_BOARD_MAX_SAMPLE_RATE_MILLIHZ ) {
        return ANALOG_INPUT_ERR_UNSUPPORTED;
    }

    if ( sig24131_data_rate_from_millihz(rate_millihz, &data_rate)
         != SIG24131_OK ) {
        return ANALOG_INPUT_ERR_UNSUPPORTED;
    }

    fill_config(&adc, config->type);
    adc.data_rate = data_rate;

    /* 先切换开关：写 CONF1 会重新启动转换。 */
    set_front_end(instance, config->type);

    status = map_status(sig24131_write_config(instance->device, &adc));
    if ( status != ANALOG_INPUT_OK ) {
        /* 器件可能只持有部分配置；丢弃已缓存的配置。 */
        mark_unconfigured(self);
        return status;
    }

    *first_conversion_delay_ms = analog_input_convert_settle_delay_ms(rate_millihz);

    self->config                     = *config;
    self->config.sample_rate_millihz = rate_millihz;
    return ANALOG_INPUT_OK;
}

static analog_input_status_t read_sample(analog_input_t* self,
                                         int32_t* sample_code)
{
    analog_input_board_t* instance;
    analog_input_status_t status;
    int32_t code;

    instance = (analog_input_board_t*)self;
    if ( is_configured(instance) == 0U ) {
        return ANALOG_INPUT_ERR_STATE;
    }

    status = map_status(sig24131_read_data(instance->device, &code));
    if ( status != ANALOG_INPUT_OK ) {
        return status;
    }
    *sample_code = code;
    return ANALOG_INPUT_OK;
}

static analog_input_status_t convert(analog_input_t* self,
                                     int32_t sample_code,
                                     const analog_input_conversion_context_t* context,
                                     float* measurement)
{
    analog_input_board_t* instance;

    instance = (analog_input_board_t*)self;
    if ( is_configured(instance) == 0U ) {
        return ANALOG_INPUT_ERR_STATE;
    }

    return analog_input_convert_sample(self->config.type,
                                       self->config.thermocouple_type,
                                       sample_code, context, measurement);
}

static analog_input_status_t enable_break_detection(analog_input_t* self,
                                                    uint32_t* first_check_delay_ms)
{
    analog_input_board_t* instance;
    sig24131_config_t adc;
    sig24131_data_rate_t data_rate;
    analog_input_status_t status;

    instance = (analog_input_board_t*)self;
    if ( is_configured(instance) == 0U ) {
        return ANALOG_INPUT_ERR_STATE;
    }
    if ( has_break_detection(self->config.type) == 0U ) {
        return ANALOG_INPUT_ERR_UNSUPPORTED;
    }

    if ( sig24131_data_rate_from_millihz(self->config.sample_rate_millihz,
                                         &data_rate)
         != SIG24131_OK ) {
        return ANALOG_INPUT_ERR_STATE;
    }

    fill_break_config(&adc, self->config.type);
    adc.data_rate = data_rate;

    status = map_status(sig24131_write_config(instance->device, &adc));
    if ( status != ANALOG_INPUT_OK ) {

        mark_unconfigured(self);
        return status;
    }

    *first_check_delay_ms = analog_input_convert_settle_delay_ms(
        self->config.sample_rate_millihz);
    return ANALOG_INPUT_OK;
}

static analog_input_status_t disable_break_detection(analog_input_t* self,
                                                     uint32_t* first_conversion_delay_ms)
{
    analog_input_board_t* instance;
    sig24131_config_t adc;
    sig24131_data_rate_t data_rate;
    analog_input_status_t status;

    instance = (analog_input_board_t*)self;
    if ( is_configured(instance) == 0U ) {
        return ANALOG_INPUT_ERR_STATE;
    }
    if ( sig24131_data_rate_from_millihz(self->config.sample_rate_millihz,
                                         &data_rate)
         != SIG24131_OK ) {
        return ANALOG_INPUT_ERR_STATE;
    }

    fill_config(&adc, self->config.type);
    adc.data_rate = data_rate;

    status = map_status(sig24131_write_config(instance->device, &adc));
    if ( status != ANALOG_INPUT_OK ) {
        mark_unconfigured(self);
        return status;
    }

    *first_conversion_delay_ms = analog_input_convert_settle_delay_ms(
        self->config.sample_rate_millihz);
    return ANALOG_INPUT_OK;
}

static analog_input_status_t check_wire_break(analog_input_t* self,
                                              analog_input_wire_status_t* wire_status)
{
    int32_t sample_code;
    analog_input_status_t status;

    if ( has_break_detection(self->config.type) == 0U ) {
        return ANALOG_INPUT_ERR_UNSUPPORTED;
    }

    status = read_sample(self, &sample_code);
    if ( status != ANALOG_INPUT_OK ) {
        return status;
    }

    /* 同一数据寄存器；阈值取决于当前生效的检测模式。 */
    if ( self->config.type == ANALOG_INPUT_TYPE_PT100 ) {
        *wire_status = analog_input_convert_rtd_wire(sample_code);
    } else {
        *wire_status = analog_input_convert_thermocouple_wire(sample_code);
    }
    return ANALOG_INPUT_OK;
}

static const analog_input_ops_t ops = {
    .configure               = configure,
    .read_sample             = read_sample,
    .convert                 = convert,
    .enable_break_detection  = enable_break_detection,
    .disable_break_detection = disable_break_detection,
    .check_wire_break        = check_wire_break,
};

static analog_input_board_t instances[SIG24131_BOARD_INSTANCE_COUNT];
analog_input_status_t analog_input_board_ctor(uint8_t channel)
{
    analog_input_board_t* instance;
    analog_input_status_t status;

    if ( channel >= SIG24131_BOARD_INSTANCE_COUNT ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    instance = &instances[channel];

    status = analog_input_ctor(&instance->base, &ops);
    if ( status != ANALOG_INPUT_OK ) {
        return status;
    }
    instance->device  = sig24131_board_get_device(channel);
    instance->channel = channel;
    return ANALOG_INPUT_OK;
}

analog_input_status_t analog_input_board_ctor_all(void)
{
    analog_input_status_t status;
    uint8_t channel;

    for ( channel = 0U; channel < SIG24131_BOARD_INSTANCE_COUNT; ++channel ) {
        status = analog_input_board_ctor(channel);
        if ( status != ANALOG_INPUT_OK ) {
            return status;
        }
    }
    return ANALOG_INPUT_OK;
}

analog_input_t* analog_input_board_get(uint8_t channel)
{
    if ( channel >= SIG24131_BOARD_INSTANCE_COUNT ) {
        return NULL;
    }
    return &instances[channel].base;
}
