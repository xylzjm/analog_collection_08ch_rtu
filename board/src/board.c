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

#include "analog_input_board.h"
#include "i2c_soft_board.h"
#include "sig24131_board.h"
#include "tmp75_board.h"

/*----------------------------------------------------------------------------*/
/* Error handling                                                             */
/*----------------------------------------------------------------------------*/

Q_NORETURN Q_onError(char const* const module,
                     int_t const id)
{
    char line[128];
    char digits[10];
    char const* name = (module != NULL) ? module : "?";
    uint32_t value;
    size_t len = 0U;
    size_t n   = 0U;
    size_t i;
    static char const prefix[] = "Q_onError: ";

    /* Build the line without stdio so this also works from interrupt context. */
    for ( i = 0U; prefix[i] != '\0' && len < sizeof(line) - 16U; ++i ) {
        line[len++] = prefix[i];
    }
    for ( i = 0U; name[i] != '\0' && len < sizeof(line) - 16U; ++i ) {
        line[len++] = name[i];
    }
    line[len++] = ' ';
    line[len++] = '(';

    if ( id < 0 ) {
        line[len++] = '-';
        value       = (uint32_t)(-(id + 1)) + 1U;
    } else {
        value = (uint32_t)id;
    }
    do {
        digits[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while ( value != 0U );
    while ( n > 0U ) {
        line[len++] = digits[--n];
    }

    line[len++] = ')';
    line[len++] = '\r';
    line[len++] = '\n';

    (void)SEGGER_RTT_Write(0U, line, (unsigned)len);

    for ( ;; ) {
    }
}

Q_NORETURN board_crit_error_(int id)
{
    Q_onError("board", id);
}

void HardFault_Handler(void)
{
    /* Never return into the faulting code. */
    Q_onError("HardFault", 0);
}

/*----------------------------------------------------------------------------*/
/* Board critical section                                                     */
/*----------------------------------------------------------------------------*/

/* Shared by QP and board modules; only the board.h macros may touch it. */
uint32_t volatile board_crit_nest;

/* Strong override of qv_port.c's weak, non-nesting versions. */
void QF_crit_entry_(void)
{
    board_critical_enter();
}

void QF_crit_exit_(void)
{
    board_critical_exit();
}

/*----------------------------------------------------------------------------*/
/* Board time                                                                 */
/*----------------------------------------------------------------------------*/

static volatile uint32_t systick_ctr;

uint32_t board_tick_get(void)
{
    return systick_ctr;
}

void SysTick_Handler(void)
{
    ++systick_ctr;
}

/*----------------------------------------------------------------------------*/
/* QP port lifecycle                                                          */
/*----------------------------------------------------------------------------*/

void QF_onCleanup(void)
{
}

void QF_onStartup(void)
{
}

void QV_onIdle(void)
{
}

/*----------------------------------------------------------------------------*/
/* Board bring-up                                                             */
/*----------------------------------------------------------------------------*/

void board_init(void)
{
    sig24131_status_t sig_status;
    analog_input_status_t analog_status;
    i2c_soft_status_t i2c_status;
    tmp75_status_t tmp75_status;

    /* Clock and flash wait states must be configured first. */
    wk_system_clock_config();
    wk_periph_clock_config();

    /* 1 ms tick feeds board_tick_get(); priorities must match the board port. */
    systick_interrupt_config(1000);
    wk_nvic_config();

    /* wk_delay_us/ms and the SIG24131 reset wait depend on this timebase. */
    wk_timebase_init();

    /* GPIO first: DE must be low and chip selects released before the buses start. */
    wk_gpio_config();
    wk_usart1_init();
    wk_spi1_init();
    wk_tmr14_init();

    /* All devices are reset first, then share one 43 ms settling wait. */
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

    /* The SIG24131 reset already waited 43 ms, which also settles the TMP75. */
    i2c_status = i2c_soft_board_init();
    if ( i2c_status != I2C_SOFT_OK ) {
        elog_error("board", "i2c_soft_board_init failed (%d)",
                   (int)i2c_status);
    }
    tmp75_status = tmp75_board_ctor();
    if ( tmp75_status != TMP75_OK ) {
        elog_error("board", "tmp75_board_ctor failed (%d)",
                   (int)tmp75_status);
    }

    /* Probe proves wiring and address only; it carries no temperature. */
    if ( i2c_status == I2C_SOFT_OK ) {
        i2c_status = i2c_soft_probe(i2c_soft_board_get(),
                                    TMP75_BOARD_ADDRESS);
        if ( i2c_status == I2C_SOFT_OK ) {
            elog_info("board", "tmp75 found at 0x%02X",
                      (unsigned)TMP75_BOARD_ADDRESS);
        } else if ( i2c_status == I2C_SOFT_ERR_NACK ) {
            elog_warn("board", "tmp75 not found (%d)", (int)i2c_status);
        } else {
            elog_error("board", "i2c bus unavailable (%d)", (int)i2c_status);
        }
    }

    /* Construction only; the application configures each channel. */
    analog_status = analog_input_board_ctor_all();
    if ( analog_status != ANALOG_INPUT_OK ) {
        elog_error("board", "analog_input_board_ctor_all failed (%d)",
                   (int)analog_status);
    }
}
