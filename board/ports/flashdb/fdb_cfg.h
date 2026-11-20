/*
 * Copyright (c) 2020, Armink, <armink.ztl@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

#include "elog.h"
#include "qpc.h"

/** @brief 启用键值数据库。 */
#define FDB_USING_KVDB

/* FDB_KV_AUTO_UPDATE 必须保持未定义：b2 缺失键故障路径依赖 FlashDB 永不回填已有存储。 */

/** @brief 通过 FAL 存放数据库。 */
#define FDB_USING_FAL_MODE

#ifdef FDB_USING_FAL_MODE
/** @brief 写入粒度；由 FAL 移植层消费，因此是单一来源（不一致会损坏 KV 布局）。 */
#define FDB_WRITE_GRAN 8
#endif

/** @brief 将 FlashDB 日志路由到 EasyLogger/RTT。 */
#define FDB_PRINT(...) elog_raw(__VA_ARGS__)

#ifdef Q_UNSAFE
/** @brief 断言随 QP 一同被编译剔除。 */
#define FDB_ASSERT(EXPR) \
    if ( 0 ) {           \
        (void)(EXPR);    \
    }
#else
/** @brief 将 FlashDB 断言路由到项目错误处理函数。 */
#define FDB_ASSERT(EXPR)                \
    if ( !(EXPR) ) {                    \
        Q_onError("FlashDB", __LINE__); \
    }
#endif

/** @brief 启用 FlashDB 调试日志；发布版本中应关闭。 */
#define FDB_DEBUG_ENABLE

#endif
