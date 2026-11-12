#ifndef STORAGE_FLASH_STUB_H
#define STORAGE_FLASH_STUB_H

#include <stdbool.h>
#include <stdint.h>

#include "storage_flash.h"

extern uint32_t stub_param_writes;
extern uint32_t stub_cal_writes;
extern uint8_t stub_last_param_key;
extern uint8_t stub_last_cal_channel;
extern cal_ch_record_t stub_last_cal_record;
extern bool stub_storage_fail;

/** @brief Reset write counters and the simulated failure flag. */
void stub_storage_reset(void);

#endif
