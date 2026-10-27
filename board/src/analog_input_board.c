#include "analog_input_board.h"

#include "sig24131_board.h"
#include "wk_system.h"

/* Status mapping ---------------------------------------------------------- */

/**
 * @brief Translate a driver status into the interface vocabulary.
 *
 * SIG24131_ERR_VERIFY reports a device that did not store what was written to
 * it. The bus itself worked, but from the application's point of view the
 * hardware failed, so the result is ANALOG_INPUT_ERR_IO and not a state error.
 */
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

/* Settle delay ------------------------------------------------------------ */

/**
 * @brief Time from the configuration write to the first usable conversion.
 *
 * The conversion restarts when the registers are written, and the output needs
 * ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS periods before it is valid. One period
 * is 1000 / f milliseconds, so the delay in milliseconds is
 * SETTLE_CONVERSIONS * 1000000 / rate_millihz. The interface wants that rounded
 * up to a whole millisecond, hence the ceiling division.
 */
static uint32_t settle_delay_ms(uint32_t rate_millihz)
{
    return ((uint32_t)ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS * 1000000U
            + rate_millihz - 1U)
           / rate_millihz;
}

/* Front-end switches ------------------------------------------------------ */

typedef struct {
    gpio_type* cv_port;
    uint16_t cv_pin;
    gpio_type* com_port;
    uint16_t com_pin;
} front_end_switch_t;

/* One entry per populated channel; optional channels are compiled out so a
 * four-device board only needs to define CTRL_CV_1..4 and CTRL_COM_1..4. */
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

/**
 * @brief Put CV and COM into the levels the input type needs.
 *
 * COM is low only for the three-wire PT100 and high for the two single-ended
 * inputs; CV is high only when the burden resistor has to be in circuit. The
 * thermocouple row uses the preferred level of the don't-care cells.
 */
static void set_front_end(const analog_input_board_t* instance,
                          analog_input_type_t type)
{
    const front_end_switch_t* entry;
    uint8_t com;
    uint8_t cv;

    entry = &switches[instance->channel - 1U];
    com   = (uint8_t)((type == ANALOG_INPUT_TYPE_PT100) ? 0U : 1U);
    cv    = (uint8_t)((type == ANALOG_INPUT_TYPE_4_20MA) ? 1U : 0U);

    write_switch(entry->com_port, entry->com_pin, com);
    write_switch(entry->cv_port, entry->cv_pin, cv);
}

/* Device configuration ---------------------------------------------------- */

/**
 * @brief Build the SIG24131 configuration of one input type.
 *
 * The input buffer is left bypassed: the two single-ended inputs sit close to
 * AVSS, below the buffered input range, and above gain 2 the device enables the
 * buffer by itself anyway.
 */
static void fill_config(sig24131_config_t* adc,
                        analog_input_type_t type)
{
    sig24131_default_config(adc);

    switch ( type ) {
        case ANALOG_INPUT_TYPE_THERMOCOUPLE:
            /* VBIAS puts the input common mode at mid-supply; a thermocouple
             * has no path to a supply rail of its own. */
            adc->vbias_enable              = 1U;
            adc->reference                 = SIG24131_REF_INTERNAL_2V5;
            adc->internal_reference_enable = 1U;
            adc->gain                      = ANALOG_INPUT_BOARD_THERMOCOUPLE_GAIN;
            adc->mux_p                     = ANALOG_INPUT_BOARD_SENSOR_MUX_P;
            adc->mux_n                     = ANALOG_INPUT_BOARD_SENSOR_MUX_N;
            break;

        case ANALOG_INPUT_TYPE_PT100:
            /* Ratiometric: the converter reference is the voltage across the
             * reference resistor, so the internal reference stays off. The two
             * excitation currents have to be paired. */
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

/**
 * @brief Build the SIG24131 configuration of a wire-break check.
 *
 * A check keeps the front end of the measurement: the sensor stays connected to
 * the same pins and only what the converter looks at changes.
 *
 * Both checks run at gain 1, which is the widest range the converter has: the
 * largest thermocouple reaches 3% of it and the intact reference voltage 66%,
 * so neither comes close to the full scale that a fault produces. Both also
 * enable the input buffer, because each one measures a node that must not be
 * loaded, the burnout-driven thermocouple terminals and the reference resistor
 * across REFP/REFN.
 *
 * A thermocouple check adds the burnout current sources, so an open sensor
 * drives its two terminals to opposite rails while a connected one carries the
 * current through its own loop.
 *
 * The PT100 check keeps the excitation currents, because they create the
 * voltage that carries the diagnosis, and measures that voltage at REFP/REFN
 * instead of the sensor. Measuring it needs a reference that is not the
 * measured voltage itself, so the internal 2.5 V reference is selected. REF_EN
 * stays 0: it feeds the internal reference out to REFP and would replace the
 * voltage at the pin with a constant.
 */
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
            adc->reference                 = SIG24131_REF_INTERNAL_2V5;
            adc->internal_reference_enable = 0U;
            adc->gain                      = SIG24131_GAIN_1;
            adc->input_buffer_enable       = 1U;
            adc->mux_p                     = SIG24131_MUX_REF;
            adc->mux_n                     = SIG24131_MUX_REF;
            break;

        case ANALOG_INPUT_TYPE_4_20MA:
        case ANALOG_INPUT_TYPE_0_10V:
            /* Nothing to detect; the caller rejects these types before it gets
             * here. */
            break;
    }
}

/* Object state ------------------------------------------------------------ */

/**
 * @brief Whether analog_input_configure() has succeeded on this object.
 *
 * The constructor leaves the base configuration as a thermocouple without a
 * sensor model, a pair the interface itself rejects, so no configuration that
 * came through the interface can look like that.
 */
static uint8_t is_configured(const analog_input_board_t* instance)
{
    if ( instance->base.config.type != ANALOG_INPUT_TYPE_THERMOCOUPLE ) {
        return 1U;
    }
    return (instance->base.config.thermocouple_type
            != ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE)
               ? 1U
               : 0U;
}

/** Put the object back into the unconfigured state the constructor produces. */
static void mark_unconfigured(analog_input_t* self)
{
    self->config.type                = ANALOG_INPUT_TYPE_THERMOCOUPLE;
    self->config.thermocouple_type   = ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE;
    self->config.sample_rate_millihz = 0U;
}

/* Conversion -------------------------------------------------------------- */

/** Full-scale code of the signed 24-bit conversion result. */
#define FULL_SCALE_CODE 8388608.0f

/** Electrical span of the two linear input types. */
#define CURRENT_SPAN_LOW_MA  4.0f
#define CURRENT_SPAN_HIGH_MA 20.0f
#define VOLTAGE_SPAN_LOW_V   0.0f
#define VOLTAGE_SPAN_HIGH_V  10.0f

/**
 * @brief Voltage at the ADC input of one raw code.
 *
 *     V_adc = code / 2^23 * V_ref / gain
 *
 * Valid for the internally referenced modes. The ratiometric PT100 front end
 * takes its reference from the reference resistor instead.
 */
static float adc_volts(int32_t sample_code,
                       float gain)
{
    return (float)sample_code
           * ANALOG_INPUT_BOARD_INTERNAL_REFERENCE_VOLTS
           / (FULL_SCALE_CODE * gain);
}

/**
 * @brief Convert a raw code of one of the two linear input types.
 *
 * The code becomes the voltage at the input terminals first, which the shunt or
 * the divider turns into milliamperes or volts. That value is then mapped onto
 * the engineering range of the caller. Values outside the span extrapolate
 * instead of clamping, so an over-range loop current stays visible.
 */
static analog_input_status_t convert_linear(analog_input_t* self,
                                            int32_t sample_code,
                                            const analog_input_conversion_context_t* context,
                                            float* measurement)
{
    float gain;
    float span_low;
    float span_high;
    float value;

    if ( context == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }

    if ( self->config.type == ANALOG_INPUT_TYPE_4_20MA ) {
        gain      = (float)(1UL << ANALOG_INPUT_BOARD_CURRENT_GAIN);
        value     = adc_volts(sample_code, gain)
                    * ANALOG_INPUT_BOARD_ATTENUATION_RATIO
                    / ANALOG_INPUT_BOARD_SHUNT_OHMS * 1000.0f;
        span_low  = CURRENT_SPAN_LOW_MA;
        span_high = CURRENT_SPAN_HIGH_MA;
    } else {
        gain      = (float)(1UL << ANALOG_INPUT_BOARD_VOLTAGE_GAIN);
        value     = adc_volts(sample_code, gain)
                    * ANALOG_INPUT_BOARD_ATTENUATION_RATIO;
        span_low  = VOLTAGE_SPAN_LOW_V;
        span_high = VOLTAGE_SPAN_HIGH_V;
    }

    *measurement = context->linear_min
                   + (value - span_low)
                         * (context->linear_max - context->linear_min)
                         / (span_high - span_low);
    return ANALOG_INPUT_OK;
}

/**
 * @brief Convert a raw code of the ratiometric PT100 front end.
 *
 * The excitation current cancels, so the code depends only on the resistance
 * ratio and on the gain:
 *
 *     R_pt100 = code / 2^23 * (2 * R_reference) / gain
 *
 * The interface helper rejects resistances outside the -200 .. +850 degC sensor
 * range instead of extrapolating; this front end is specified from -200 to
 * 600 degC, so anything outside that is rejected here as well.
 */
static analog_input_status_t convert_pt100(int32_t sample_code,
                                           float* measurement)
{
    analog_input_status_t status;
    float gain;
    float resistance;
    float temperature;

    if ( measurement == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }

    gain       = (float)(1UL << ANALOG_INPUT_BOARD_RTD_GAIN);
    resistance = (float)sample_code / FULL_SCALE_CODE
                 * (2.0f * ANALOG_INPUT_BOARD_RTD_REFERENCE_OHMS) / gain;

    if ( resistance <= 0.0f ) {
        return ANALOG_INPUT_ERR_CONVERSION;
    }

    status = analog_input_pt100_resistance_to_temperature(resistance, &temperature);
    if ( status != ANALOG_INPUT_OK ) {
        return status;
    }
    if ( temperature < ANALOG_INPUT_BOARD_RTD_MIN_TEMPERATURE_C
         || temperature > ANALOG_INPUT_BOARD_RTD_MAX_TEMPERATURE_C ) {
        return ANALOG_INPUT_ERR_CONVERSION;
    }
    *measurement = temperature;
    return ANALOG_INPUT_OK;
}

/**
 * @brief Convert a raw code of a thermocouple input.
 *
 * The code becomes the thermoelectric voltage between the two terminals. Adding
 * the voltage the cold junction itself produces at the measured ambient
 * temperature turns it into the voltage the hot junction would produce against
 * 0 degC, which the inverse sensor polynomial expects.
 */
static analog_input_status_t convert_thermocouple(analog_input_t* self,
                                                  int32_t sample_code,
                                                  const analog_input_conversion_context_t* context,
                                                  float* measurement)
{
    analog_input_status_t status;
    float cold_junction_mv;
    float gain;
    float emf_mv;

    if ( context == NULL ) {
        return ANALOG_INPUT_ERR_ARG;
    }

    gain   = (float)(1UL << ANALOG_INPUT_BOARD_THERMOCOUPLE_GAIN);
    emf_mv = adc_volts(sample_code, gain) * 1000.0f;

    status = analog_input_thermocouple_temperature_to_mv(self->config.thermocouple_type,
                                                         context->cold_junction,
                                                         &cold_junction_mv);
    if ( status != ANALOG_INPUT_OK ) {
        return status;
    }

    return analog_input_thermocouple_mv_to_temperature(self->config.thermocouple_type,
                                                       emf_mv + cold_junction_mv,
                                                       measurement);
}

/* Detection criteria ------------------------------------------------------ */

/** Whether the input type has a wire-break check at all. */
static uint8_t has_break_detection(analog_input_type_t type)
{
    return (type == ANALOG_INPUT_TYPE_THERMOCOUPLE
            || type == ANALOG_INPUT_TYPE_PT100)
               ? 1U
               : 0U;
}

/**
 * @brief Classify a code taken in thermocouple detection mode.
 *
 * The burnout sources pull an open pair to opposite rails, so the result leaves
 * the range a connected thermocouple can produce. Both signs count: which rail
 * the pair ends up at depends on how the sensor is wired. A two-terminal sensor
 * has no second fault this check could tell apart.
 */
static analog_input_wire_status_t thermocouple_wire_status(int32_t sample_code)
{
    if ( sample_code > ANALOG_INPUT_BOARD_BREAK_CODE_THRESHOLD
         || sample_code < -ANALOG_INPUT_BOARD_BREAK_CODE_THRESHOLD ) {
        return ANALOG_INPUT_WIRE_BROKEN;
    }
    return ANALOG_INPUT_WIRE_NORMAL;
}

/**
 * @brief Classify a code taken in PT100 detection mode.
 *
 * The check measures the reference resistor voltage at gain 1 against the
 * internal 2.5 V reference, which is what adc_volts() decodes. An intact
 * three-wire sensor leaves the full voltage there, an open excitation lead
 * removes one of the two currents and halves it, and an open return lead leaves
 * the resistor without any current and clears it.
 */
static analog_input_wire_status_t rtd_wire_status(int32_t sample_code)
{
    float gain;
    float volts;

    gain  = (float)(1UL << SIG24131_GAIN_1);
    volts = adc_volts(sample_code, gain);

    if ( volts
         < ANALOG_INPUT_BOARD_RTD_REFERENCE_VOLTS
               * ANALOG_INPUT_BOARD_RTD_BREAK_RETURN_RATIO ) {
        return ANALOG_INPUT_WIRE_BROKEN_RETURN;
    }
    if ( volts
         < ANALOG_INPUT_BOARD_RTD_REFERENCE_VOLTS
               * ANALOG_INPUT_BOARD_RTD_BREAK_RATIO ) {
        return ANALOG_INPUT_WIRE_BROKEN_LEAD;
    }
    return ANALOG_INPUT_WIRE_NORMAL;
}

/* Operations -------------------------------------------------------------- */

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

    /* Nothing is allowed to change before the request is known to be
     * supported. */
    if ( sig24131_data_rate_from_millihz(rate_millihz, &data_rate)
         != SIG24131_OK ) {
        return ANALOG_INPUT_ERR_UNSUPPORTED;
    }

    fill_config(&adc, config->type);
    adc.data_rate = data_rate;

    /* The switches come first: writing CONF1 restarts the conversion, so the
     * first conversion has to already see the new front end. */
    set_front_end(instance, config->type);

    status = map_status(sig24131_write_config(instance->device, &adc));
    if ( status != ANALOG_INPUT_OK ) {
        /* The front end and the registers may sit halfway between the two
         * configurations, so the object cannot keep claiming the old one. */
        mark_unconfigured(self);
        return status;
    }

    *first_conversion_delay_ms = settle_delay_ms(rate_millihz);

    /* The object stores what is now in effect, which includes the rate a zero
     * request resolved to. */
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

    switch ( self->config.type ) {
        case ANALOG_INPUT_TYPE_4_20MA:
        case ANALOG_INPUT_TYPE_0_10V:
            return convert_linear(self, sample_code, context, measurement);
        case ANALOG_INPUT_TYPE_PT100:
            return convert_pt100(sample_code, measurement);
        case ANALOG_INPUT_TYPE_THERMOCOUPLE:
            return convert_thermocouple(self, sample_code, context, measurement);
    }
    return ANALOG_INPUT_ERR_STATE;
}

/**
 * @brief Switch the channel from measuring to checking its wiring.
 *
 * The input type is checked before anything is written, so an unsupported type
 * leaves the hardware in its measurement configuration. The front-end switches
 * keep their measurement levels: the sensor stays connected to the same pins,
 * only the converter's own view of them changes.
 */
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

    /* The stored rate was resolved by configure(), so this lookup cannot fail
     * unless the object was tampered with. */
    if ( sig24131_data_rate_from_millihz(self->config.sample_rate_millihz,
                                         &data_rate)
         != SIG24131_OK ) {
        return ANALOG_INPUT_ERR_STATE;
    }

    fill_break_config(&adc, self->config.type);
    adc.data_rate = data_rate;

    status = map_status(sig24131_write_config(instance->device, &adc));
    if ( status != ANALOG_INPUT_OK ) {
        /* The device may hold the detection configuration in part, so the
         * object cannot keep claiming a measurement configuration. */
        mark_unconfigured(self);
        return status;
    }

    *first_check_delay_ms = settle_delay_ms(self->config.sample_rate_millihz);
    return ANALOG_INPUT_OK;
}

/**
 * @brief Return the channel to the measurement mode it was configured for.
 *
 * The configuration is rebuilt from the object, so the channel comes back
 * exactly as configure() left it, including the rate a zero request resolved
 * to. The front-end switches never left their measurement levels and are not
 * touched.
 */
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

    *first_conversion_delay_ms = settle_delay_ms(
        self->config.sample_rate_millihz);
    return ANALOG_INPUT_OK;
}

/**
 * @brief Read one code of the detection mode and classify it.
 *
 * The code comes from the same register a measurement reads, so the check is
 * one SPI read and the classification depends on the mode the device is in:
 * outside the detection mode the same thresholds describe data the converter
 * took while it was measuring.
 */
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

    if ( self->config.type == ANALOG_INPUT_TYPE_PT100 ) {
        *wire_status = rtd_wire_status(sample_code);
    } else {
        *wire_status = thermocouple_wire_status(sample_code);
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

/* Channel objects --------------------------------------------------------- */

/*
 * One object per populated channel. The driver instances they point at belong to
 * sig24131_board.c, so what lives here is only the interface state of each
 * channel.
 */

static analog_input_board_t instances[SIG24131_BOARD_INSTANCE_COUNT];

analog_input_status_t analog_input_board_ctor(uint8_t channel)
{
    analog_input_board_t* instance;
    analog_input_status_t status;

    if ( channel == 0U || channel > SIG24131_BOARD_INSTANCE_COUNT ) {
        return ANALOG_INPUT_ERR_ARG;
    }
    instance = &instances[channel - 1U];

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

    for ( channel = 1U; channel <= SIG24131_BOARD_INSTANCE_COUNT; ++channel ) {
        status = analog_input_board_ctor(channel);
        if ( status != ANALOG_INPUT_OK ) {
            return status;
        }
    }
    return ANALOG_INPUT_OK;
}

uint8_t analog_input_board_count(void)
{
    return (uint8_t)SIG24131_BOARD_INSTANCE_COUNT;
}

analog_input_t* analog_input_board_get(uint8_t channel)
{
    if ( channel == 0U || channel > SIG24131_BOARD_INSTANCE_COUNT ) {
        return NULL;
    }
    return &instances[channel - 1U].base;
}
