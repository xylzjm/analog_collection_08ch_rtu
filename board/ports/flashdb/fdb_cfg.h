/*
 * Copyright (c) 2020, Armink, <armink.ztl@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

#include "elog.h"
#include "qpc.h"

/** @brief Enable the key-value database. */
#define FDB_USING_KVDB

/** @brief Store the database through FAL. */
#define FDB_USING_FAL_MODE

#ifdef FDB_USING_FAL_MODE
/** @brief Must match fal_flash_dev.write_gran; a mismatch corrupts the KV layout. */
#define FDB_WRITE_GRAN                8
#endif

/** @brief Route FlashDB logs to EasyLogger/RTT. */
#define FDB_PRINT(...)              elog_raw(__VA_ARGS__)

#ifdef Q_UNSAFE
/** @brief Assertions are compiled out together with QP. */
#define FDB_ASSERT(EXPR)            if (0) { (void)(EXPR); }
#else
/** @brief Route FlashDB assertions to the project error handler. */
#define FDB_ASSERT(EXPR)            if (!(EXPR)) { Q_onError("FlashDB", __LINE__); }
#endif

/** @brief Enable FlashDB debug logging; disable in release builds. */
#define FDB_DEBUG_ENABLE

#endif
