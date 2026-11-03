/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-05-17     armink       the first version
 *
 * Adapted for analog_collection_08ch_rtu: one on-chip flash device and one
 * parameter partition at the top of the 64 KB flash.  No rtconfig.h/board.h:
 * this is a bare-metal build.
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

/* ===================== Flash device Configuration ========================= */
extern const struct fal_flash_dev at32f421_onchip_flash;

/* flash device table */
#define FAL_FLASH_DEV_TABLE                                          \
{                                                                    \
    &at32f421_onchip_flash,                                          \
}

/* ====================== Partition Configuration ========================== */
/* With FAL_PART_HAS_TABLE_CFG fal_partition.c keeps the partition table and
 * its flash-info cache in static storage and never calls malloc, which matters
 * because microlib's heap is only 512 B. */
#define FAL_PART_HAS_TABLE_CFG

/* partition table
 * fdb_kvdb: product parameters (Modbus slave address, sensor type, two-point
 * calibration, linear range), the 4 KB the firmware's ROM region leaves out:
 * 60 KB offset, 4 KB length = 0x0800F000..0x0800FFFF. */
#define FAL_PART_TABLE                                                               \
{                                                                                    \
    {FAL_PART_MAGIC_WORD, "fdb_kvdb", "at32f421_onchip", 60*1024, 4*1024, 0}, \
}

#endif /* _FAL_CFG_H_ */
