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

/* Sample rate ------------------------------------------------------------- */

/** Widest code the data-rate field of CONF1 can carry. */
#define RATE_CODE_MAX 0x3FU

/**
 * @brief Translate a requested rate into a SIG24131 data-rate code.
 *
 * The driver owns the code-to-rate table, so the board scans it instead of
 * duplicating the 32 entries. Returns non-zero when the rate is supported.
 */
static uint8_t lookup_rate(uint32_t millihz,
                           sig24131_data_rate_t* data_rate)
{
    uint32_t code;

    for ( code = 0U; code <= RATE_CODE_MAX; code++ ) {
        if ( sig24131_data_rate_millihz((sig24131_data_rate_t)code) == millihz ) {
            *data_rate = (sig24131_data_rate_t)code;
            return 1U;
        }
    }
    return 0U;
}

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
    return ((uint32_t)ANALOG_INPUT_BOARD_SETTLE_CONVERSIONS * 1000000U + rate_millihz - 1U)
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
    return (instance->base.config.thermocouple_type != ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE)
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

/* Operations -------------------------------------------------------------- */

/*
 * convert() and the wire-break work mode follow in the next steps. Until they
 * exist they report that the object is not ready, so a half-built layer cannot
 * be mistaken for a working one.
 */

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
    if ( lookup_rate(rate_millihz, &data_rate) == 0U ) {
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
    (void)self;
    (void)sample_code;
    (void)context;
    (void)measurement;
    return ANALOG_INPUT_ERR_STATE;
}

static analog_input_status_t enable_break_detection(analog_input_t* self)
{
    (void)self;
    return ANALOG_INPUT_ERR_STATE;
}

static analog_input_status_t disable_break_detection(analog_input_t* self)
{
    (void)self;
    return ANALOG_INPUT_ERR_STATE;
}

static analog_input_status_t check_wire_break(analog_input_t* self,
                                              analog_input_wire_status_t* wire_status)
{
    (void)self;
    (void)wire_status;
    return ANALOG_INPUT_ERR_STATE;
}

static const analog_input_ops_t ops = {
    .configure               = configure,
    .read_sample             = read_sample,
    .convert                 = convert,
    .enable_break_detection  = enable_break_detection,
    .disable_break_detection = disable_break_detection,
    .check_wire_break        = check_wire_break,
};

/* Object ------------------------------------------------------------------ */

analog_input_status_t analog_input_board_ctor(
    analog_input_board_t* self,
    sig24131_t* device,
    uint8_t channel)
{
    analog_input_status_t status;

    if ( self == NULL || device == NULL
         || channel == 0U
         || channel > SIG24131_BOARD_INSTANCE_COUNT ) {
        return ANALOG_INPUT_ERR_ARG;
    }

    status = analog_input_ctor(&self->base, &ops);
    if ( status != ANALOG_INPUT_OK ) {
        return status;
    }
    self->device  = device;
    self->channel = channel;
    return ANALOG_INPUT_OK;
}
