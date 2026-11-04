#include <stdio.h>

#include "SEGGER_RTT.h"

int fputc(int ch, FILE* f)
{
    /* Fallback path only: printf writes one byte per call. */
    char const c = (char)ch;

    (void)f;
    (void)SEGGER_RTT_Write(0U, &c, 1U);
    return ch;
}
