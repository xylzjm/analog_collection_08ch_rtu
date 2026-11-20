/**
 * @file cold_junction_ao.c
 * @brief 冷端温度活动对象（TMP75，1 Hz 心跳）。
 */

#include "cold_junction_ao.h"

#include "app.h"

Q_DEFINE_THIS_MODULE("cold_junction_ao")

static QState cold_junction_ao_initial(cold_junction_ao_t* const me, void const* const par);
static QState cold_junction_ao_starting(cold_junction_ao_t* const me, QEvt const* const e);
static QState cold_junction_ao_sampling(cold_junction_ao_t* const me, QEvt const* const e);

/**
 * @brief 读取一次 TMP75 并发布 CJC_SIG 心跳。
 *
 * 有效性遵循 docs/ao/cold_junction_ao.md 第 2 节：有效要求已存在成功读数
 * 且连续失败次数少于 COLD_JUNCTION_AO_ERR_LIMIT。
 */
static void sample(cold_junction_ao_t* me)
{
    float temp = 0.0f;
    uint8_t valid;

    if ( (tmp75_read_temperature(me->dev, &temp) == TMP75_OK)
         && (temp >= COLD_JUNCTION_AO_MIN_TEMP_C)
         && (temp <= COLD_JUNCTION_AO_MAX_TEMP_C) ) {
        me->err_cnt      = 0U;
        me->have_reading = 1U;
        me->temp_c       = temp;
    } else if ( me->err_cnt < 0xFFU ) {
        /* 饱和计数：超过上限后具体数值不再有意义。 */
        ++me->err_cnt;
    }

    valid = (uint8_t)((me->have_reading != 0U)
                      && (me->err_cnt < COLD_JUNCTION_AO_ERR_LIMIT));

    {
        cjc_evt_t* evt = Q_NEW(cjc_evt_t, CJC_SIG);

        evt->temp_c = me->temp_c;
        evt->valid  = valid;
        QACTIVE_PUBLISH(&evt->super, Q_ACTIVE_UPCAST(me));
    }
}

void cold_junction_ao_ctor(cold_junction_ao_t* me,
                           tmp75_t* dev)
{
    QActive_ctor(&me->super, Q_STATE_CAST(&cold_junction_ao_initial));
    QTimeEvt_ctorX(&me->te_tick, Q_ACTIVE_UPCAST(me), TICK_SIG, 0U);
    me->dev = dev;
    Q_ASSERT(me->dev != NULL);
    me->temp_c       = 0.0f;
    me->err_cnt      = 0U;
    me->have_reading = 0U;
}

/**
 * @brief 顶层初始转换：启动进入 Starting 瞬态。
 */
static QState cold_junction_ao_initial(cold_junction_ao_t* const me, void const* const par)
{
    Q_UNUSED_PAR(par);
    return Q_TRAN(&cold_junction_ao_starting);
}

/**
 * @brief Starting：先做一次首读，然后进入稳态。
 *
 * entry 动作一完成就执行进入 Sampling 的嵌套初始转换（QHsm 嵌套初始
 * 转移）。
 */
static QState cold_junction_ao_starting(cold_junction_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            sample(me);
            QTimeEvt_armX(&me->te_tick, COLD_JUNCTION_AO_TICK_MS,
                          COLD_JUNCTION_AO_TICK_MS);
            status = Q_HANDLED();
            break;
        }
        case Q_INIT_SIG: {
            status = Q_TRAN(&cold_junction_ao_sampling);
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
 * @brief Sampling：每个 1 Hz 节拍读取一次。
 */
static QState cold_junction_ao_sampling(cold_junction_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case TICK_SIG: {
            sample(me);
            status = Q_HANDLED();
            break;
        }
        default: {
            /* Starting 是 Sampling 的超状态（嵌套初始）。 */
            status = Q_SUPER(&cold_junction_ao_starting);
            break;
        }
    }
    return status;
}
