#ifndef __BOARD_H__
#define __BOARD_H__

#include <stdint.h>

/* Host-test stub: the real board header depends on ARM intrinsics. */
uint32_t board_tick_get(void);
#define board_critical_enter() \
    do {                       \
    } while ( 0 )
#define board_critical_exit() \
    do {                      \
    } while ( 0 )

#endif
