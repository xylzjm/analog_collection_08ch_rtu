#ifndef APP_EVT_H
#define APP_EVT_H

#include "qpc.h"

#include "analog_input.h"
#include "cal_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief CAL_SELECT_SIG payload; built on the stack and dispatched synchronously. */
typedef struct {
    QEvt super;
    uint8_t channel; /**< Modbus channel 0..7. */
    uint8_t type;    /**< Calibration type 0..3. */
} cal_select_evt_t;

/** @brief CH_DATA_SIG and CJ_TEMP_SIG payload. */
typedef struct {
    QEvt super;
    uint8_t channel;    /**< Modbus channel, or 0 for the cold junction. */
    uint8_t status;     /**< Channel or cold-junction status code. */
    uint8_t reserved[4];/**< Keeps measurement events out of the calibration pool. */
    int32_t value;      /**< Engineering value x0.001, or INT32_MIN. */
} meas_evt_t;

/** @brief CH_CONFIG_SIG payload. */
typedef struct {
    QEvt super;
    uint8_t channel;                 /**< Modbus channel 0..7. */
    analog_input_config_t config;    /**< Input type, model and rate. */
    int32_t linear_min;              /**< Linear lower limit x0.001. */
    int32_t linear_max;              /**< Linear upper limit x0.001. */
    int32_t user_offset;             /**< User offset x0.001. */
    int32_t user_gain;               /**< User gain x0.001. */
} cfg_evt_t;

/** @brief CAL_SAVE_SIG payload; storage copies the record immediately. */
typedef struct {
    QEvt super;
    uint8_t channel;               /**< Modbus channel 0..7; selects calN. */
    cal_ch_record_t const* record; /**< Pending record owned by calibrate_app. */
} cal_save_evt_t;

#ifdef __cplusplus
}
#endif

#endif
