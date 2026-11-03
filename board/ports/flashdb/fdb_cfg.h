/*
 * Copyright (c) 2020, Armink, <armink.ztl@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief FlashDB configuration for analog_collection_08ch_rtu, adapted from
 *        fdb_cfg_template.h: KVDB only, FAL storage on the on-chip flash,
 *        logs through EasyLogger raw output (which ends up on SEGGER RTT).
 */

#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

/* FlashDB logs through FDB_PRINT below, which is elog_raw(); EasyLogger must
 * therefore be in place before this header compiles. */
#include "elog.h"

/* using KVDB feature */
#define FDB_USING_KVDB

#ifdef FDB_USING_KVDB
/* Auto update KV to latest default when current KVDB version number is changed. @see fdb_kvdb.ver_num */
/* left off: enabling fdb_kv_set_default() behaviour belongs to the future
 * parameter module, not to this port. */
/* #define FDB_KV_AUTO_UPDATE */
#endif

/* using TSDB (Time series database) feature */
/* #define FDB_USING_TSDB */

/* Using FAL storage mode */
#define FDB_USING_FAL_MODE

#ifdef FDB_USING_FAL_MODE
/* the flash write granularity, unit: bit
 * only support 1(nor flash)/ 8(stm32f2/f4)/ 32(stm32f1)/ 64(stm32f7)/ 128(stm32h5) */
/* AT32F421 programs 8 bits at a time (flash_byte_program).  Must match
 * fal_flash_dev.write_gran of at32f421_onchip_flash, or fdb_kvdb.c refuses to
 * compile.  Do not copy the 32 of the stm32f1 ports. */
#define FDB_WRITE_GRAN                8
#endif

/* Using file storage mode by LIBC file API, like fopen/fread/fwrte/fclose */
/* #define FDB_USING_FILE_LIBC_MODE */

/* Using file storage mode by POSIX file API, like open/read/write/close */
/* #define FDB_USING_FILE_POSIX_MODE */

/* MCU Endian Configuration, default is Little Endian Order. */
/* #define FDB_BIG_ENDIAN */

/* log print macro. default EF_PRINT macro is printf() */
/* Keep FlashDB's own FDB_LOG_TAG + FDB_DEBUG/INFO/WARN/ERROR macros untouched;
 * only the print channel is redirected: elog_raw() -> SEGGER RTT.
 * elog_raw_output() silently drops while elog.output_enabled is false, so
 * board_init() must call elog_start() before fdb_kvdb_init(). */
#define FDB_PRINT(...)              elog_raw(__VA_ARGS__)

/* print debug information (development builds; FDB_DEBUG carries
 * __FILE__/__LINE__, which raises both ROM and line length -- comment it out
 * for a release build) */
#define FDB_DEBUG_ENABLE

#endif /* _FDB_CFG_H_ */
