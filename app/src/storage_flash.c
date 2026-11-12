#include "storage_flash.h"

#include <string.h>

#include "fal.h"

/* FAL and EasyLogger both define generic log_* macros; keep EasyLogger's. */
#undef log_d
#undef log_i
#undef log_e
#include "flashdb.h"

#include "modbus_app_regs.h"
#include "param_app.h"

static struct fdb_kvdb kvdb;
static bool db_ready;

static void make_param_key(uint8_t key, char* name)
{
    if ( key == 0U ) {
        name[0] = 's';
        name[1] = 'y';
        name[2] = 's';
        name[3] = '\0';
    } else {
        name[0] = 'c';
        name[1] = 'h';
        name[2] = (char)('0' + (key - 1U));
        name[3] = '\0';
    }
}

static void make_cal_key(uint8_t channel, char* name)
{
    name[0] = 'c';
    name[1] = 'a';
    name[2] = 'l';
    name[3] = (char)('0' + channel);
    name[4] = '\0';
}

static bool read_blob(const char* key, void* data, uint16_t capacity, uint16_t* len)
{
    struct fdb_blob blob;
    size_t size;

    if ( db_ready == false ) {
        return false;
    }

    (void)fdb_blob_make(&blob, data, capacity);
    size = fdb_kv_get_blob(&kvdb, key, &blob);
    if ( size == 0U || size > capacity ) {
        return false;
    }
    *len = (uint16_t)size;
    return true;
}

static bool write_blob(const char* key, const void* data, uint16_t len)
{
    struct fdb_blob blob;

    if ( db_ready == false ) {
        return false;
    }

    (void)fdb_blob_make(&blob, data, len);
    return (fdb_kv_set_blob(&kvdb, key, &blob) == FDB_NO_ERR) ? true : false;
}

static bool sys_valid(const param_sys_record_t* record)
{
    return (record->version == PARAM_APP_VERSION
            && record->size == (uint16_t)sizeof(*record)
            && record->address >= 1U && record->address <= 247U)
               ? true
               : false;
}

static bool channel_valid(const param_ch_record_t* record)
{
    return (record->version == PARAM_APP_VERSION
            && record->size == (uint16_t)sizeof(*record)
            && modbus_app_regs_type_valid(record->type)
            && modbus_app_regs_rate_valid(record->rate_index)
            && modbus_app_regs_gain_valid(record->user_gain)
            && modbus_app_regs_offset_valid(record->user_offset))
               ? true
               : false;
}

bool storage_load(void)
{
    union {
        param_sys_record_t sys;
        param_ch_record_t ch;
    } param;
    cal_ch_record_t cal;
    char key[5];
    uint16_t len;
    uint8_t channel;
    bool fault = false;

    if ( fal_init() < 1 ) {
        return true;
    }
    if ( fdb_kvdb_init(&kvdb, "env", "fdb_kvdb", NULL, NULL) != FDB_NO_ERR ) {
        return true;
    }
    db_ready = true;

    make_param_key(0U, key);
    if ( read_blob(key, &param.sys, (uint16_t)sizeof(param.sys), &len) == false
         || len != sizeof(param.sys) || sys_valid(&param.sys) == false ) {
        fault = true;
    } else {
        (void)param_app_restore_sys(&param.sys);
    }

    for ( channel = 0U; channel < PARAM_APP_CHANNEL_COUNT; ++channel ) {
        make_param_key((uint8_t)(channel + 1U), key);
        if ( read_blob(key, &param.ch, (uint16_t)sizeof(param.ch), &len) == false
             || len != sizeof(param.ch)
             || channel_valid(&param.ch) == false ) {
            fault = true;
        } else {
            (void)param_app_restore_channel(channel, &param.ch);
        }
    }

    for ( channel = 0U; channel < CAL_DATA_CHANNEL_COUNT; ++channel ) {
        make_cal_key(channel, key);
        if ( read_blob(key, &cal, (uint16_t)sizeof(cal), &len) == false
             || len != sizeof(cal)
             || cal_data_record_sanitize(&cal) == false ) {
            continue;
        }
        (void)cal_data_set(channel, &cal);
    }

    return fault;
}

bool storage_flash_write_param(uint8_t key, const void* data, uint16_t len)
{
    char name[4];

    if ( key > PARAM_APP_CHANNEL_COUNT || data == NULL ) {
        return false;
    }
    make_param_key(key, name);
    return write_blob(name, data, len);
}

bool storage_flash_write_cal(uint8_t channel, const cal_ch_record_t* record)
{
    char name[5];

    if ( channel >= CAL_DATA_CHANNEL_COUNT || record == NULL ) {
        return false;
    }
    make_cal_key(channel, name);
    return write_blob(name, record, (uint16_t)sizeof(*record));
}
