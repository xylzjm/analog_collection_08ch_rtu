/**
 * @file analog_input_fake.c
 * @brief Fake analog input over the six-slot ops vtable.
 *
 * The call log is the trajectory evidence for the AO tests: entry and
 * exit of the detection and measurement modes are visible as
 * enable/check/disable records, each stamped with the virtual tick and
 * the effective configuration, so deadline and stale-generation
 * violations can be asserted without any tracing facility.
 */

#include "analog_input_fake.h"

#include "host_support.h"

/** @brief Append one record; the log is sized per scenario, overflow is a bug. */
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

    /* Implementations own the cached configuration; record it too. */
    f->base.config = *config;
    record(f, (uint8_t)ANALOG_INPUT_FAKE_CONFIGURE);
    f->log[f->log_len - 1U].type = (uint8_t)config->type;
    *first_conversion_delay_ms = f->configure_delay_ms;
    return f->configure_status;
}

static analog_input_status_t fake_read_sample(analog_input_t* self,
                                              int32_t* sample_code)
{
    analog_input_fake_t* f = (analog_input_fake_t*)self;

    record(f, (uint8_t)ANALOG_INPUT_FAKE_READ_SAMPLE);
    f->log[f->log_len - 1U].code = f->sample_code;
    *sample_code = f->sample_code;
    return f->read_status;
}

static analog_input_status_t fake_convert(analog_input_t* self,
                                          int32_t sample_code,
                                          const analog_input_conversion_context_t* context,
                                          float* measurement)
{
    analog_input_fake_t* f = (analog_input_fake_t*)self;

    (void)sample_code;
    (void)context;
    record(f, (uint8_t)ANALOG_INPUT_FAKE_CONVERT);
    f->log[f->log_len - 1U].raw = f->convert_raw;
    *measurement = f->convert_raw;
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
    f->log[f->log_len - 1U].wire = (uint8_t)f->check_wire;
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

/** @brief The one operation table every fake instance installs. */
static const analog_input_ops_t ops = {
    &fake_configure,
    &fake_read_sample,
    &fake_convert,
    &fake_enable_break_detection,
    &fake_disable_break_detection,
    &fake_check_wire_break
};

void analog_input_fake_init(analog_input_fake_t* f)
{
    (void)analog_input_ctor(&f->base, &ops);

    f->log_len          = 0U;
    f->configure_status  = ANALOG_INPUT_OK;
    f->configure_delay_ms = 200U;
    f->read_status      = ANALOG_INPUT_OK;
    f->sample_code      = 1000;
    f->convert_status   = ANALOG_INPUT_OK;
    f->convert_raw      = 25.0f;
    f->enable_status    = ANALOG_INPUT_OK;
    f->enable_delay_ms  = 100U;
    f->check_status     = ANALOG_INPUT_OK;
    f->check_wire       = ANALOG_INPUT_WIRE_NORMAL;
    f->disable_status   = ANALOG_INPUT_OK;
    f->disable_delay_ms = 200U;
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
