/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include "board_config.h"

/** @brief 片上闪存设备。 */
extern const struct fal_flash_dev at32f421_onchip_flash;

/** @brief FAL 闪存设备表。 */
#define FAL_FLASH_DEV_TABLE     \
    {                           \
        &at32f421_onchip_flash, \
    }

/** @brief 分区表与缓存保持静态分配；避免 malloc。 */
#define FAL_PART_HAS_TABLE_CFG

/** @brief 位于 0x0800F000 的参数分区，4 KB。 */
#define FAL_PART_TABLE                                                                           \
    {                                                                                            \
        {FAL_PART_MAGIC_WORD, BOARD_PARAM_PARTITION, "at32f421_onchip", 60 * 1024, 4 * 1024, 0}, \
    }

#endif
