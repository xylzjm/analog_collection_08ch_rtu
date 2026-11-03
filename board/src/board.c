#include "qpc.h"

#include "board.h"

#include "at32f421_wk_config.h"
#include "wk_gpio.h"
#include "wk_spi.h"
#include "wk_system.h"
#include "wk_tmr.h"
#include "wk_usart.h"

#include "SEGGER_RTT.h"
#include "elog.h"
#include "fal.h"
#include "flashdb.h"

#include "analog_input_board.h"
#include "sig24131_board.h"

/*----------------------------------------------------------------------------
 * Nestable QF critical sections
 *
 * qv_port.c provides QF_crit_entry_()/QF_crit_exit_() as weak, strictly
 * non-nesting functions that assert the PRIMASK (or BASEPRI) mask is clear on
 * entry and set on exit, and re-enable the interrupts on every exit.  The
 * strong definitions below replace them with the board's nestable critical
 * section (board.h), so a critical section may be entered again from inside
 * another one.  Every caller -- QP's own critical sections included -- shares
 * the one nesting count behind those macros, and only the outermost exit
 * re-enables the interrupts, so the masking behaviour and the rules of the
 * original port (see board.h, same error codes 110/111 as qv_port.c) stay
 * exactly as they were.
 */
/* The one nesting count behind the board.h macros; board.h only declares it
 * inside those macros, so the count is not part of the header's API. */
extern uint32_t volatile board_crit_nest;
uint32_t volatile board_crit_nest;

Q_NORETURN board_crit_error_(int id)
{
    Q_onError("board", id);
}

void QF_crit_entry_(void)
{
    board_critical_enter();
}

void QF_crit_exit_(void)
{
    board_critical_exit();
}

static volatile uint32_t systick_ctr;

uint32_t board_tick_get(void)
{
    return systick_ctr;
}

Q_NORETURN Q_onError(char const* const module,
                     int_t const id)
{
    Q_UNUSED_PAR(module);
    Q_UNUSED_PAR(id);

    for ( ;; ) {
    }
}

void QF_onCleanup(void)
{
}

void QF_onStartup(void)
{
}

void QV_onIdle(void)
{
}

/* Platform bring-up (board_init) ----------------------------------------- */

/* The parameter KVDB: product parameters (Modbus slave address, sensor type,
 * two-point calibration, linear range) live in this one database on the
 * "fdb_kvdb" partition, 0x0800F000..0x0800FFFF.  The instance is static here
 * and handed out by board_kvdb_get(); the parameter module and the selftest
 * are its users. */
static struct fdb_kvdb kvdb;

struct fdb_kvdb* board_kvdb_get(void)
{
    return &kvdb;
}

void board_init(void)
{
    error_status systick_status;
    int fal_status;
    fdb_err_t fdb_status;
    sig24131_status_t sig_status;
    analog_input_status_t analog_status;
    uint8_t level;

    /* 1-2: 120 MHz clock tree with the flash wait cycles, then the
     * peripheral clock gates.  The clock config must be first. */
    wk_system_clock_config();
    wk_periph_clock_config();

    /* 3-4: 1 ms SysTick for board_tick_get(), then the NVIC priorities
     * (USART1 = 5, TMR14 = 5, SysTick = 15). */
    systick_status = systick_interrupt_config(1000);

    wk_nvic_config();

    /* 5: TMR6 1 MHz timebase; wk_delay_us/ms need it and
     * sig24131_board_reset_all() waits 43 ms on it. */
    wk_timebase_init();

    /* 6-9: GPIO first so DE comes up low (bus released) and the chip selects
     * are released before the buses start; then USART1 at 115200 with the
     * receiver still off (the modbus board opens it), SPI1 for the SIG24131
     * bus, and TMR14 configured but not started. */
    wk_gpio_config();
    wk_usart1_init();
    wk_spi1_init();
    wk_tmr14_init();

    if ( systick_status != SUCCESS ) {
        /* EasyLogger is not up yet, so this one can only use the printf
         * fallback outlet (segger_rtt_port.c). */
        printf("board: systick_interrupt_config failed\r\n");
    }

    /* 10: bring the RTT control block up early; every later log and every
     * printf byte goes out through it. */
    SEGGER_RTT_Init();

    /* 11: logging.  elog_set_fmt() must contain ELOG_FMT_LVL on every level,
     * or elog_find_lvl() asserts on the formatted line. */
    if ( elog_init() != ELOG_NO_ERR ) {
        printf("board: elog_init failed\r\n");
    }
    for ( level = ELOG_LVL_ASSERT; level < ELOG_LVL_TOTAL_NUM; ++level ) {
        elog_set_fmt(level, ELOG_FMT_LVL | ELOG_FMT_TAG);
    }
    /* elog_start() before fdb_kvdb_init(): FDB_PRINT is elog_raw() and
     * elog_raw_output() drops everything before the output is enabled. */
    elog_start();

    /* 12: FAL registers the flash device and the parameter partition.  With
     * FAL_PART_HAS_TABLE_CFG it uses static tables only, no malloc. */
    fal_status = fal_init();
    if ( fal_status < 0 ) {
        elog_error("board", "fal_init failed (%d)", fal_status);
    }

    /* 13: the parameter KVDB; the first power-up formats the partition. */
    fdb_status = fdb_kvdb_init(&kvdb, "params", "fdb_kvdb", NULL, NULL);
    if ( fdb_status != FDB_NO_ERR ) {
        elog_error("board", "fdb_kvdb_init failed (%d)", (int)fdb_status);
    }

    /* 14: the SIG24131 driver instances, then one shared 43 ms reset wait
     * for all of them. */
    sig_status = sig24131_board_ctor_all();
    if ( sig_status != SIG24131_OK ) {
        elog_error("board", "sig24131_board_ctor_all failed (%d)",
                   (int)sig_status);
    }
    sig_status = sig24131_board_reset_all();
    if ( sig_status != SIG24131_OK ) {
        elog_error("board", "sig24131_board_reset_all failed (%d)",
                   (int)sig_status);
    }

    /* 15: the analog input objects.  This only builds RAM objects; values
     * appear once the application configures each channel.  A failing step
     * above is logged, never raised: the selftest reports the end state. */
    analog_status = analog_input_board_ctor_all();
    if ( analog_status != ANALOG_INPUT_OK ) {
        elog_error("board", "analog_input_board_ctor_all failed (%d)",
                   (int)analog_status);
    }
}

void HardFault_Handler(void)
{
}

void SysTick_Handler(void)
{
    ++systick_ctr;
}
