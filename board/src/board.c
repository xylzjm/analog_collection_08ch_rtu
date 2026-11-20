#include "board.h"

#include "qpc.h"

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
/* 错误处理                                                                   */
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

    /* 不借助 stdio 构造该行，因而在中断上下文中同样可用。 */
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
    /* 绝不返回到出错的代码中。 */
    Q_onError("HardFault", 0);
}

/*----------------------------------------------------------------------------*/
/* 板级临界区                                                                 */
/*----------------------------------------------------------------------------*/

/* 由 QP 与板级模块共享；只有 board.h 的宏可以访问它。 */
uint32_t volatile board_crit_nest;

/* 对 qv_port.c 中弱定义、不可嵌套版本的强覆盖。 */
void QF_crit_entry_(void)
{
    board_critical_enter();
}

void QF_crit_exit_(void)
{
    board_critical_exit();
}

/*----------------------------------------------------------------------------*/
/* 板级时间                                                                   */
/*----------------------------------------------------------------------------*/

static volatile uint32_t systick_ctr;

uint32_t board_tick_get(void)
{
    return systick_ctr;
}

void SysTick_Handler(void)
{
    ++systick_ctr;

    /* 供 QP 时间事件使用的 1 ms 时钟节拍（rate 0）。 */
    QTIMEEVT_TICK_X(0U, (void*)0);

    /* Cortex-M4 勘误 838869 的规避措施：ISR 退出前执行 DSB。 */
    QV_ARM_ERRATUM_838869();
}

/*----------------------------------------------------------------------------*/
/* QP 端口生命周期                                                            */
/*----------------------------------------------------------------------------*/

void QF_onStartup(void)
{
    /* 1 ms 节拍为 board_tick_get() 提供时基；优先级必须与板级端口一致。 */
    systick_interrupt_config(1000);
    wk_nvic_config();
}

void QF_onCleanup(void)
{
}

void QV_onIdle(void)
{
    /* 进入时中断已关闭；QV 内核在本回调返回后会立即再次关闭中断，
     * 因此这里必须让中断保持开启。 */
    QF_INT_ENABLE();
}

/*----------------------------------------------------------------------------*/
/* 板级初始化                                                                 */
/*----------------------------------------------------------------------------*/

/** @brief board_init 末尾等待 TMP75 上电稳定的时间（ms）。 */
#define TMP75_SETTLE_WAIT_MS 300U

void board_init(void)
{
    sig24131_status_t sig_status;
    analog_input_status_t analog_status;
    i2c_soft_status_t i2c_status;
    tmp75_status_t tmp75_status;

    /* 必须先配置时钟与 Flash 等待周期。 */
    wk_system_clock_config();
    wk_periph_clock_config();

    /* wk_delay_us/ms 与 SIG24131 复位等待都依赖该时基。 */
    wk_timebase_init();

    /* 先配置 GPIO：总线启动前 DE 必须为低且片选已释放。 */
    wk_gpio_config();
    wk_usart1_init();
    wk_spi1_init();
    wk_tmr14_init();

    /* 先复位所有器件，然后共用一次 43 ms 稳定等待。 */
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

    /* SIG24131 复位已等待 43 ms，这同时让 TMP75 稳定。 */
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

    /* 探测仅验证接线与地址；不携带温度。 */
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

    /* 仅完成构造；各通道由应用层配置。 */
    analog_status = analog_input_board_ctor_all();
    if ( analog_status != ANALOG_INPUT_OK ) {
        elog_error("board", "analog_input_board_ctor_all failed (%d)",
                   (int)analog_status);
    }

    wk_delay_ms(TMP75_SETTLE_WAIT_MS);
}
