/*
 * This file is part of the EasyLogger Library.
 *
 * Copyright (c) 2015-2016, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _ELOG_CFG_H_
#define _ELOG_CFG_H_

/** @brief 启用日志输出。 */
#define ELOG_OUTPUT_ENABLE

/** @brief 编译期最大输出级别。 */
#define ELOG_OUTPUT_LVL ELOG_LVL_DEBUG

/** @brief 启用 EasyLogger 断言；log_init() 将其路由到 Q_onError()。 */
#define ELOG_ASSERT_ENABLE

/** @brief 单行缓冲区大小；超长行会被截断。 */
#define ELOG_LINE_BUF_SIZE 256

/** @brief 格式化行号的最大长度。 */
#define ELOG_LINE_NUM_MAX_LEN 5

/** @brief 输出过滤器使用的最大标签长度。 */
#define ELOG_FILTER_TAG_MAX_LEN 30

/** @brief 输出过滤器使用的最大关键字长度。 */
#define ELOG_FILTER_KW_MAX_LEN 16

/** @brief 按标签划分的级别过滤器数量。 */
#define ELOG_FILTER_TAG_LVL_MAX_NUM 5

/** @brief EasyLogger 追加的行结束符。 */
#define ELOG_NEWLINE_SIGN "\r\n"

/** @brief 颜色值；ELOG_COLOR_ENABLE 关闭时未使用。 */
#define ELOG_COLOR_ASSERT  (F_MAGENTA B_NULL S_NORMAL)
#define ELOG_COLOR_ERROR   (F_RED B_NULL S_NORMAL)
#define ELOG_COLOR_WARN    (F_YELLOW B_NULL S_NORMAL)
#define ELOG_COLOR_INFO    (F_CYAN B_NULL S_NORMAL)
#define ELOG_COLOR_DEBUG   (F_GREEN B_NULL S_NORMAL)
#define ELOG_COLOR_VERBOSE (F_BLUE B_NULL S_NORMAL)

/** @brief 在格式化输出中启用文件、函数和行号字段。 */
#define ELOG_FMT_USING_FUNC
#define ELOG_FMT_USING_DIR
#define ELOG_FMT_USING_LINE

#endif
