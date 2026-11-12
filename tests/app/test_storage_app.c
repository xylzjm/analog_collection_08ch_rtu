#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "qpc.h"

#include "common_app.h"
#include "param_app.h"
#include "qp_host_stub.h"
#include "storage_app.h"
#include "storage_flash_stub.h"

static QActive modbus;
static QEvt const param_save_event = QEVT_INITIALIZER(PARAM_SAVE_SIG);

static cal_ch_record_t cal_record;
static cal_save_evt_t cal_event;

static void send_signal(storage_app* storage, enum_t sig)
{
    QEvt event;

    memset(&event, 0, sizeof(event));
    event.sig = (QSignal)sig;
    QHSM_DISPATCH(&storage->super, &event, 0U);
}

static void send_event(storage_app* storage, QEvt const* event)
{
    QHSM_DISPATCH(&storage->super, event, 0U);
}

static void expect_signal(QActive* target, enum_t sig)
{
    QEvt const* event = test_qp_pop(target);

    assert(event != NULL);
    if ( event->sig != sig ) {
        fprintf(stderr, "expected sig %d, got %d\n", (int)sig,
                (int)event->sig);
    }
    assert(event->sig == sig);
}

static void make_dirty(uint16_t address)
{
    param_sys_record_t sys;

    param_app_get_sys(&sys);
    sys.address = address;
    param_app_set_sys(&sys);
}

static void make_cal_event(uint8_t channel)
{
    memset(&cal_record, 0, sizeof(cal_record));
    cal_record.version = CAL_DATA_VERSION;
    cal_record.size    = (uint16_t)sizeof(cal_record);
    cal_record.valid_mask = (uint8_t)(1U << CAL_DATA_TYPE_THERMOCOUPLE);
    cal_record.entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro = 1000000L;
    cal_record.entry[CAL_DATA_TYPE_THERMOCOUPLE].offset_code = 0L;

    memset(&cal_event, 0, sizeof(cal_event));
    cal_event.super.sig = CAL_SAVE_SIG;
    cal_event.channel   = channel;
    cal_event.record    = &cal_record;
}

static void test_param_save_success(storage_app* storage)
{
    make_dirty(5U);
    send_signal(storage, PARAM_SAVE_SIG);

    assert(stub_param_writes == 1U);
    assert(stub_last_param_key == 0U);
    assert(param_app_dirty_mask() == 0U);

    expect_signal(&modbus, PARAM_SAVED_SIG);
    expect_signal(&storage->super, STORAGE_DONE_SIG);
    send_signal(storage, STORAGE_DONE_SIG);
}

static void test_param_save_no_dirty(storage_app* storage)
{
    uint32_t const before = stub_param_writes;

    send_signal(storage, PARAM_SAVE_SIG);
    assert(stub_param_writes == before);
    assert(param_app_dirty_mask() == 0U);
}

static void test_param_save_failure_and_retry(storage_app* storage)
{
    make_dirty(6U);
    stub_storage_fail = true;
    send_signal(storage, PARAM_SAVE_SIG);

    assert(param_app_dirty_mask() != 0U);
    expect_signal(&modbus, PARAM_SAVE_FAILED_SIG);
    expect_signal(&storage->super, STORAGE_FAIL_SIG);
    send_signal(storage, STORAGE_FAIL_SIG);
    assert(storage->retryTimer.ctr == 5000U);

    stub_storage_fail = false;
    send_signal(storage, STORAGE_RETRY_TIMER_SIG);
    assert(param_app_dirty_mask() == 0U);
    expect_signal(&modbus, PARAM_SAVED_SIG);
    expect_signal(&storage->super, STORAGE_DONE_SIG);
    send_signal(storage, STORAGE_DONE_SIG);
}

static void test_cal_deferred_behind_params(storage_app* storage)
{
    uint32_t const param_before = stub_param_writes;
    uint32_t const cal_before   = stub_cal_writes;

    make_dirty(7U);
    make_cal_event(2U);

    send_signal(storage, PARAM_SAVE_SIG);
    assert(stub_param_writes == param_before + 1U);
    expect_signal(&modbus, PARAM_SAVED_SIG);
    expect_signal(&storage->super, STORAGE_DONE_SIG);

    send_event(storage, &cal_event.super);
    assert(test_qp_deferred_count() == 1U);
    assert(stub_cal_writes == cal_before);

    send_signal(storage, STORAGE_DONE_SIG);
    expect_signal(&storage->super, CAL_SAVE_SIG);
    send_event(storage, &cal_event.super);

    assert(stub_cal_writes == cal_before + 1U);
    assert(stub_last_cal_channel == 2U);
    assert(stub_last_cal_record.entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro
           == 1000000L);
    expect_signal(&modbus, CAL_SAVED_SIG);
    expect_signal(&storage->super, STORAGE_DONE_SIG);
    send_signal(storage, STORAGE_DONE_SIG);
}

static void test_param_deferred_behind_cal(storage_app* storage)
{
    uint32_t const cal_before = stub_cal_writes;

    make_dirty(8U);
    make_cal_event(3U);

    send_event(storage, &cal_event.super);
    assert(stub_cal_writes == cal_before + 1U);
    expect_signal(&modbus, CAL_SAVED_SIG);
    expect_signal(&storage->super, STORAGE_DONE_SIG);

    send_event(storage, &param_save_event);
    assert(test_qp_deferred_count() == 1U);

    send_signal(storage, STORAGE_DONE_SIG);
    expect_signal(&storage->super, PARAM_SAVE_SIG);
    send_event(storage, &param_save_event);

    assert(stub_param_writes != 0U);
    assert(param_app_dirty_mask() == 0U);
    expect_signal(&modbus, PARAM_SAVED_SIG);
    expect_signal(&storage->super, STORAGE_DONE_SIG);
    send_signal(storage, STORAGE_DONE_SIG);
}

static void test_deferred_lifo(storage_app* storage)
{
    make_dirty(10U);
    make_cal_event(6U);

    send_signal(storage, PARAM_SAVE_SIG);
    expect_signal(&storage->super, STORAGE_DONE_SIG);

    send_event(storage, &cal_event.super);
    send_event(storage, &param_save_event);
    assert(test_qp_deferred_count() == 2U);

    send_signal(storage, STORAGE_DONE_SIG);
    expect_signal(&storage->super, PARAM_SAVE_SIG);
    expect_signal(&storage->super, CAL_SAVE_SIG);

    test_qp_clear();
}

static void test_defer_full_cal(storage_app* storage)
{
    uint32_t const cal_before = stub_cal_writes;
    uint8_t index;

    make_cal_event(4U);
    send_event(storage, &cal_event.super);
    assert(stub_cal_writes == cal_before + 1U);
    expect_signal(&modbus, CAL_SAVED_SIG);
    expect_signal(&storage->super, STORAGE_DONE_SIG);

    for ( index = 0U; index < 16U; ++index ) {
        send_event(storage, &cal_event.super);
    }
    assert(test_qp_deferred_count() == 16U);

    /* The 17th request exceeds the defer depth and must fail explicitly. */
    send_event(storage, &cal_event.super);
    assert(test_qp_deferred_count() == 16U);
    expect_signal(&modbus, CAL_SAVE_FAILED_SIG);

    test_qp_clear();
}

static void test_cal_failure(storage_app* storage)
{
    make_cal_event(4U);
    stub_storage_fail = true;

    send_event(storage, &cal_event.super);
    assert(stub_cal_writes != 0U);
    expect_signal(&modbus, CAL_SAVE_FAILED_SIG);
    expect_signal(&storage->super, STORAGE_FAIL_SIG);
    send_signal(storage, STORAGE_FAIL_SIG);

    assert(storage->retryTimer.ctr == 0U);
    assert(storage->pendingCalChannel == 0xFFU);
    assert(storage->pendingCal.version == 0U);
    stub_storage_fail = false;
}

int main(void)
{
    storage_app storage;

    param_app_init();
    stub_storage_reset();
    test_qp_clear();

    storage_app_ctor(&storage, &modbus);
    QHSM_INIT(&storage.super, NULL, 0U);

    test_param_save_success(&storage);
    test_param_save_no_dirty(&storage);
    test_param_save_failure_and_retry(&storage);
    test_cal_deferred_behind_params(&storage);
    test_param_deferred_behind_cal(&storage);
    test_cal_failure(&storage);
    test_deferred_lifo(&storage);
    test_defer_full_cal(&storage);

    puts("test_storage_app: PASS");
    return 0;
}
