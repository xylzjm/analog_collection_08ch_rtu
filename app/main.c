#include <stdio.h>

#include "board.h"
#include "qpc.h"
#include "SEGGER_RTT.h"
#include "elog.h"

/** @brief Route EasyLogger assertions to the project error handler. */
static void elog_assert_handler(char const* expr,
                                char const* func,
                                size_t line)
{
    (void)expr;
    (void)func;
    Q_onError("EasyLogger", (int_t)line);
}

/** @brief Bring up SEGGER RTT and EasyLogger before any logging. */
static void log_init(void)
{
    uint8_t level;

    /* RTT must exist before EasyLogger and the printf fallback are used. */
    SEGGER_RTT_Init();

    if ( elog_init() != ELOG_NO_ERR ) {
        printf("log: elog_init failed\r\n");
        return;
    }

    elog_assert_set_hook(elog_assert_handler);

    /* Every level needs ELOG_FMT_LVL; elog_find_lvl() asserts otherwise. */
    for ( level = ELOG_LVL_ASSERT; level < ELOG_LVL_TOTAL_NUM; ++level ) {
        elog_set_fmt(level, ELOG_FMT_LVL | ELOG_FMT_TAG);
    }
    elog_start();
}

int main(void)
{
    log_init();
    board_init();

    for ( ;; ) {
    }
}
