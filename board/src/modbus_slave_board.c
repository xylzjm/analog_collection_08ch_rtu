#include "modbus_slave_board.h"

#include "at32f421_wk_config.h"

#include "board.h"

#include "qpc.h"

/** @brief TMR14 分辨率与 16 位周期上限。 */
#define MB_TMR_TICK_US   10U
#define MB_TMR_MAX_TICKS 65535U

/** @brief Modbus 收/发缓冲区大小。 */
#define MB_BUFFER_SIZE 256U

/** @brief RS485 线路绑定。 */
typedef struct {
    usart_type* uart;
    gpio_type* de_port;
    uint16_t de_pin;
} line_ctx_t;

/** @brief T3.5 / 发送看门狗定时器绑定。 */
typedef struct {
    tmr_type* timer;
} timer_ctx_t;

static const line_ctx_t line_ctx = {
    USART1,
    USART_DE_GPIO_PORT,
    USART_DE_PIN,
};

static const timer_ctx_t timer_ctx = {
    TMR14,
};

static uint8_t rx_buffer[MB_BUFFER_SIZE];
static uint8_t tx_buffer[MB_BUFFER_SIZE];

static modbus_slave_t slave;

static modbus_slave_status_t line_direction(void* ctx,
                                            uint8_t rx,
                                            uint8_t tx)
{
    const line_ctx_t* b = (const line_ctx_t*)ctx;

    /*
     * 无论 wk_usart1_init() 留下何种外设状态，本端口都掌管接收窗口：
     * RDBF 在第一次 direction(1, 0) 之前保持屏蔽，且核心在会话停止
     * 期间丢弃每个字节，因此启动前的线路噪声无害。
     */

    if ( rx != 0U && tx != 0U ) {
        return MODBUS_SLAVE_ERR_ARG;
    }

    if ( rx != 0U ) {
        /* 先静默发送，再释放 DE：TC 表示最后一位已发出。 */
        usart_interrupt_enable(b->uart, USART_TDBE_INT, FALSE);
        usart_interrupt_enable(b->uart, USART_TDC_INT, FALSE);
        usart_flag_clear(b->uart, USART_TDC_FLAG);
        usart_receiver_enable(b->uart, TRUE);
        usart_interrupt_enable(b->uart, USART_RDBF_INT, TRUE);
        gpio_bits_reset(b->de_port, b->de_pin);
    } else if ( tx != 0U ) {
        /* 在使能 TXE 前拉高 DE，避免首字节在 DE 为低时被驱动。 */
        usart_interrupt_enable(b->uart, USART_RDBF_INT, FALSE);
        usart_receiver_enable(b->uart, FALSE);
        gpio_bits_set(b->de_port, b->de_pin);
        usart_flag_clear(b->uart, USART_TDC_FLAG);
        usart_interrupt_enable(b->uart, USART_TDC_INT, TRUE);
        usart_interrupt_enable(b->uart, USART_TDBE_INT, TRUE);
    } else {

        usart_interrupt_enable(b->uart, USART_RDBF_INT, FALSE);
        usart_receiver_enable(b->uart, FALSE);
        usart_interrupt_enable(b->uart, USART_TDBE_INT, FALSE);
        usart_interrupt_enable(b->uart, USART_TDC_INT, FALSE);
        usart_flag_clear(b->uart, USART_TDC_FLAG);
        gpio_bits_reset(b->de_port, b->de_pin);
    }

    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t line_put_byte(void* ctx, uint8_t byte)
{
    const line_ctx_t* b = (const line_ctx_t*)ctx;

    usart_data_transmit(b->uart, (uint16_t)byte);
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t line_tx_done(void* ctx)
{
    const line_ctx_t* b = (const line_ctx_t*)ctx;

    usart_interrupt_enable(b->uart, USART_TDBE_INT, FALSE);
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t timer_arm(void* ctx, uint32_t timeout_us)
{
    const timer_ctx_t* t = (const timer_ctx_t*)ctx;
    uint32_t ticks;

    /*
     * 定时器遵循同样的归属规则：无论 wk_tmr14_init() 留下何种状态
     * （单周期模式下计数器自行停止），本入口都会先停止并清除再重新
     * 编程，因此不会有残留的到期事件触发。
     */

    /* 钳制到 16 位周期；核心的超时预期都能容纳。 */
    ticks = (timeout_us + MB_TMR_TICK_US - 1U) / MB_TMR_TICK_US;
    if ( ticks == 0U ) {
        ticks = 1U;
    }
    if ( ticks > MB_TMR_MAX_TICKS ) {
        ticks = MB_TMR_MAX_TICKS;
    }

    tmr_interrupt_enable(t->timer, TMR_OVF_INT, FALSE);
    tmr_counter_enable(t->timer, FALSE);
    tmr_period_value_set(t->timer, ticks - 1U);
    tmr_counter_value_set(t->timer, 0U);
    tmr_flag_clear(t->timer, TMR_OVF_FLAG);
    tmr_interrupt_enable(t->timer, TMR_OVF_INT, TRUE);
    tmr_counter_enable(t->timer, TRUE);
    return MODBUS_SLAVE_OK;
}

static modbus_slave_status_t timer_cancel(void* ctx)
{
    const timer_ctx_t* t = (const timer_ctx_t*)ctx;

    tmr_counter_enable(t->timer, FALSE);
    tmr_interrupt_enable(t->timer, TMR_OVF_INT, FALSE);
    tmr_flag_clear(t->timer, TMR_OVF_FLAG);
    return MODBUS_SLAVE_OK;
}

/* 与 QP 共享嵌套计数；见 board.h。 */
static void critical_enter(void* ctx)
{
    (void)ctx;
    board_critical_enter();
}

static void critical_exit(void* ctx)
{
    (void)ctx;
    board_critical_exit();
}

/** @brief 交给 AO 的构造配置平台部分：线路、定时器与临界区端口，
 *  收发缓冲区以及波特率。回调与身份保持清零；AO 在
 *  modbus_slave_ctor() 之前覆盖它们。
 */
/*
 * 指定初始化器：库中 modbus_slave_config_t 的字段重排不得
 * 让本表被静默地重新绑定。
 */
static const modbus_slave_config_t slave_config = {
    .baudrate    = BOARD_MODBUS_BAUDRATE,
    .rx_buffer   = rx_buffer,
    .rx_capacity = (uint16_t)MB_BUFFER_SIZE,
    .tx_buffer   = tx_buffer,
    .tx_capacity = (uint16_t)MB_BUFFER_SIZE,
    .line        = {
        .ctx       = (void*)&line_ctx,
        .direction = line_direction,
        .put_byte  = line_put_byte,
        .tx_done   = line_tx_done,
    },
    .timer = {
        .ctx    = (void*)&timer_ctx,
        .arm    = timer_arm,
        .cancel = timer_cancel,
    },
    .critical = {
        .ctx   = NULL,
        .enter = critical_enter,
        .exit  = critical_exit,
    },
};

const modbus_slave_config_t* modbus_slave_board_config(void)
{
    return &slave_config;
}

modbus_slave_t* modbus_slave_board_get(void)
{
    return &slave;
}

void USART1_IRQHandler(void)
{
    /* 先处理 RDBF：清除错误标志会读取 sts+dt，从而吞掉该字节。 */
    if ( usart_interrupt_flag_get(USART1, USART_RDBF_FLAG) != RESET ) {
        uint16_t data = usart_data_receive(USART1);

        (void)modbus_slave_rx_byte(&slave, (uint8_t)data);
    }

    if ( usart_interrupt_flag_get(USART1, USART_TDBE_FLAG) != RESET ) {

        (void)modbus_slave_tx_ready(&slave);
    }

    if ( usart_interrupt_flag_get(USART1, USART_TDC_FLAG) != RESET ) {
        usart_flag_clear(USART1, USART_TDC_FLAG);
        (void)modbus_slave_tx_complete(&slave);
    }

    {
        uint32_t errors = 0U;

        if ( usart_flag_get(USART1, USART_NERR_FLAG) != RESET ) {
            errors |= USART_NERR_FLAG;
        }
        if ( usart_flag_get(USART1, USART_ROERR_FLAG) != RESET ) {
            errors |= USART_ROERR_FLAG;
        }
        if ( usart_flag_get(USART1, USART_FERR_FLAG) != RESET ) {
            errors |= USART_FERR_FLAG;
        }
        if ( usart_flag_get(USART1, USART_PERR_FLAG) != RESET ) {
            errors |= USART_PERR_FLAG;
        }
        if ( errors != 0U ) {
            usart_flag_clear(USART1, errors);
        }
    }

    /* Cortex-M4 勘误 838869 的规避措施：ISR 退出前执行 DSB。 */
    QV_ARM_ERRATUM_838869();
}

void TMR14_GLOBAL_IRQHandler(void)
{
    if ( tmr_flag_get(TMR14, TMR_OVF_FLAG) != RESET ) {
        tmr_flag_clear(TMR14, TMR_OVF_FLAG);
        (void)modbus_slave_timer_expired(&slave);
    }

    /* Cortex-M4 勘误 838869 的规避措施：ISR 退出前执行 DSB。 */
    QV_ARM_ERRATUM_838869();
}
