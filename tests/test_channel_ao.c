/**
 * @file test_channel_ao.c
 * @brief 通道采集 AO 的宿主事件注入测试。
 *
 * 驱动真实的 QHsm 派发循环（QActive_get_ -> QASM_DISPATCH -> QF_gc，与 QV 内核
 * 执行的同一三元组），并通过 QTIMEEVT_TICK_X 注入节拍。交错扫掠针对真实状态机
 * 运行，假模拟量输入的调用日志就是轨迹证据。
 *
 * 记录器 AO 观察 MEASURE_SIG 与 CFG_APPLIED_SIG：CFG_APPLIED_SIG 是接受路径。
 *
 * 宿主范围说明：扫掠证明 docs/ao/channel_ao.md 第 6 节的队列顺序纪律（重配置被
 * 派发时已经入队的定时器事件，必须由旧状态消费，或在 Unconfigured 中被忽略）。
 * 在单线程宿主上，到期不可能落在同一 RTC 步进的两个动作之间；在目标板上，通过
 * 在投递 APPLY_SIG 之前解除定时从结构上排除，且下面的截止时间断言会捕获任何
 * 派发可见的回归。
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qpc.h"

#include "analog_input_board.h"
#include "analog_input_fake.h"
#include "app.h"
#include "channel_ao.h"
#include "host_support.h"

static int checks;
static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        ++checks;                                                  \
        if ( !(cond) ) {                                           \
            ++failures;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while ( 0 )

/*----------------------------------------------------------------------------*/
/* 测试夹具                                                                   */
/*----------------------------------------------------------------------------*/

static channel_ao_t ao;
static analog_input_fake_t fake;
static QEvtPtr ao_queue[CHANNEL_AO_QUEUE_LEN];
static QSubscrList subscr[MAX_SIG];
static QF_MPOOL_EL(measure_evt_t) pool_a[APP_POOL_A_BLOCK_NUM];
static QF_MPOOL_EL(config_set_evt_t) pool_b[APP_POOL_B_BLOCK_NUM];

/* 发布观察者：一个普通 QActive，测试直接排空它的队列。 */
static QActive recorder;
static QEvtPtr rec_queue[32];
static uint8_t ao_started;
static uint8_t rec_started;

/** @brief 自场景开始以来观察到的接受事件（CFG_APPLIED_SIG）。 */
static uint16_t applied_count;
static uint8_t applied_ch;

/** @brief 为一条非 MEASURE 观察者事件记账。 */
static void rec_absorb(QEvt const* e)
{
    if ( e->sig == CFG_APPLIED_SIG ) {
        ++applied_count;
        applied_ch = Q_EVT_CAST(cfg_applied_evt_t)->ch;
    }
}

/** @brief 排空记录器队列，并统计其中持有的 MEASURE_SIG 事件。 */
static uint16_t rec_drain(void)
{
    uint16_t n = 0U;

    while ( !QEQueue_isEmpty(&recorder.eQueue) ) {
        QEvt const* e = QActive_get_(&recorder);

        if ( e->sig == MEASURE_SIG ) {
            ++n;
        } else {
            rec_absorb(e);
        }
        QF_gc(e);
    }
    return n;
}

/** @brief 取出一条 MEASURE_SIG 事件；队列中没有时返回 NULL。 */
static QEvt const* rec_take(void)
{
    while ( !QEQueue_isEmpty(&recorder.eQueue) ) {
        QEvt const* e = QActive_get_(&recorder);

        if ( e->sig == MEASURE_SIG ) {
            return e;
        }
        rec_absorb(e);
        QF_gc(e);
    }
    return NULL;
}

static void tick(uint32_t n)
{
    while ( n != 0U ) {
        QTIMEEVT_TICK_X(0U, (void*)0);
        ++g_test_tick;
        --n;
    }
}

static void dispatch_one(void)
{
    QEvt const* e = QActive_get_(Q_ACTIVE_UPCAST(&ao));

    QASM_DISPATCH(Q_ACTIVE_UPCAST(&ao), e, 0U);
    QF_gc(e);
}

static void drain(void)
{
    while ( !QEQueue_isEmpty(&ao.super.eQueue) ) {
        dispatch_one();
    }
}

/** @brief 排空 AO 并回收每条观察者事件，事件池安全。 */
static void drain_all(void)
{
    drain();
    rec_drain();
}

/**
 * @brief 像 QV 循环那样，在持续排空的同时推进虚拟时间。
 *
 * 稳定截止时间是在派发过程中设置的，因此节拍必须与排空交错，否则下一个截止
 * 时间永远不会落在预算之内。
 */
static void run_ticks(uint32_t n)
{
    while ( n != 0U ) {
        tick(1U);
        drain_all();
        --n;
    }
}

static QState rec_state(QHsm* const me, QEvt const* const e)
{
    (void)me;
    (void)e;
    return Q_SUPER(&QHsm_top);
}

static QState rec_handler(QHsm* const me, QEvt const* const e)
{
    (void)me;
    (void)e;
    return Q_TRAN(&rec_state);
}

/**
 * @brief 启动 AO（其初始迁移在 QActive_start 内运行），
 *        并让记录器订阅 MEASURE_SIG 与 CFG_APPLIED_SIG。
 */
static void scenario_begin(void)
{
    if ( ao_started != 0U ) {
        (void)QTimeEvt_disarm(&ao.te_sample);
        (void)QTimeEvt_disarm(&ao.te_settle);
        (void)QTimeEvt_disarm(&ao.te_break);
        QActive_unregister_(Q_ACTIVE_UPCAST(&ao));
    }
    if ( rec_started != 0U ) {
        QActive_unregister_(&recorder);
    }

    /* 清除所有订阅，使重复订阅不会造成重复投递。 */
    memset(subscr, 0, sizeof(subscr));
    QActive_psInit(subscr, MAX_SIG);

    analog_input_fake_init(&fake);
    analog_input_fake_reset_log(&fake);
    applied_count = 0U;
    applied_ch    = 0xFFU;

    channel_ao_ctor(&ao, 0U, &fake.base);
    QActive_start(Q_ACTIVE_UPCAST(&ao), Q_PRIO(APP_PRIO_CH_FIRST, 0U),
                  ao_queue, CHANNEL_AO_QUEUE_LEN, (void*)0, 0U, (void*)0);
    ao_started = 1U;

    QActive_ctor(&recorder, Q_STATE_CAST(&rec_handler));
    QActive_start(&recorder, Q_PRIO(30U, 0U),
                  rec_queue, 32U, (void*)0, 0U, (void*)0);
    rec_started = 1U;
    QActive_subscribe(&recorder, MEASURE_SIG);
    QActive_subscribe(&recorder, CFG_APPLIED_SIG);
}

/** @brief 某个速率码的期望采样率（mHz，APP_RATE_LIST）。 */
static uint32_t rate_mhz_of(uint16_t code)
{
    static const uint32_t table[] = {
#define X(rate_name, millihertz) millihertz,
        APP_RATE_LIST(X)
#undef X
    };

    return (code < (sizeof(table) / sizeof(table[0]))) ? table[code] : 0U;
}

/** @brief 为通道 1 发布一份配置。 */
static void publish_config(uint8_t type,
                           uint8_t tc_type,
                           uint16_t rate_code)
{
    config_set_evt_t* e = Q_NEW(config_set_evt_t, CONFIG_SET_SIG);

    e->ch             = 0U;
    e->cfg.type       = type;
    e->cfg.tc_type    = tc_type;
    e->cfg.rate_code  = rate_code;
    e->cfg.linear_min = 0.0f;
    e->cfg.linear_max = 0.0f;
    e->cfg.offset     = 0.0f;
    e->cfg.gain       = 1.0f;
    QACTIVE_PUBLISH(&e->super, (void*)0);
}

/** @brief 发布一次冷端心跳。 */
static void publish_cjc(float temp_c,
                        uint8_t valid)
{
    cjc_evt_t* e = Q_NEW(cjc_evt_t, CJC_SIG);

    e->temp_c = temp_c;
    e->valid  = valid;
    QACTIVE_PUBLISH(&e->super, (void*)0);
}

/**
 * @brief 送入一份配置，并让 AO 运行到 Measuring。
 */
static void drive_to_measuring(uint8_t type,
                               uint8_t tc_type,
                               uint16_t rate_code)
{
    publish_config(type, tc_type, rate_code);
    drain(); /* Unconfigured：pending + APPLY_SIG */
    drain(); /* APPLY：configure、Working 初始化，Detecting 入口设置 te_settle */
    tick(fake.enable_delay_ms + 1U);
    drain(); /* SETTLE：断线检查、关闭，Settling 入口设置 te_settle */
    tick(fake.disable_delay_ms + 1U);
    drain(); /* SETTLE：首次采样，Measuring 入口设置 te_sample */
}

/*----------------------------------------------------------------------------*/
/* 批次 1：重配置竞态，针对真实状态机扫掠                                     */
/*----------------------------------------------------------------------------*/

/** @brief 重配置扫掠的起始状态。 */
enum {
    SW_UNCONFIG = 0,
    SW_DETECTING,
    SW_SETTLING,
    SW_MEASURING
};

/**
 * @brief 一种交错：在定时器运行时重配置，然后证明重新进入的代次遵守
 *        自己的截止时间与合并规则。
 *
 * @param start SW_* 起始状态之一。
 * @param settle_before 在投递重配置之前让已设置的定时器到期。
 * @param late_after_post 在投递之后、重配置派发之前让已设置的定时器到期
 *        （该事件必须在 Unconfigured 中被忽略）。
 * @param second_cfg 在 APPLY 之前再投递一次重配置（pending 合并）。
 * @param post_ticks APPLY 派发之后搅动的节拍数。
 */
static void sweep_run(int start,
                      int settle_before,
                      int late_after_post,
                      int second_cfg,
                      unsigned post_ticks)
{
    uint16_t baseline;
    uint16_t new_cfg_count;
    uint16_t i;

    scenario_begin();
    baseline = fake.log_len;

    /* 到达请求的起始状态。 */
    if ( start == SW_DETECTING ) {
        publish_config((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);
        drain();
        drain();
    } else if ( start == SW_SETTLING ) {
        publish_config((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);
        drain();
        drain();
        tick(fake.enable_delay_ms + 1U);
        drain();
    } else if ( start == SW_MEASURING ) {
        drive_to_measuring((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);
    } else { /* SW_UNCONFIG：无需准备 */
    }

    if ( settle_before ) {
        /* 截止时间事件排在重配置之前入队：旧状态必须在重配置派发之前把它当作
         * 真实事件消费。 */
        if ( start == SW_MEASURING ) {
            tick(100U + 1U); /* 10 Hz 下的 te_sample 周期 */
        } else {
            tick(201U); /* 任一稳定截止时间最长 200 ms */
        }
    }

    publish_config((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 5U); /* 20 Hz */
    if ( late_after_post ) {
        /* 投递与派发之间发生的定时器到期在 FIFO 中排在重配置之后；
         * Unconfigured 必须忽略它。 */
        if ( start == SW_MEASURING ) {
            tick(100U + 1U);
        } else if ( !settle_before ) {
            tick(201U);
        }
    }
    if ( second_cfg ) {
        publish_config((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 9U); /* 50 Hz */
    }

    drain(); /* 重配置派发、被忽略的迟到事件、APPLY 重入 */
    tick(post_ticks);
    drain();

    /* 收敛：新代次必须达到周期采样。 */
    run_ticks(fake.enable_delay_ms + fake.disable_delay_ms + 2U * 40U);
    run_ticks(3U * 40U);

    /* 没有触发断言（Q_onError 会中止），且两个队列都为空。 */
    CHECK(QEQueue_isEmpty(&ao.super.eQueue));
    CHECK(QEQueue_isEmpty(&recorder.eQueue));

    /* 不论如何合并，最终代次恰好一次 configure。 */
    new_cfg_count = 0U;
    for ( i = baseline; i < fake.log_len; ++i ) {
        if ( fake.log[i].op == (uint8_t)ANALOG_INPUT_FAKE_CONFIGURE ) {
            if ( (fake.log[i].rate == rate_mhz_of(5U))
                 || (fake.log[i].rate == rate_mhz_of(9U)) ) {
                ++new_cfg_count;
            }
        }
    }
    CHECK(new_cfg_count == 1U);

    /* 旧代次的断线检查只能出现在新代次的 configure 之前。 */
    {
        uint16_t cfg_idx          = 0U;
        uint8_t have_cfg          = 0U;
        uint32_t const final_rate = rate_mhz_of((uint16_t)(second_cfg ? 9U : 5U));

        for ( i = baseline; i < fake.log_len; ++i ) {
            if ( (fake.log[i].op == (uint8_t)ANALOG_INPUT_FAKE_CONFIGURE)
                 && (fake.log[i].rate == final_rate) ) {
                cfg_idx  = i;
                have_cfg = 1U;
                break;
            }
        }
        CHECK(have_cfg != 0U);

        if ( have_cfg != 0U ) {
            const analog_input_fake_call_t* enable = NULL;
            const analog_input_fake_call_t* check  = NULL;
            uint16_t reads                         = 0U;

            for ( i = cfg_idx; i < fake.log_len; ++i ) {
                const analog_input_fake_call_t* c = &fake.log[i];

                if ( c->op == (uint8_t)ANALOG_INPUT_FAKE_ENABLE_BREAK ) {
                    CHECK(enable == NULL); /* 一代次一次 enable */
                    enable = c;
                } else if ( c->op == (uint8_t)ANALOG_INPUT_FAKE_CHECK_WIRE ) {
                    CHECK(c->rate == final_rate);
                    if ( check == NULL ) {
                        check = c;
                        /* 新代次的截止时间得到了遵守。 */
                        CHECK(enable != NULL);
                        CHECK(c->tick >= (enable->tick + fake.enable_delay_ms - 1U));
                    }
                } else if ( c->op == (uint8_t)ANALOG_INPUT_FAKE_READ_SAMPLE ) {
                    /* 外来配置下不采样。 */
                    CHECK(c->rate == final_rate);
                    ++reads;
                }
            }
            CHECK(check != NULL);
            CHECK(reads >= 2U); /* 已收敛 */
        }
    }
}

static void test_reconfig_sweep(void)
{
    int start;
    int settle_before;
    int late_after_post;
    int second_cfg;
    unsigned post_ticks;

    for ( start = SW_UNCONFIG; start <= SW_MEASURING; ++start ) {
        for ( settle_before = 0; settle_before <= 1; ++settle_before ) {
            for ( late_after_post = 0; late_after_post <= 1; ++late_after_post ) {
                for ( second_cfg = 0; second_cfg <= 1; ++second_cfg ) {
                    for ( post_ticks = 0U; post_ticks <= 2U; ++post_ticks ) {
                        sweep_run(start, settle_before, late_after_post,
                                  second_cfg, post_ticks);
                    }
                }
            }
        }
    }
}

/*----------------------------------------------------------------------------*/
/* 批次 2：固定轨迹                                                           */
/*----------------------------------------------------------------------------*/

/** @brief 先检查后测量：首次检查之前不发布任何内容。 */
static void test_first_value_after_check(void)
{
    scenario_begin();
    publish_cjc(25.0f, 1U); /* 稳态：采样即为测量值 */
    drain();
    publish_config((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);
    drain();
    drain(); /* APPLY 已派发：进入 Detecting，尚未发布任何内容 */
    CHECK(rec_drain() == 0U);

    tick(fake.enable_delay_ms + 1U);
    drain(); /* 断线检查完成，Detecting 仍未发布任何内容 */
    CHECK(rec_drain() == 0U);

    tick(fake.disable_delay_ms + 1U);
    drain(); /* 首次采样：揭示随首个值一起发出 */
    {
        QEvt const* e = rec_take();

        CHECK(e != NULL);
        if ( e != NULL ) {
            measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

            CHECK(m->status == (uint16_t)CH_STATUS_OK);
            QF_gc(e);
        }
    }
    CHECK(rec_drain() == 0U);
}

/** @brief 断线在检查完成时揭示，随后冻结。 */
static void test_wire_break_reveal_and_freeze(void)
{
    QEvt const* e;
    uint16_t status;

    scenario_begin();
    fake.check_wire = ANALOG_INPUT_WIRE_BROKEN;
    drive_to_measuring((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);

    /* 先检查后测量：首个值就已经带有断线状态。 */
    e = rec_take();
    CHECK(e != NULL);
    status = (uint16_t)CH_STATUS_OK;
    if ( e != NULL ) {
        measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

        status = m->status;
        CHECK(m->value == 0.0f); /* 冻结：从未取到有效采样 */
        QF_gc(e);
    }
    CHECK(status == (uint16_t)CH_STATUS_TC_BREAK);
    rec_drain();

    /* 之后每次采样都冻结在同一状态。 */
    tick(200U);
    drain();
    tick(200U);
    drain();
    {
        uint16_t frozen = 0U;
        uint16_t other  = 0U;

        while ( !QEQueue_isEmpty(&recorder.eQueue) ) {
            measure_evt_t const* mm;
            QEvt const* ee = QActive_get_(&recorder);

            if ( ee->sig == MEASURE_SIG ) {
                mm = Q_EVT_CAST(measure_evt_t);
                if ( mm->status == (uint16_t)CH_STATUS_TC_BREAK ) {
                    ++frozen;
                } else {
                    ++other;
                }
            }
            QF_gc(ee);
        }
        CHECK(frozen >= 2U);
        CHECK(other == 0U);
    }
}

/**
 * @brief 经由检测循环验证断线判定矩阵。
 *
 * wire_status_code() 的状态映射恰好在这里可观察：检查完成即立刻发布判定。
 */
static void test_wire_code_matrix(void)
{
    static const struct {
        uint8_t type;
        uint8_t tc_type;
        uint8_t wire;
        uint16_t expected;
    } cases[] = {
        /* 完好回路在首次采样之前不发布任何内容。 */
        {(uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 0U, (uint16_t)CH_STATUS_OK},
        {(uint8_t)CH_TYPE_PT100, CH_TC_TYPE_NONE, 0U, (uint16_t)CH_STATUS_OK},
        /* 热电偶：任何开路都是状态 2（引线不可分辨）。 */
        {(uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 1U, (uint16_t)CH_STATUS_TC_BREAK},
        {(uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U, (uint16_t)CH_STATUS_TC_BREAK},
        {(uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 3U, (uint16_t)CH_STATUS_TC_BREAK},
        /* PT100：可分辨的引线报告 3 / 4。 */
        {(uint8_t)CH_TYPE_PT100, CH_TC_TYPE_NONE, 2U, (uint16_t)CH_STATUS_RTD_LEAD_BREAK},
        {(uint8_t)CH_TYPE_PT100, CH_TC_TYPE_NONE, 3U, (uint16_t)CH_STATUS_RTD_RETURN_BREAK},
        /* 不可分辨的 PT100 断线：前端契约违规，状态 6。 */
        {(uint8_t)CH_TYPE_PT100, CH_TC_TYPE_NONE, 1U, (uint16_t)CH_STATUS_HW_ERROR},
        /* 未知断线码：状态 6。 */
        {(uint8_t)CH_TYPE_PT100, CH_TC_TYPE_NONE, 7U, (uint16_t)CH_STATUS_HW_ERROR}};
    size_t k;

    for ( k = 0; k < (sizeof(cases) / sizeof(cases[0])); ++k ) {
        QEvt const* e;
        uint16_t status;

        scenario_begin();
        publish_cjc(25.0f, 1U); /* 稳态：状态 0 可观察 */
        drain();
        fake.check_wire = (analog_input_wire_status_t)cases[k].wire;
        drive_to_measuring(cases[k].type, cases[k].tc_type, 2U);

        /* 首个发布的值带有映射后的状态：检查完成时是断线本身，
         * 首次采样时是状态 0。 */
        e = rec_take();
        CHECK(e != NULL);
        status = (uint16_t)CH_STATUS_OK;
        if ( e != NULL ) {
            measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

            status = m->status;
            QF_gc(e);
        }
        CHECK(status == cases[k].expected);
        rec_drain();
    }
}

/**
 * @brief 不可检测类型上的冻结判定报告状态 6。
 *
 * 断线判定在重配置后仍然保留，并在每次冻结采样时通过新类型映射；不可检测的
 * 类型永远无法执行新的检查，因此前端违规码成立（断线码矩阵中的 4-20 mA /
 * 0-10 V 行）。
 */
static void test_wire_code_frozen_non_detectable(void)
{
    static const struct {
        uint8_t type;
        uint8_t wire;
    } cases[] = {
        {(uint8_t)CH_TYPE_4_20MA, 1U},
        {(uint8_t)CH_TYPE_0_10V, 2U},
        {(uint8_t)CH_TYPE_0_10V, 7U}};
    size_t k;

    for ( k = 0; k < (sizeof(cases) / sizeof(cases[0])); ++k ) {
        QEvt const* e;
        uint16_t status;

        scenario_begin();

        /* 一次 PT100 检查建立判定；冻结断线规则让它在重配置后保留
         *（channel_ao.md）。 */
        fake.check_wire = (analog_input_wire_status_t)cases[k].wire;
        publish_config((uint8_t)CH_TYPE_PT100, CH_TC_TYPE_NONE, 2U);
        drain();
        drain();
        tick(fake.enable_delay_ms + 1U);
        drain();
        rec_drain();

        publish_config(cases[k].type, CH_TC_TYPE_NONE, 2U);
        drain();
        drain();
        tick(fake.enable_delay_ms + 1U);
        drain();
        tick(fake.disable_delay_ms + 1U);
        drain();

        e = rec_take();
        CHECK(e != NULL);
        status = (uint16_t)CH_STATUS_OK;
        if ( e != NULL ) {
            measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

            status = m->status;
            QF_gc(e);
        }
        CHECK(status == (uint16_t)CH_STATUS_HW_ERROR);
        rec_drain();
    }
}

/** @brief TC 采样跟随冷端有效性心跳。 */
static void test_cjc_validity_gates_tc_samples(void)
{
    QEvt const* e;
    uint16_t status;

    scenario_begin();
    drive_to_measuring((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);
    rec_drain();

    publish_cjc(25.0f, 1U);
    drain();
    tick(101U);
    drain();
    rec_drain();

    publish_cjc(25.0f, 0U);
    drain();
    tick(101U);
    drain();
    e = rec_take();
    CHECK(e != NULL);
    status = (uint16_t)CH_STATUS_OK;
    if ( e != NULL ) {
        measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

        status = m->status;
        QF_gc(e);
    }
    CHECK(status == (uint16_t)CH_STATUS_NO_CJC);
    rec_drain();

    publish_cjc(25.0f, 1U);
    drain();
    tick(101U);
    drain();
    e = rec_take();
    CHECK(e != NULL);
    status = (uint16_t)CH_STATUS_HW_ERROR;
    if ( e != NULL ) {
        measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

        status = m->status;
        QF_gc(e);
    }
    CHECK(status == (uint16_t)CH_STATUS_OK);
    rec_drain();
}

/** @brief 被拒绝的配置让 AO 停驻并持续上报。 */
static void test_configure_failure_parks(void)
{
    scenario_begin();
    fake.configure_status = ANALOG_INPUT_ERR_IO;

    publish_config((uint8_t)CH_TYPE_TC, (uint8_t)CH_TC_TYPE_K, 2U);
    drain();
    drain(); /* APPLY 失败：发布状态 6，仍处于 Unconfigured */

    /* 失败的 apply 不发布接受事件。 */
    {
        QEvt const* e = rec_take();

        CHECK(e != NULL);
        if ( e != NULL ) {
            measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

            CHECK(m->status == (uint16_t)CH_STATUS_HW_ERROR);
            QF_gc(e);
        }
    }
    CHECK(applied_count == 0U);
    rec_drain();

    /* 停驻时不采样，但重写配置可以恢复。 */
    tick(1000U);
    drain();
    CHECK(rec_drain() == 0U);

    fake.configure_status = ANALOG_INPUT_OK;
    publish_config((uint8_t)CH_TYPE_4_20MA, CH_TC_TYPE_NONE, 2U);
    drain();
    drain();
    tick(fake.enable_delay_ms + 1U); /* 线性类型：只经 Settling */
    drain();
    tick(fake.disable_delay_ms + 1U);
    drain();
    CHECK(rec_drain() >= 1U);

    /* 重写为通道 1 恰好产生一次接受。 */
    CHECK(applied_count == 1U);
    CHECK(applied_ch == 0U);
}

/**
 * @brief 每个速率码都能驱动板级档位速率和采样周期。
 */
static void test_rate_table(void)
{
    uint16_t code;

    CHECK(CH_CONFIG_CHANNEL_COUNT == ANALOG_INPUT_BOARD_CHANNEL_COUNT);
    CHECK(CH_CONFIG_RATE_CODE_COUNT == 13U);

    for ( code = 0U; code < CH_CONFIG_RATE_CODE_COUNT; ++code ) {
        const analog_input_fake_call_t* c;
        uint32_t mhz = rate_mhz_of(code);
        uint32_t ticks;
        uint8_t have_cfg = 0U;
        uint16_t i;

        CHECK(mhz != 0U);

        scenario_begin();
        drive_to_measuring((uint8_t)CH_TYPE_4_20MA, CH_TC_TYPE_NONE, code);

        /* configure 调用携带板级档位速率（mHz）。 */
        for ( i = 0U; i < fake.log_len; ++i ) {
            c = analog_input_fake_at(&fake, i);
            if ( c->op == (uint8_t)ANALOG_INPUT_FAKE_CONFIGURE ) {
                CHECK(c->rate == mhz);
                have_cfg = 1U;
            }
        }
        CHECK(have_cfg != 0U);

        /* 采样定时器按 1 ms 节拍设置向上取整后的周期：
         * 实际速率保持在标称值或以下，且偏差在 4 % 以内。 */
        ticks = ao.te_sample.interval;
        CHECK(ticks == ((1000000U + mhz - 1U) / mhz));
        CHECK((mhz * ticks) >= 1000000U);
        CHECK((mhz * ticks) <= 1041666U);

        /* 在下一个场景之前回收每条观察者事件。 */
        drain_all();
    }
}

/*----------------------------------------------------------------------------*/

int main(void)
{
    QF_init();
    QF_poolInit(pool_a, sizeof(pool_a), sizeof(pool_a[0]));
    QF_poolInit(pool_b, sizeof(pool_b), sizeof(pool_b[0]));

    test_reconfig_sweep();
    test_first_value_after_check();
    test_wire_break_reveal_and_freeze();
    test_wire_code_matrix();
    test_wire_code_frozen_non_detectable();
    test_cjc_validity_gates_tc_samples();
    test_configure_failure_parks();
    test_rate_table();

    printf("test_channel_ao: %d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
