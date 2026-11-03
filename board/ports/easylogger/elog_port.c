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
 *
 * Function: Portable interface for each platform.
 * Created on: 2015-04-28
 *
 * Adapted for analog_collection_08ch_rtu: the log goes to the SEGGER RTT up
 * channel 0, one whole line per SEGGER_RTT_Write() call.  RTT is configured
 * with SEGGER_RTT_MODE_NO_BLOCK_SKIP, so when no host is reading the up
 * buffer, a line that does not fit is dropped whole.  The lock/unlock pair
 * stays empty: the product runs the QV kernel in a single thread and the
 * discipline is not to log from interrupt context; add the board critical
 * section here when a multi-threaded port needs it.
 */

#include <elog.h>

#include "SEGGER_RTT.h"

/**
 * EasyLogger port initialize
 *
 * @return result
 */
ElogErrCode elog_port_init(void)
{
    ElogErrCode result = ELOG_NO_ERR;

    /* RTT is initialized by board_init(); nothing to do here. */

    return result;
}

/**
 * EasyLogger port deinitialize
 *
 */
void elog_port_deinit(void)
{
}

/**
 * output log port interface
 *
 * @param log output of log
 * @param size log size
 */
void elog_port_output(const char* log, size_t size)
{
    (void)SEGGER_RTT_Write(0U, log, (unsigned)size);
}

/**
 * output lock
 */
void elog_port_output_lock(void)
{
}

/**
 * output unlock
 */
void elog_port_output_unlock(void)
{
}

/**
 * get current time interface
 *
 * @return current time
 */
const char* elog_port_get_time(void)
{
    /* The enabled formats are ELOG_FMT_LVL | ELOG_FMT_TAG only, so no time is
     * ever printed.  When the timestamp comes back into the format, print
     * board_tick_get() into a static buffer with snprintf() here. */
    return "";
}

/**
 * get current process name interface
 *
 * @return current process name
 */
const char* elog_port_get_p_info(void)
{
    return "";
}

/**
 * get current thread name interface
 *
 * @return current thread name
 */
const char* elog_port_get_t_info(void)
{
    return "";
}
