#include "storage_flash_stub.h"

#include <string.h>

uint32_t stub_param_writes;
uint32_t stub_cal_writes;
uint8_t stub_last_param_key;
uint8_t stub_last_cal_channel;
cal_ch_record_t stub_last_cal_record;
bool stub_storage_fail;

void stub_storage_reset(void)
{
    stub_param_writes   = 0U;
    stub_cal_writes     = 0U;
    stub_last_param_key = 0xFFU;
    stub_last_cal_channel = 0xFFU;
    memset(&stub_last_cal_record, 0, sizeof(stub_last_cal_record));
    stub_storage_fail   = false;
}

bool storage_flash_write_param(uint8_t key,
                               const void* data,
                               uint16_t len)
{
    (void)data;
    (void)len;

    ++stub_param_writes;
    stub_last_param_key = key;
    return (stub_storage_fail == false) ? true : false;
}

bool storage_flash_write_cal(uint8_t channel,
                             const cal_ch_record_t* record)
{
    if ( record != NULL ) {
        stub_last_cal_record = *record;
    }

    ++stub_cal_writes;
    stub_last_cal_channel = channel;
    return (stub_storage_fail == false) ? true : false;
}

bool storage_load(void)
{
    return false;
}
