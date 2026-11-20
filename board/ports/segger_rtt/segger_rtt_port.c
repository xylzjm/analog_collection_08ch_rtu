#include <stdio.h>

#include "SEGGER_RTT.h"

int fputc(int ch, FILE* f)
{
    /* 仅为兜底路径：printf 每次调用只写入一个字节。 */
    char const c = (char)ch;

    (void)f;
    (void)SEGGER_RTT_Write(0U, &c, 1U);
    return ch;
}
