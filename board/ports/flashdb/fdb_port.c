/* FlashDB print port for analog_collection_08ch_rtu.
 *
 * FDB_PRINT (fdb_cfg.h) routes every FlashDB output fragment through
 * fdb_port_print() here.  FlashDB's messages end in a bare '\n' (see
 * fdb_def.h) while EasyLogger lines end in ELOG_NEWLINE_SIGN, and a mixed
 * line-ending stream stair-steps on an RTT terminal; this function converts
 * bare LFs to CRLF and hands the fragment to elog_raw() in one call, so a
 * fragment still reaches the RTT up buffer as a single SEGGER_RTT_Write().
 * Nothing else changes: FlashDB's own FDB_LOG_TAG + FDB_DEBUG/INFO/WARN/ERROR
 * macros stay untouched, and elog_raw() keeps the output-enabled gate (all
 * fragments are dropped until board_init() runs elog_start()).
 *
 * Not reentrant: the product logs from thread level only, exactly like
 * EasyLogger's own line buffer.
 */

#include <stdarg.h>
#include <stdio.h>

#include "elog.h"

#include "fdb_cfg.h"

/**
 * @brief Print one FlashDB fragment with its bare LFs turned into CRLF.
 *
 * @param format printf-style format of the fragment
 */
void fdb_port_print(const char* format, ...)
{
    /* The fragment and its CRLF-converted copy.  The copy is twice the
     * EasyLogger line buffer because every LF may gain a CR; anything longer
     * than ELOG_LINE_BUF_SIZE would be truncated by elog_raw() anyway. */
    static char conv[2 * ELOG_LINE_BUF_SIZE];
    char frag[ELOG_LINE_BUF_SIZE];
    va_list args;
    size_t in;
    size_t out = 0U;
    int len;

    va_start(args, format);
    len = vsnprintf(frag, sizeof(frag), format, args);
    va_end(args);

    if ( len < 0 ) {
        return;
    }
    if ( (size_t)len >= sizeof(frag) ) {
        len = (int)sizeof(frag) - 1;
    }

    for ( in = 0U; (in < (size_t)len) && (out + 2U < sizeof(conv)); ++in ) {
        if ( (frag[in] == '\n') && ((in == 0U) || (frag[in - 1U] != '\r')) ) {
            conv[out++] = '\r';
        }
        conv[out++] = frag[in];
    }
    conv[out] = '\0';

    elog_raw("%s", conv);
}
