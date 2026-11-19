#ifndef ANALOG_INPUT_FAKE_H_
#define ANALOG_INPUT_FAKE_H_

#include <stdint.h>

#include "analog_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Recorded operation kinds, one enum value per vtable slot. */
typedef enum {
    ANALOG_INPUT_FAKE_CONFIGURE = 0,
    ANALOG_INPUT_FAKE_READ_SAMPLE,
    ANALOG_INPUT_FAKE_CONVERT,
    ANALOG_INPUT_FAKE_ENABLE_BREAK,
    ANALOG_INPUT_FAKE_CHECK_WIRE,
    ANALOG_INPUT_FAKE_DISABLE_BREAK,
    ANALOG_INPUT_FAKE_OP_COUNT
} analog_input_fake_op_t;

/** @brief Upper bound on the recorded calls of one scenario. */
#define ANALOG_INPUT_FAKE_LOG_MAX 64U

/** @brief One recorded call stamped with the virtual tick and the rate. */
typedef struct {
    uint8_t  op;     /**< analog_input_fake_op_t of the call. */
    uint32_t tick;   /**< g_test_tick when the call happened. */
    uint32_t rate;   /**< Effective config rate in mHz at call time. */
} analog_input_fake_call_t;

/** @brief Fake analog input: scriptable results, records every call. */
typedef struct {
    analog_input_t base;  /**< Interface base object; must stay first. */
    analog_input_fake_call_t log[ANALOG_INPUT_FAKE_LOG_MAX];
    uint16_t log_len;
    /* Script */
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
 * @brief Construct the fake and install the operation table.
 *
 * Defaults model an intact channel: every operation succeeds, configure
 * reports a 200 ms settle (two conversions at 10 Hz), the check reports a
 * closed circuit and the first sample decodes to 25.0.
 */
void analog_input_fake_init(analog_input_fake_t* f);

/** @brief Drop the call log and the script counters; keep the object valid. */
void analog_input_fake_reset_log(analog_input_fake_t* f);

/** @brief Count the recorded calls of one operation kind. */
uint16_t analog_input_fake_count(const analog_input_fake_t* f,
                                 uint8_t op);

/** @brief Indexed access to the call log; NULL when out of range. */
const analog_input_fake_call_t* analog_input_fake_at(const analog_input_fake_t* f,
                                                     uint16_t index);

#ifdef __cplusplus
}
#endif

#endif
