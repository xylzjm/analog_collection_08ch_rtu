#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "qpc.h"

#include "analog_input_board.h"
#include "cal_data.h"
#include "calibrate_app.h"
#include "common_app.h"
#include "qp_host_stub.h"

static QActive container;
static QActive storage;

static void send_signal(calibrate_app* cal, enum_t sig)
{
    QEvt event;

    memset(&event, 0, sizeof(event));
    event.sig = (QSignal)sig;
    QHSM_DISPATCH(&cal->super, &event, 0U);
}

static void select_input(calibrate_app* cal, uint8_t channel, uint8_t type)
{
    cal_select_evt_t event;

    memset(&event, 0, sizeof(event));
    event.super.sig = CAL_SELECT_SIG;
    event.channel   = channel;
    event.type      = type;
    QHSM_DISPATCH(&cal->super, &event.super, 0U);
}

static void dispatch_ready(calibrate_app* cal)
{
    QEvt const* event = test_qp_pop(&container);

    assert(event != NULL);
    assert(event->sig == CH_CAL_READY_SIG);
    QHSM_DISPATCH(&cal->super, event, 0U);
}

static void settle(calibrate_app* cal)
{
    send_signal(cal, CAL_SETTLE_TIMER_SIG);
    assert(cal->status == CAL_STATUS_SETTLED);
}

static void capture_point(calibrate_app* cal, enum_t start_sig, int32_t code)
{
    uint8_t sample;

    stub_analog_input_set_sample(code);
    send_signal(cal, start_sig);
    for ( sample = 0U; sample < CAL_SAMPLE_COUNT; ++sample ) {
        /* A one-shot QTimeEvt is already disarmed when its event is
         * dispatched; the test dispatches the event directly. */
        cal->sampleTimer.ctr = 0U;
        send_signal(cal, CAL_SAMPLE_TIMER_SIG);
    }
}

static cal_save_evt_t const* commit_and_take_save(calibrate_app* cal)
{
    QEvt const* event;

    send_signal(cal, CAL_COMMIT_SIG);
    assert(cal->status == CAL_STATUS_SAVING);

    event = test_qp_pop(&storage);
    assert(event != NULL);
    assert(event->sig == CAL_SAVE_SIG);
    return (cal_save_evt_t const*)event;
}

static void test_full_cycle(calibrate_app* cal)
{
    cal_save_evt_t const* save;

    select_input(cal, 0U, CAL_DATA_TYPE_THERMOCOUPLE);
    assert(cal->status == CAL_STATUS_WAITING_CHANNEL);
    assert(cal->channelTimer.ctr == 1000U);
    dispatch_ready(cal);
    assert(cal->status == CAL_STATUS_SETTLING);
    assert(stub_analog_input_config.sample_rate_millihz == 10000U);
    assert(stub_analog_input_config.type == ANALOG_INPUT_TYPE_THERMOCOUPLE);
    assert(stub_analog_input_config.thermocouple_type
           == ANALOG_INPUT_THERMOCOUPLE_TYPE_K);
    assert(cal->settleTimer.ctr == 60U);

    settle(cal);
    capture_point(cal, CAL_LOW_SIG, 0L);
    assert(cal->status == CAL_STATUS_LOW_CAPTURED);
    assert(cal->rawLow == 0L);

    capture_point(cal, CAL_HIGH_SIG, 5000000L);
    assert(cal->status == CAL_STATUS_HIGH_CAPTURED);
    assert(cal->rawHigh == 5000000L);

    save = commit_and_take_save(cal);
    assert(save->channel == 0U);
    assert(save->record->valid_mask
           == (uint8_t)(1U << CAL_DATA_TYPE_THERMOCOUPLE));
    assert(save->record->entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro
           == 1000000L);
    assert(save->record->entry[CAL_DATA_TYPE_THERMOCOUPLE].offset_code == 0L);

    send_signal(cal, CAL_SAVED_SIG);
    assert(cal->status == CAL_STATUS_DONE);
    assert(cal_data_valid_mask(0U)
           == (uint8_t)(1U << CAL_DATA_TYPE_THERMOCOUPLE));
    assert(cal_data_get(0U)->entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro
           == 1000000L);
}

static void test_save_failure_and_exit(calibrate_app* cal)
{
    cal_save_evt_t const* save;

    select_input(cal, 0U, CAL_DATA_TYPE_THERMOCOUPLE);
    dispatch_ready(cal);
    settle(cal);
    capture_point(cal, CAL_LOW_SIG, 0L);
    capture_point(cal, CAL_HIGH_SIG, 6000000L);

    save = commit_and_take_save(cal);
    assert(save->record->entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro
           == 833333L);

    send_signal(cal, CAL_SAVE_FAILED_SIG);
    assert(cal->status == CAL_STATUS_HIGH_CAPTURED);
    assert(cal->error == CAL_ERROR_SAVE_FAILED);
    assert(cal_data_get(0U)->entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro
           == 1000000L);

    send_signal(cal, CAL_EXIT_SIG);
    assert(cal->status == CAL_STATUS_IDLE);
}

static void test_exit_while_saving(calibrate_app* cal)
{
    select_input(cal, 0U, CAL_DATA_TYPE_THERMOCOUPLE);
    dispatch_ready(cal);
    settle(cal);
    capture_point(cal, CAL_LOW_SIG, 0L);
    capture_point(cal, CAL_HIGH_SIG, 5000000L);

    (void)commit_and_take_save(cal);
    send_signal(cal, CAL_EXIT_SIG);
    assert(cal->status == CAL_STATUS_SAVING);
    assert(cal->exitPending == 1U);

    send_signal(cal, CAL_SAVED_SIG);
    assert(cal->status == CAL_STATUS_IDLE);
}

static void test_raw_saturation(calibrate_app* cal)
{
    select_input(cal, 0U, CAL_DATA_TYPE_THERMOCOUPLE);
    dispatch_ready(cal);
    settle(cal);
    capture_point(cal, CAL_LOW_SIG, 0L);
    capture_point(cal, CAL_HIGH_SIG, CAL_RAW_CODE_LIMIT);

    send_signal(cal, CAL_COMMIT_SIG);
    assert(cal->status == CAL_STATUS_HIGH_CAPTURED);
    assert(cal->error == CAL_ERROR_CAPTURE_FAILED);
    assert(test_qp_pop(&storage) == NULL);

    send_signal(cal, CAL_EXIT_SIG);
    assert(cal->status == CAL_STATUS_IDLE);
}

static void test_commit_without_storage(calibrate_app* cal)
{
    cal_save_evt_t const* save;

    select_input(cal, 0U, CAL_DATA_TYPE_THERMOCOUPLE);
    dispatch_ready(cal);
    settle(cal);
    capture_point(cal, CAL_LOW_SIG, 0L);
    capture_point(cal, CAL_HIGH_SIG, 6000000L);

    calibrate_app_bind_storage(cal, NULL);
    send_signal(cal, CAL_COMMIT_SIG);
    assert(cal->status == CAL_STATUS_HIGH_CAPTURED);
    assert(cal->error == CAL_ERROR_SAVE_FAILED);
    assert(cal->pending.entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro
           == 1000000L);

    calibrate_app_bind_storage(cal, &storage);
    save = commit_and_take_save(cal);
    assert(save->record->entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro
           == 833333L);
    send_signal(cal, CAL_SAVED_SIG);
    assert(cal->status == CAL_STATUS_DONE);
    send_signal(cal, CAL_EXIT_SIG);
    assert(cal->status == CAL_STATUS_IDLE);
}

static void test_error_paths(calibrate_app* cal)
{
    send_signal(cal, CAL_LOW_SIG);
    assert(cal->status == CAL_STATUS_ERROR);
    assert(cal->error == CAL_ERROR_NOT_SELECTED);
    send_signal(cal, CAL_ENTER_SIG);
    assert(cal->status == CAL_STATUS_IDLE);

    select_input(cal, 8U, CAL_DATA_TYPE_THERMOCOUPLE);
    assert(cal->status == CAL_STATUS_ERROR);
    assert(cal->error == CAL_ERROR_INVALID_CHANNEL_TYPE);
    send_signal(cal, CAL_ENTER_SIG);
    assert(cal->status == CAL_STATUS_IDLE);

    select_input(cal, 0U, CAL_DATA_TYPE_THERMOCOUPLE);
    send_signal(cal, CAL_CHANNEL_TIMER_SIG);
    assert(cal->status == CAL_STATUS_ERROR);
    assert(cal->error == CAL_ERROR_CHANNEL_NOT_READY);
    test_qp_clear();
    send_signal(cal, CAL_ENTER_SIG);
    assert(cal->status == CAL_STATUS_IDLE);
}

int main(void)
{
    calibrate_app cal;

    cal_data_init();
    stub_factory_cal_reset();
    stub_analog_input_reset();
    test_qp_clear();

    calibrate_app_ctor(&cal, &container);
    calibrate_app_bind_storage(&cal, &storage);
    QHSM_INIT(&cal.super, NULL, 0U);
    assert(cal.status == CAL_STATUS_IDLE);

    test_error_paths(&cal);
    test_full_cycle(&cal);
    test_save_failure_and_exit(&cal);
    test_exit_while_saving(&cal);
    test_raw_saturation(&cal);
    test_commit_without_storage(&cal);

    puts("test_calibrate_app: PASS");
    return 0;
}
