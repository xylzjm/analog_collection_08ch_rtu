/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

/** @brief On-chip flash device. */
extern const struct fal_flash_dev at32f421_onchip_flash;

/** @brief FAL flash device table. */
#define FAL_FLASH_DEV_TABLE                                          \
{                                                                    \
    &at32f421_onchip_flash,                                          \
}

/** @brief Keep the partition table and cache static; avoids malloc. */
#define FAL_PART_HAS_TABLE_CFG

/** @brief Parameter partition at 0x0800F000, 4 KB. */
#define FAL_PART_TABLE                                                               \
{                                                                                    \
    {FAL_PART_MAGIC_WORD, "fdb_kvdb", "at32f421_onchip", 60*1024, 4*1024, 0}, \
}

#endif
