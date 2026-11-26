/**
 * @file channel_ao.c
 * @brief 通道采集活动对象状态机（QHsm，手写）。
 *
 * 改配在结构上被隔离（docs/ao/channel_ao.md 第 6 节）：CONFIG_SET_SIG 先
 * 撤防定时器、缓存待应用配置、投递 APPLY_SIG 并转入 Unconfigured；真正的
 * configure() 在下一个派发中执行，因此任何迟到的定时器事件都会落入
 * Unconfigured 并在那里被忽略。
 */

#include "channel_ao.h"

#include "analog_input_board.h"
#include "elog.h"

Q_DEFINE_THIS_MODULE("channel_ao")

/** @brief 改配重入事件；由全部八个实例共享。 */
static QEvt const applyEvt = QEVT_INITIALIZER(APPLY_SIG);

/* 通道类型码必须与 analog_input_type_t 的编码一致。 */
Q_ASSERT_STATIC((int)CH_TYPE_TC == (int)ANALOG_INPUT_TYPE_THERMOCOUPLE);
Q_ASSERT_STATIC((int)CH_TYPE_PT100 == (int)ANALOG_INPUT_TYPE_PT100);
Q_ASSERT_STATIC((int)CH_TYPE_4_20MA == (int)ANALOG_INPUT_TYPE_4_20MA);
Q_ASSERT_STATIC((int)CH_TYPE_0_10V == (int)ANALOG_INPUT_TYPE_0_10V);

/* 热电偶分度类型码同样必须与驱动编码一致。“无”哨兵值有意不同
 * （CH_TC_TYPE_NONE 是寄存器编码的 0xFF，驱动用 -1），由 apply_pending()
 * 转换。 */
Q_ASSERT_STATIC((int)CH_TC_TYPE_B == (int)ANALOG_INPUT_THERMOCOUPLE_TYPE_B);
Q_ASSERT_STATIC((int)CH_TC_TYPE_E == (int)ANALOG_INPUT_THERMOCOUPLE_TYPE_E);
Q_ASSERT_STATIC((int)CH_TC_TYPE_J == (int)ANALOG_INPUT_THERMOCOUPLE_TYPE_J);
Q_ASSERT_STATIC((int)CH_TC_TYPE_K == (int)ANALOG_INPUT_THERMOCOUPLE_TYPE_K);
Q_ASSERT_STATIC((int)CH_TC_TYPE_N == (int)ANALOG_INPUT_THERMOCOUPLE_TYPE_N);
Q_ASSERT_STATIC((int)CH_TC_TYPE_R == (int)ANALOG_INPUT_THERMOCOUPLE_TYPE_R);
Q_ASSERT_STATIC((int)CH_TC_TYPE_S == (int)ANALOG_INPUT_THERMOCOUPLE_TYPE_S);
Q_ASSERT_STATIC((int)CH_TC_TYPE_T == (int)ANALOG_INPUT_THERMOCOUPLE_TYPE_T);

/** @brief 各速率码对应的采样速率，单位 mHz（板级档位表）。 */
static const uint32_t rate_millihz_table[CH_CONFIG_RATE_CODE_COUNT] = {
#define X(rate_name, millihertz) millihertz,
    APP_RATE_LIST(X)
#undef X
};

static QState channel_ao_initial(channel_ao_t* const me, void const* const par);
static QState channel_ao_root(channel_ao_t* const me, QEvt const* const e);
static QState channel_ao_unconfigured(channel_ao_t* const me, QEvt const* const e);
static QState channel_ao_working(channel_ao_t* const me, QEvt const* const e);
static QState channel_ao_detecting(channel_ao_t* const me, QEvt const* const e);
static QState channel_ao_settling(channel_ao_t* const me, QEvt const* const e);
static QState channel_ao_measuring(channel_ao_t* const me, QEvt const* const e);

/**
 * @brief 判断该输入类型是否走断线检测循环。
 */
static bool detectable(const channel_ao_t* me)
{
    return (me->cfg.type == (uint8_t)CH_TYPE_TC)
           || (me->cfg.type == (uint8_t)CH_TYPE_PT100);
}

/**
 * @brief 把断线检查结果映射为通道状态码。
 *
 * 断线状态参数使用 analog_input_wire_status_t 编码。
 *
 * @return 正常返回 0；热电偶断线一律返回 2；PT100 可分辨断线返回 3/4；
 *         PT100 不可分辨断线（前端契约违约）、不可检测的类型或未知断线码
 *         返回 6。
 */
static uint16_t wire_status_code(uint8_t cfg_type,
                                 uint8_t wire_status)
{
    if ( wire_status == (uint8_t)ANALOG_INPUT_WIRE_NORMAL ) {
        return (uint16_t)CH_STATUS_OK;
    }

    if ( cfg_type == (uint8_t)CH_TYPE_TC ) {
        /* 热电偶回路无法报告是哪根线开路。 */
        return (uint16_t)CH_STATUS_TC_BREAK;
    }

    if ( cfg_type == (uint8_t)CH_TYPE_PT100 ) {
        if ( wire_status == (uint8_t)ANALOG_INPUT_WIRE_BROKEN_LEAD ) {
            return (uint16_t)CH_STATUS_RTD_LEAD_BREAK;
        }
        if ( wire_status == (uint8_t)ANALOG_INPUT_WIRE_BROKEN_RETURN ) {
            return (uint16_t)CH_STATUS_RTD_RETURN_BREAK;
        }
    }

    /* PT100 不可分辨断线或不可检测类型：前端契约违约。 */
    return (uint16_t)CH_STATUS_HW_ERROR;
}

/** @brief 速率码对应的采样速率，单位毫赫兹（无效码返回 0）。 */
static uint32_t rate_millihz(uint16_t code)
{
    if ( code > CH_CONFIG_RATE_CODE_MAX ) {
        return 0U;
    }
    return rate_millihz_table[code];
}

/**
 * @brief 速率码对应的采样周期，单位 1 ms tick：ceil(1e6 / rate_mhz)。
 *
 * 向上取整使实际速率不高于标称速率。
 */
static uint32_t rate_ticks(uint16_t code)
{
    uint32_t mhz = rate_millihz(code);

    if ( mhz == 0U ) {
        return 0U;
    }
    return (1000000U + mhz - 1U) / mhz;
}

/**
 * @brief 发布一条 MEASURE_SIG 事件（故障码时值为冻结值）。
 */
static void publish(channel_ao_t* me, float value, uint16_t status)
{
    measure_evt_t* evt = Q_NEW(measure_evt_t, MEASURE_SIG);

    evt->ch     = me->ch;
    evt->value  = value;
    evt->status = status;
    QACTIVE_PUBLISH(&evt->super, Q_ACTIVE_UPCAST(me));
}

/**
 * @brief 为本通道发布一条 CFG_APPLIED_SIG 接受事件。
 */
static void publish_applied(channel_ao_t* me)
{
    cfg_applied_evt_t* evt = Q_NEW(cfg_applied_evt_t, CFG_APPLIED_SIG);

    evt->ch = me->ch;
    QACTIVE_PUBLISH(&evt->super, Q_ACTIVE_UPCAST(me));
}

/**
 * @brief 一次采样步骤：读取、转换、校准、发布。
 */
static void do_sample(channel_ao_t* me)
{
    analog_input_conversion_context_t ctx;
    int32_t code;
    float value;

    if ( me->wire != (uint8_t)ANALOG_INPUT_WIRE_NORMAL ) {
        /* 值冻结，断线判定维持到下一次检查。 */
        publish(me, me->last_value,
                wire_status_code(me->cfg.type, me->wire));
        return;
    }

    if ( analog_input_read_sample(me->input, &code) != ANALOG_INPUT_OK ) {
        publish(me, me->last_value, (uint16_t)CH_STATUS_HW_ERROR);
        return;
    }

    if ( (me->cfg.type == (uint8_t)CH_TYPE_TC) && (me->cjc_valid == 0U) ) {
        publish(me, me->last_value, (uint16_t)CH_STATUS_NO_CJC);
        return;
    }

    ctx.cold_junction = me->cjc_temp;
    ctx.linear_min    = me->cfg.linear_min;
    ctx.linear_max    = me->cfg.linear_max;
    ctx.gain          = me->cfg.gain;
    ctx.offset        = me->cfg.offset;

    if ( analog_input_convert(me->input, code, &ctx, &value) != ANALOG_INPUT_OK ) {
        publish(me, me->last_value, (uint16_t)CH_STATUS_OUT_OF_RANGE);
        return;
    }

    me->last_value = value;
    publish(me, value, (uint16_t)CH_STATUS_OK);
}

/**
 * @brief 应用待生效配置：APPLY_SIG 重入步骤。
 *
 * @return 驱动接受该配置时返回 true。
 */
static bool apply_pending(channel_ao_t* me)
{
    analog_input_config_t acfg;
    uint32_t delay = 0U;

    acfg.type                = (analog_input_type_t)me->pending.type;
    acfg.thermocouple_type   = (me->pending.type == (uint8_t)CH_TYPE_TC)
                                   ? (analog_input_thermocouple_type_t)me->pending.tc_type
                                   : ANALOG_INPUT_THERMOCOUPLE_TYPE_NONE;
    acfg.sample_rate_millihz = rate_millihz(me->pending.rate_code);

    if ( analog_input_configure(me->input, &acfg, &delay) != ANALOG_INPUT_OK ) {
        /* 合法异常：上报并保持未配置，直到被重新写入。 */
        publish(me, me->last_value, (uint16_t)CH_STATUS_HW_ERROR);
        return false;
    }

    me->cfg       = me->pending;
    me->settle_ms = delay;
    publish_applied(me);
    elog_debug("channel_ao", "ch%u configured: type=%u rate=%u",
               (unsigned)me->ch, (unsigned)me->cfg.type,
               (unsigned)me->cfg.rate_code);
    return true;
}

void channel_ao_ctor(channel_ao_t* me,
                     uint8_t ch,
                     analog_input_t* input)
{
    QActive_ctor(&me->super, Q_STATE_CAST(&channel_ao_initial));
    QTimeEvt_ctorX(&me->te_sample, Q_ACTIVE_UPCAST(me), SAMPLE_SIG, 0U);
    QTimeEvt_ctorX(&me->te_settle, Q_ACTIVE_UPCAST(me), SETTLE_SIG, 0U);
    QTimeEvt_ctorX(&me->te_break, Q_ACTIVE_UPCAST(me), BREAK_WINDOW_SIG, 0U);

    me->input = input;
    Q_ASSERT(me->input != NULL);

    me->ch = ch;
    Q_ASSERT(ch < CH_CONFIG_CHANNEL_COUNT);
    me->cjc_temp   = 0.0f;
    me->last_value = 0.0f;
    me->settle_ms  = 0U;
    me->cjc_valid  = 0U;
    me->wire       = (uint8_t)ANALOG_INPUT_WIRE_NORMAL;

    /* 在测量路径被使用之前由 CONFIG_SET_SIG 填充。 */
    me->cfg.type       = (uint8_t)CH_TYPE_TC;
    me->cfg.tc_type    = CH_TC_TYPE_NONE;
    me->cfg.rate_code  = 0U;
    me->cfg.linear_min = 0.0f;
    me->cfg.linear_max = 0.0f;
    me->cfg.offset     = 0.0f;
    me->cfg.gain       = 1.0f;
    me->pending        = me->cfg;
}

/**
 * @brief 顶层初始转换：订阅并驻留。
 *
 * 这里不发布状态 1：启动加载的发布将早于 AoModbus 的订阅而被丢弃。镜像
 * 初值（status = 1）才是该机制（docs/ao/channel_ao.md）。
 */
static QState channel_ao_initial(channel_ao_t* const me, void const* const par)
{
    Q_UNUSED_PAR(par);

    /* 订阅在初始转换中完成（QP/C 示例风格）。 */
    QActive_subscribe(Q_ACTIVE_UPCAST(me), CONFIG_SET_SIG);
    QActive_subscribe(Q_ACTIVE_UPCAST(me), CJC_SIG);

    return Q_TRAN(&channel_ao_unconfigured);
}

/**
 * @brief 根状态：所有状态共有的处理。
 */
static QState channel_ao_root(channel_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case CJC_SIG: {
            cjc_evt_t const* cjc = Q_EVT_CAST(cjc_evt_t);

            me->cjc_temp  = cjc->temp_c;
            me->cjc_valid = cjc->valid;
            status        = Q_HANDLED();
            break;
        }
        default: {
            status = Q_SUPER(&QHsm_top);
            break;
        }
    }
    return status;
}

/**
 * @brief Unconfigured：无生效配置，改配闸门。
 *
 * 唯一出边是 APPLY_SIG。此处从不 arm 定时器，因此迟到的定时器事件必然
 * 落入本状态并被忽略（channel_ao.md 第 6 节）。
 */
static QState channel_ao_unconfigured(channel_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            /* 无条件撤防；撤防已停止的定时器无副作用。 */
            (void)QTimeEvt_disarm(&me->te_sample);
            (void)QTimeEvt_disarm(&me->te_settle);
            (void)QTimeEvt_disarm(&me->te_break);
            status = Q_HANDLED();
            break;
        }
        case CONFIG_SET_SIG: {
            if ( Q_EVT_CAST(config_set_evt_t)->ch == me->ch ) {
                me->pending = Q_EVT_CAST(config_set_evt_t)->cfg;
                QACTIVE_POST(Q_ACTIVE_UPCAST(me), &applyEvt, Q_ACTIVE_UPCAST(me));
            }
            /* 连续改配合并到最新的待应用配置。 */
            status = Q_HANDLED();
            break;
        }
        case APPLY_SIG: {
            if ( apply_pending(me) ) {
                /* 成功后经 Working 重入测量路径。 */
                status = Q_TRAN(&channel_ao_working);
            } else {
                /* 失败：上报状态 6（在内部完成）并等待。 */
                status = Q_HANDLED();
            }
            break;
        }
        default: {
            status = Q_SUPER(&channel_ao_root);
            break;
        }
    }
    return status;
}

/**
 * @brief Working：配置已生效并运行中；改配的公共入口。
 */
static QState channel_ao_working(channel_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_INIT_SIG: {
            /* 初始目标由已配置的输入类型决定。 */
            status = detectable(me)
                         ? Q_TRAN(&channel_ao_detecting)
                         : Q_TRAN(&channel_ao_settling);
            break;
        }
        case CONFIG_SET_SIG: {
            if ( Q_EVT_CAST(config_set_evt_t)->ch == me->ch ) {
                /*
                 * 改配隔离。先撤防再投递 APPLY_SIG：若某个节拍落在投递与
                 * 撤防之间，迟到的 SETTLE 会排在 APPLY_SIG 之后入队并到达
                 * 重入后的叶子状态（channel_ao.md 第 6 节）。
                 */
                (void)QTimeEvt_disarm(&me->te_sample);
                (void)QTimeEvt_disarm(&me->te_settle);
                (void)QTimeEvt_disarm(&me->te_break);
                me->pending = Q_EVT_CAST(config_set_evt_t)->cfg;
                QACTIVE_POST(Q_ACTIVE_UPCAST(me), &applyEvt, Q_ACTIVE_UPCAST(me));
                status = Q_TRAN(&channel_ao_unconfigured);
            } else {
                status = Q_HANDLED();
            }
            break;
        }
        default: {
            status = Q_SUPER(&channel_ao_root);
            break;
        }
    }
    return status;
}

/**
 * @brief Detecting：硬件处于断线检测模式，值冻结。
 */
static QState channel_ao_detecting(channel_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            uint32_t delay = 0U;

            /* 此处不发布：断线在检查完成时才揭示。 */
            (void)analog_input_enable_break_detection(me->input, &delay);
            QTimeEvt_armX(&me->te_settle, (delay != 0U) ? delay : 1U, 0U);
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            (void)QTimeEvt_disarm(&me->te_settle);
            status = Q_HANDLED();
            break;
        }
        case SETTLE_SIG: {
            analog_input_wire_status_t ws = (analog_input_wire_status_t)me->wire;
            uint32_t delay                = 0U;

            if ( analog_input_check_wire_break(me->input, &ws) == ANALOG_INPUT_OK ) {
                uint16_t code;

                me->wire = (uint8_t)ws;
                code     = wire_status_code(me->cfg.type, me->wire);
                if ( code != (uint16_t)CH_STATUS_OK ) {
                    /* 断线在检查完成的瞬间揭示。 */
                    publish(me, me->last_value, code);
                    elog_debug("channel_ao", "ch%u wire break: %u",
                               (unsigned)me->ch, (unsigned)code);
                }
                /* 完好：状态 0 随首个测量值一起上报。 */
            } else {
                /* IO 失败：上报并在后续节拍重试。 */
                publish(me, me->last_value, (uint16_t)CH_STATUS_HW_ERROR);
            }

            (void)analog_input_disable_break_detection(me->input, &delay);
            me->settle_ms = delay;
            status        = Q_TRAN(&channel_ao_settling);
            break;
        }
        default: {
            status = Q_SUPER(&channel_ao_working);
            break;
        }
    }
    return status;
}

/**
 * @brief Settling：测量模式，等待首个有效转换。
 */
static QState channel_ao_settling(channel_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            QTimeEvt_armX(&me->te_settle,
                          (me->settle_ms != 0U) ? me->settle_ms : 1U, 0U);
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            (void)QTimeEvt_disarm(&me->te_settle);
            status = Q_HANDLED();
            break;
        }
        case SETTLE_SIG: {
            /* 首次采样恰好落在稳定点。 */
            do_sample(me);
            status = Q_TRAN(&channel_ao_measuring);
            break;
        }
        default: {
            status = Q_SUPER(&channel_ao_working);
            break;
        }
    }
    return status;
}

/**
 * @brief Measuring：周期采样，外加 3 s 断线窗口。
 */
static QState channel_ao_measuring(channel_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            uint32_t ticks = rate_ticks(me->cfg.rate_code);

            QTimeEvt_armX(&me->te_sample, ticks, ticks);
            if ( detectable(me) ) {
                QTimeEvt_armX(&me->te_break, CHANNEL_AO_BREAK_WINDOW_MS,
                              CHANNEL_AO_BREAK_WINDOW_MS);
            }
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            (void)QTimeEvt_disarm(&me->te_sample);
            (void)QTimeEvt_disarm(&me->te_break);
            status = Q_HANDLED();
            break;
        }
        case SAMPLE_SIG: {
            /* 周期定时器：无需重新 arm。 */
            do_sample(me);
            status = Q_HANDLED();
            break;
        }
        case BREAK_WINDOW_SIG: {
            /* 模式切换在 Detecting 的 entry 动作中完成。 */
            status = Q_TRAN(&channel_ao_detecting);
            break;
        }
        default: {
            status = Q_SUPER(&channel_ao_working);
            break;
        }
    }
    return status;
}
