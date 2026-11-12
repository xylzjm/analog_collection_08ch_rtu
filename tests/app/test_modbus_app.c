#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "qpc.h"

#include "common_app.h"
#include "modbus_app.h"
#include "modbus_app_regs.h"
#include "modbus_cal_regs.h"
#include "modbus_slave_board_stub.h"
#include "param_app.h"
#include "qp_host_stub.h"
#include "storage_app.h"

static QActive storage_ao;
static uint32_t test_tick;

uint32_t board_tick_get(void)
{
    return test_tick;
}

QActive* storage_app_get(void)
{
    return &storage_ao;
}

static modbus_slave_exception_t read_register(modbus_app* app,
                                              uint16_t address,
                                              uint16_t* value)
{
    uint8_t buffer[2] = {0U, 0U};
    modbus_slave_callbacks_t const* callbacks = stub_modbus_callbacks();
    modbus_slave_exception_t exception;

    exception = callbacks->holding_registers(callbacks->ctx, address, 1U,
                                             buffer, MODBUS_SLAVE_REG_READ);
    *value = modbus_app_regs_get_u16(buffer);
    return exception;
}

static modbus_slave_exception_t write_register(modbus_app* app,
                                               uint16_t address,
                                               uint16_t value)
{
    uint8_t buffer[2];
    modbus_slave_callbacks_t const* callbacks = stub_modbus_callbacks();

    (void)app;
    modbus_app_regs_put_u16(buffer, value);
    return callbacks->holding_registers(callbacks->ctx, address, 1U, buffer,
                                        MODBUS_SLAVE_REG_WRITE);
}

static modbus_slave_exception_t write_range(modbus_app* app,
                                            uint16_t address,
                                            uint16_t count,
                                            uint16_t const* values)
{
    uint8_t buffer[2U * MODBUS_CAL_COUNT];
    modbus_slave_callbacks_t const* callbacks = stub_modbus_callbacks();
    uint16_t index;

    (void)app;
    assert(count <= MODBUS_CAL_COUNT);
    for ( index = 0U; index < count; ++index ) {
        modbus_app_regs_put_u16(&buffer[2U * index], values[index]);
    }
    return callbacks->holding_registers(callbacks->ctx, address, count, buffer,
                                        MODBUS_SLAVE_REG_WRITE);
}

static modbus_slave_exception_t read_holding_range(modbus_app* app,
                                                   uint16_t address,
                                                   uint16_t count)
{
    uint8_t buffer[4];
    modbus_slave_callbacks_t const* callbacks = stub_modbus_callbacks();

    (void)app;
    return callbacks->holding_registers(callbacks->ctx, address, count, buffer,
                                        MODBUS_SLAVE_REG_READ);
}

static modbus_slave_exception_t read_input_range(modbus_app* app,
                                                 uint16_t address,
                                                 uint16_t count)
{
    uint8_t buffer[4];
    modbus_slave_callbacks_t const* callbacks = stub_modbus_callbacks();

    (void)app;
    return callbacks->input_registers(callbacks->ctx, address, count, buffer,
                                      MODBUS_SLAVE_REG_READ);
}

static modbus_slave_exception_t read_input_word(modbus_app* app,
                                                uint16_t address,
                                                uint16_t* value)
{
    uint8_t buffer[2] = {0U, 0U};
    modbus_slave_callbacks_t const* callbacks = stub_modbus_callbacks();
    modbus_slave_exception_t exception;

    (void)app;
    exception = callbacks->input_registers(callbacks->ctx, address, 1U, buffer,
                                           MODBUS_SLAVE_REG_READ);
    *value = modbus_app_regs_get_u16(buffer);
    return exception;
}

static void put_s32_words(uint16_t* words, int32_t value)
{
    words[0] = (uint16_t)((uint32_t)value >> 16U);
    words[1] = (uint16_t)value;
}

static int32_t read_s32(modbus_app* app, uint16_t address)
{
    uint16_t high;
    uint16_t low;

    assert(read_register(app, address, &high) == MODBUS_SLAVE_EX_NONE);
    assert(read_register(app, (uint16_t)(address + 1U), &low)
           == MODBUS_SLAVE_EX_NONE);
    return (int32_t)(((uint32_t)high << 16U) | (uint32_t)low);
}

static void send_ch_data(modbus_app* app,
                         uint8_t channel,
                         uint8_t status,
                         int32_t value)
{
    meas_evt_t event;

    memset(&event, 0, sizeof(event));
    event.super.sig = CH_DATA_SIG;
    event.channel   = channel;
    event.status    = status;
    event.value     = value;
    QHSM_DISPATCH(&app->super, &event.super, 0U);
}

static void send_cj_temp(modbus_app* app, uint8_t status, int32_t value)
{
    meas_evt_t event;

    memset(&event, 0, sizeof(event));
    event.super.sig = CJ_TEMP_SIG;
    event.status    = status;
    event.value     = value;
    QHSM_DISPATCH(&app->super, &event.super, 0U);
}

static void send_signal(modbus_app* app, enum_t sig)
{
    QEvt event;

    memset(&event, 0, sizeof(event));
    event.sig = (QSignal)sig;
    QHSM_DISPATCH(&app->super, &event, 0U);
}

static void test_hidden_block(modbus_app* app)
{
    uint16_t value;
    uint16_t values[3];

    assert(read_register(app, MODBUS_CAL_STATUS, &value)
           == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);

    assert(write_register(app, MODBUS_CAL_MAGIC1, MODBUS_CAL_MAGIC1_VALUE)
           == MODBUS_SLAVE_EX_NONE);
    assert(write_register(app, MODBUS_CAL_MAGIC2, MODBUS_CAL_MAGIC2_VALUE)
           == MODBUS_SLAVE_EX_NONE);
    assert(app->calUnlocked == 1U);
    assert(app->calTimer.ctr == MODBUS_CAL_LOCK_TIMEOUT_MS);

    assert(read_register(app, MODBUS_CAL_MAGIC1, &value)
           == MODBUS_SLAVE_EX_NONE);
    assert(value == 0U);

    app->calTimer.ctr = 30000U;
    assert(read_register(app, MODBUS_CAL_STATUS, &value)
           == MODBUS_SLAVE_EX_NONE);
    assert(app->calTimer.ctr == 30000U);
    assert(read_register(app, MODBUS_CAL_REMAINING, &value)
           == MODBUS_SLAVE_EX_NONE);
    assert(value == 30U);
    assert(app->calTimer.ctr == 30000U);

    app->calTimer.ctr = 1U;
    assert(write_register(app, 0x0F11U, 0U)
           == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
    assert(app->calTimer.ctr == MODBUS_CAL_LOCK_TIMEOUT_MS);

    values[0] = 2U;
    values[1] = 1U;
    values[2] = 0U;
    assert(write_range(app, MODBUS_CAL_CHANNEL, 3U, values)
           == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
    assert(app->calChannel == 0xFFU);
    assert(app->calType == 0xFFU);

    assert(write_range(app, MODBUS_CAL_CHANNEL, 2U, values)
           == MODBUS_SLAVE_EX_NONE);
    assert(app->calChannel == 2U);
    assert(app->calType == 1U);

    values[0] = MODBUS_CAL_CMD_SELECT;
    values[1] = 0U;
    assert(write_range(app, MODBUS_CAL_CMD, 2U, values)
           == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);
    assert(write_register(app, MODBUS_CAL_CMD, 0U)
           == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);

    assert(write_register(app, MODBUS_CAL_CMD, MODBUS_CAL_CMD_EXIT)
           == MODBUS_SLAVE_EX_NONE);
    assert(app->calUnlocked == 0U);
    assert(read_register(app, MODBUS_CAL_STATUS, &value)
           == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);

    assert(write_register(app, 0x0F11U, 0U)
           == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
    values[0] = 0U;
    values[1] = 0U;
    assert(write_range(app, 0x0F1FU, 2U, values)
           == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
}

static void test_read_range_boundaries(modbus_app* app)
{
    assert(read_holding_range(app, 0xFFFFU, 2U)
           == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
    assert(read_holding_range(app, 0xFFF0U, 0x20U)
           == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
    assert(read_input_range(app, 0xFFFFU, 2U)
           == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
}

static void test_save_cap_timer(modbus_app* app)
{
    QEvt const* event;

    test_tick = 1000U;
    assert(write_register(app, MODBUS_REG_ADDRESS, 5U)
           == MODBUS_SLAVE_EX_NONE);
    assert(app->saveTimer.ctr == 1000U);
    assert(app->saveCapTimer.ctr == 10000U);
    assert(param_app_dirty_mask() != 0U);

    send_signal(app, MB_SAVE_CAP_TIMER_SIG);

    event = test_qp_pop(&storage_ao);
    assert(event != NULL);
    assert(event->sig == PARAM_SAVE_SIG);
    assert(app->saveTimer.ctr == 0U);
    assert(app->saveCapTimer.ctr == 0U);
    assert(app->saveFirstTick == 0U);
}

static void test_user_registers(modbus_app* app)
{
    uint16_t values[10];
    uint16_t bad[11];
    uint16_t value;
    param_ch_record_t record;

    values[0] = 0U;      /* type: 4-20 mA */
    values[1] = 0xFFFFU; /* rate: default 10 Hz */
    put_s32_words(&values[2], -1000L);
    put_s32_words(&values[4], 1000L);
    put_s32_words(&values[6], -10L);
    put_s32_words(&values[8], 2000L);

    assert(write_range(app, MODBUS_REG_CH_FIRST, 10U, values)
           == MODBUS_SLAVE_EX_NONE);

    assert(read_register(app, MODBUS_REG_CH_FIRST, &value)
           == MODBUS_SLAVE_EX_NONE);
    assert(value == 0U);
    assert(read_register(app, MODBUS_REG_CH_FIRST + 1U, &value)
           == MODBUS_SLAVE_EX_NONE);
    assert(value == MODBUS_REG_RATE_DEFAULT_INDEX);

    assert(param_app_get_channel(0U, &record));
    assert(record.type == 0U);
    assert(record.rate_index == MODBUS_REG_RATE_DEFAULT_INDEX);
    assert(record.linear_min == -1000L);
    assert(record.linear_max == 1000L);
    assert(record.user_offset == -10L);
    assert(record.user_gain == 2000L);
    assert(read_s32(app, MODBUS_REG_CH_FIRST + MODBUS_REG_CH_LINEAR_MIN)
           == -1000L);
    assert(read_s32(app, MODBUS_REG_CH_FIRST + MODBUS_REG_CH_LINEAR_MAX)
           == 1000L);
    assert(read_s32(app, MODBUS_REG_CH_FIRST + MODBUS_REG_CH_USER_OFFSET)
           == -10L);
    assert(read_s32(app, MODBUS_REG_CH_FIRST + MODBUS_REG_CH_USER_GAIN)
           == 2000L);

    /* Invalid gain/offset must be rejected using the newly written value. */
    values[8] = 0U;
    values[9] = 99U;
    assert(write_range(app, MODBUS_REG_CH_FIRST + MODBUS_REG_CH_USER_GAIN,
                       2U, &values[8]) == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);
    put_s32_words(&values[6], -1000001L);
    assert(write_range(app, MODBUS_REG_CH_FIRST + MODBUS_REG_CH_USER_OFFSET,
                       2U, &values[6]) == MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE);
    assert(param_app_get_channel(0U, &record));
    assert(record.user_gain == 2000L);
    assert(record.user_offset == -10L);

    /* FC10 that includes a reserved register must not change the channel. */
    memset(bad, 0, sizeof(bad));
    bad[0] = 6U;
    bad[9] = 1000U; /* keep the user gain valid so the reserved offset is reached */
    {
        modbus_slave_exception_t ex = write_range(app, MODBUS_REG_CH_FIRST,
                                                  11U, bad);
        assert(ex == MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS);
    }
    assert(param_app_get_channel(0U, &record));
    assert(record.type == 0U);
    assert(record.rate_index == MODBUS_REG_RATE_DEFAULT_INDEX);
}

static void test_address_and_identity(modbus_app* app)
{
    uint16_t value;

    assert(write_register(app, MODBUS_REG_ADDRESS, 7U)
           == MODBUS_SLAVE_EX_NONE);
    assert(stub_modbus_identity()->slave_id == 7U);
    assert(read_register(app, MODBUS_REG_ADDRESS, &value)
           == MODBUS_SLAVE_EX_NONE);
    assert(value == 7U);
}

static void test_status_ownership(modbus_app* app)
{
    uint16_t values[10];

    send_ch_data(app, 0U, MODBUS_CH_STATUS_ADC_IO_FAULT,
                 MODBUS_IMAGE_INVALID_VALUE);
    assert(app->image.ch[0].status == MODBUS_CH_STATUS_ADC_IO_FAULT);

    values[0] = 0U; /* 4-20 mA with invalid linear limits 0/0 */
    values[1] = MODBUS_REG_RATE_DEFAULT_INDEX;
    put_s32_words(&values[2], 0L);
    put_s32_words(&values[4], 0L);
    put_s32_words(&values[6], 0L);
    put_s32_words(&values[8], 1000L);
    assert(write_range(app, MODBUS_REG_CH_FIRST, 10U, values)
           == MODBUS_SLAVE_EX_NONE);

    /* M2: configuration validity belongs to channel_app, not modbus_app. */
    assert(app->image.ch[0].status == MODBUS_CH_STATUS_ADC_IO_FAULT);

    send_ch_data(app, 0U, MODBUS_CH_STATUS_NORMAL, 1234L);
    assert(app->image.ch[0].status == MODBUS_CH_STATUS_NORMAL);
    assert(app->image.ch[0].value == 1234L);
}

static void test_input_image(modbus_app* app)
{
    uint16_t value;

    send_ch_data(app, 3U, MODBUS_CH_STATUS_NORMAL, -1234L);
    assert(read_input_word(app, MODBUS_IN_CH_VALUE_FIRST + 6U, &value)
           == MODBUS_SLAVE_EX_NONE);
    assert(value == 0xFFFFU);
    assert(read_input_word(app, MODBUS_IN_CH_VALUE_FIRST + 7U, &value)
           == MODBUS_SLAVE_EX_NONE);
    assert(value == (uint16_t)-1234L);
    assert(read_input_word(app, MODBUS_IN_CH_STATUS + 3U, &value)
           == MODBUS_SLAVE_EX_NONE);
    assert(value == MODBUS_CH_STATUS_NORMAL);

    send_cj_temp(app, MODBUS_CJ_STATUS_NORMAL, 25000L);
    assert(read_input_word(app, MODBUS_IN_CJ_VALUE, &value)
           == MODBUS_SLAVE_EX_NONE);
    assert(value == 0U);
    assert(read_input_word(app, MODBUS_IN_CJ_VALUE + 1U, &value)
           == MODBUS_SLAVE_EX_NONE);
    assert(value == 25000U);
    assert(read_input_word(app, MODBUS_IN_CJ_STATUS, &value)
           == MODBUS_SLAVE_EX_NONE);
    assert(value == MODBUS_CJ_STATUS_NORMAL);
}

int main(void)
{
    modbus_app app;

    param_app_init();
    test_qp_clear();

    modbus_app_ctor(&app, 0U);
    QHSM_INIT(&app.super, NULL, 0U);

    assert(stub_modbus_running());
    assert(test_qp_is_subscribed(CJ_TEMP_SIG));
    assert(stub_modbus_identity()->additional_length
           == APP_IDENTITY_ADDITIONAL_LENGTH);
    assert(memcmp(stub_modbus_identity()->additional,
                  APP_MODEL_STRING, APP_MODEL_STRING_LENGTH) == 0);
    assert(stub_modbus_identity()->additional[APP_MODEL_STRING_LENGTH]
           == APP_VERSION_MAJOR);
    assert(stub_modbus_identity()->additional[APP_MODEL_STRING_LENGTH + 1U]
           == APP_VERSION_MINOR);
    assert(stub_modbus_identity()->additional[APP_MODEL_STRING_LENGTH + 2U]
           == APP_VERSION_PATCH);

    test_read_range_boundaries(&app);
    test_hidden_block(&app);
    test_save_cap_timer(&app);
    test_user_registers(&app);
    test_address_and_identity(&app);
    test_status_ownership(&app);
    test_input_image(&app);

    puts("test_modbus_app: PASS");
    return 0;
}
