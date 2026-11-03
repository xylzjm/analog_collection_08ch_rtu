#include "board.h"
#include "board_selftest.h"
#include "elog.h"

int main(void)
{
    board_init();

#ifdef BOARD_SELFTEST
    uint32_t failed = board_selftest_run();
    /* the plan writes elog_i(); EasyLogger's API for it is elog_info() */
    elog_info("selftest", "0x%08X", (unsigned)failed);
    board_selftest_serve(); /* stays only with the online item enabled */
#endif

    for ( ;; ) {
    }
}
