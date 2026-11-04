/*
 * This file is part of the EasyLogger Library.
 *
 * Copyright (c) 2015, Armink, <armink.ztl@gmail.com>
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

#include <elog.h>

#include "SEGGER_RTT.h"

ElogErrCode elog_port_init(void)
{
    ElogErrCode result = ELOG_NO_ERR;

    /* RTT is initialized by log_init() before elog_init(). */
    return result;
}

void elog_port_deinit(void)
{
}

void elog_port_output(const char* log, size_t size)
{
    /* NO_BLOCK_SKIP drops the whole line when the up buffer has no room. */
    (void)SEGGER_RTT_Write(0U, log, (unsigned)size);
}

void elog_port_output_lock(void)
{
    /* Empty by design: single-threaded use, no logging from interrupt context. */
}

void elog_port_output_unlock(void)
{
}

const char* elog_port_get_time(void)
{
    /* Time is not part of the enabled output format. */
    return "";
}

const char* elog_port_get_p_info(void)
{
    return "";
}

const char* elog_port_get_t_info(void)
{
    return "";
}
