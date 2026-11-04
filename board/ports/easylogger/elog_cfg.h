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

/** @brief Enable log output. */
#define ELOG_OUTPUT_ENABLE

/** @brief Compile-time maximum output level. */
#define ELOG_OUTPUT_LVL                          ELOG_LVL_DEBUG

/** @brief Enable EasyLogger assertions; log_init() routes them to Q_onError(). */
#define ELOG_ASSERT_ENABLE

/** @brief Per-line buffer size; long lines are truncated. */
#define ELOG_LINE_BUF_SIZE                       256

/** @brief Maximum formatted line-number length. */
#define ELOG_LINE_NUM_MAX_LEN                    5

/** @brief Maximum tag length used by the output filter. */
#define ELOG_FILTER_TAG_MAX_LEN                  30

/** @brief Maximum keyword length used by the output filter. */
#define ELOG_FILTER_KW_MAX_LEN                   16

/** @brief Number of per-tag level filters. */
#define ELOG_FILTER_TAG_LVL_MAX_NUM              5

/** @brief Line terminator appended by EasyLogger. */
#define ELOG_NEWLINE_SIGN                        "\r\n"

/** @brief Color values; unused while ELOG_COLOR_ENABLE is off. */
#define ELOG_COLOR_ASSERT                        (F_MAGENTA B_NULL S_NORMAL)
#define ELOG_COLOR_ERROR                         (F_RED B_NULL S_NORMAL)
#define ELOG_COLOR_WARN                          (F_YELLOW B_NULL S_NORMAL)
#define ELOG_COLOR_INFO                          (F_CYAN B_NULL S_NORMAL)
#define ELOG_COLOR_DEBUG                         (F_GREEN B_NULL S_NORMAL)
#define ELOG_COLOR_VERBOSE                       (F_BLUE B_NULL S_NORMAL)

/** @brief Enable file, function and line fields in formatted output. */
#define ELOG_FMT_USING_FUNC
#define ELOG_FMT_USING_DIR
#define ELOG_FMT_USING_LINE

#endif
