#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cal_data.h"
#include "flashdb_stub.h"
#include "modbus_app_regs.h"
#include "param_app.h"
#include "storage_flash.h"

static void write_sys(uint16_t address)
{
    param_sys_record_t sys;
    uint16_t len = 0U;
    uint32_t rev = 0U;

    param_app_get_sys(&sys);
    sys.address = address;
    param_app_set_sys(&sys);
    assert(param_app_snapshot(0U, &sys, &len, &rev));
    assert(storage_flash_write_param(0U, &sys, len));
}

static void write_channels(uint8_t type, uint8_t rate_index)
{
    param_ch_record_t ch;
    uint16_t len = 0U;
    uint32_t rev = 0U;
    uint8_t channel;

    for ( channel = 0U; channel < PARAM_APP_CHANNEL_COUNT; ++channel ) {
        assert(param_app_get_channel(channel, &ch));
        ch.type       = type;
        ch.rate_index = rate_index;
        assert(param_app_set_channel(channel, &ch));
        assert(param_app_snapshot((uint8_t)(channel + 1U), &ch, &len, &rev));
        assert(storage_flash_write_param((uint8_t)(channel + 1U), &ch, len));
    }
}

static void make_valid_cal(cal_ch_record_t* cal)
{
    memset(cal, 0, sizeof(*cal));
    cal->version    = CAL_DATA_VERSION;
    cal->size       = (uint16_t)sizeof(*cal);
    cal->valid_mask = (uint8_t)(1U << CAL_DATA_TYPE_THERMOCOUPLE);
    cal->entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro  = 1000000L;
    cal->entry[CAL_DATA_TYPE_THERMOCOUPLE].offset_code = 0L;
}

static void prepare_db(void)
{
    stub_fdb_reset();
    param_app_init();
    cal_data_init();
    (void)storage_load(); /* opens the host DB and sets db_ready */
    param_app_init();
    cal_data_init();
}

static void test_load_valid(void)
{
    param_ch_record_t ch;
    cal_ch_record_t cal;

    prepare_db();
    write_sys(9U);
    write_channels(6U, modbus_app_regs_rate_store_index(0xFFFFU));
    make_valid_cal(&cal);
    assert(storage_flash_write_cal(0U, &cal));

    assert(storage_load() == false);
    assert(param_app_get_address() == 9U);
    assert(param_app_get_channel(0U, &ch));
    assert(ch.type == 6U);
    assert(ch.rate_index == 2U);
    assert(cal_data_valid_mask(0U)
           == (uint8_t)(1U << CAL_DATA_TYPE_THERMOCOUPLE));
}

static void test_load_missing_and_corrupt(void)
{
    param_ch_record_t ch;
    param_ch_record_t bad;

    prepare_db();
    write_sys(11U);
    assert(storage_load() == true);
    assert(param_app_get_address() == 11U);
    assert(param_app_get_channel(0U, &ch));
    assert(ch.type == PARAM_APP_DEFAULT_TYPE);

    prepare_db();
    write_sys(12U);
    write_channels(6U, 2U);
    assert(param_app_get_channel(0U, &bad));
    bad.version = 99U;
    /* Bypass param_app_set_channel() so the corrupt version reaches Flash. */
    assert(storage_flash_write_param(1U, &bad, (uint16_t)sizeof(bad)));

    assert(storage_load() == true);
    assert(param_app_get_channel(0U, &ch));
    assert(ch.version == PARAM_APP_VERSION);
    assert(ch.type == PARAM_APP_DEFAULT_TYPE);
}

static void test_cal_sanitize_on_load(void)
{
    cal_ch_record_t cal;

    prepare_db();
    memset(&cal, 0, sizeof(cal));
    cal.version    = CAL_DATA_VERSION;
    cal.size       = (uint16_t)sizeof(cal);
    cal.valid_mask = (uint8_t)((1U << CAL_DATA_TYPE_THERMOCOUPLE)
                               | (1U << CAL_DATA_TYPE_PT100));
    cal.entry[CAL_DATA_TYPE_THERMOCOUPLE].gain_micro = 1000000L;
    cal.entry[CAL_DATA_TYPE_PT100].gain_micro        = 0L;
    assert(storage_flash_write_cal(0U, &cal));

    (void)storage_load();
    assert(cal_data_valid_mask(0U)
           == (uint8_t)(1U << CAL_DATA_TYPE_THERMOCOUPLE));
}

int main(void)
{
    test_load_valid();
    test_load_missing_and_corrupt();
    test_cal_sanitize_on_load();

    puts("test_storage_flash: PASS");
    return 0;
}
