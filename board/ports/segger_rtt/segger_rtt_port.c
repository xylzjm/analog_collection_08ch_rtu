#include <stdio.h>

#include "SEGGER_RTT.h"

/* printf fallback outlet.  Microlib's printf retargets through fputc alone, so
 * this one function is what keeps printf linkable and on the RTT up channel.
 * It is a fallback: the application logs through EasyLogger (elog_port.c),
 * which writes a whole line in one SEGGER_RTT_Write() call, while printf here
 * goes byte by byte and is only meant for third-party code, asserts and
 * ad-hoc debugging.  Keep it out of high-rate paths. */
int fputc(int ch, FILE* f)
{
    char const c = (char)ch;

    (void)f;
    (void)SEGGER_RTT_Write(0U, &c, 1U);
    return ch;
}
