/**
 * @file ao_modbus.c
 * @brief Modbus RTU slave active object: protocol, mirrors, write pipeline.
 *
 * The write pipeline (docs/component/reg_map.md section 5) is atomic per
 * frame: validate everything first, commit synchronously with a read-back
 * check, and only then touch the mirror or publish SIG_CONFIG_SET.
 */

#include "ao_modbus.h"

#include <string.h>

#include "ao_events.h"
#include "modbus_slave_board.h"
#include "param.h"
#include "reg_map.h"

Q_DEFINE_THIS_MODULE("ao_modbus")

static QState ao_modbus_initial(ao_modbus_t* const me,
                                void const* const par);
static QState ao_modbus_serving(ao_modbus_t* const me,
                                QEvt const* const e);

/**
 * @brief Publish one SIG_CONFIG_SET event (commit succeeded).
 */
static void ao_modbus_publish_config(ao_modbus_t* me,
                                     uint8_t ch,
                                     const ch_config_t* cfg)
{
    config_set_evt_t* evt = Q_NEW(config_set_evt_t, SIG_CONFIG_SET);

    evt->ch  = ch;
    evt->cfg = *cfg;
    QACTIVE_PUBLISH(&evt->super, &me->super);
}

/**
 * @brief Synthesize the device fault word from mirrors and latches.
 */
static uint16_t ao_modbus_fault_word(const ao_modbus_t* me)
{
    uint16_t latched = 0U;
    uint8_t ch;

    if ( me->b1 != 0U ) {
        latched |= REG_MAP_FAULT_PARAM_WRITE;
    }
    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        if ( me->load_failed[ch] != 0U ) {
            latched |= REG_MAP_FAULT_PARAM_LOAD;
            break;
        }
    }
    if ( me->addr_load_failed != 0U ) {
        latched |= REG_MAP_FAULT_PARAM_LOAD;
    }
    if ( me->fdb_init_failed != 0U ) {
        latched |= REG_MAP_FAULT_FDB_INIT;
    }
    return reg_map_fault_word(me->cjc_valid, latched);
}

/*----------------------------------------------------------------------------*/
/* Data-space callbacks                                                       */
/*----------------------------------------------------------------------------*/

static modbus_slave_exception_t ao_modbus_coils(void* ctx,
                                                uint16_t start,
                                                uint16_t count,
                                                uint8_t* buffer,
                                                modbus_slave_register_mode_t mode)
{
    (void)ctx;
    (void)buffer;
    (void)mode;

    /* No coils in this product: the space itself is unmapped. */
    return reg_map_check(REG_SPACE_COIL, start, count, NULL);
}

static modbus_slave_exception_t ao_modbus_discrete(void* ctx,
                                                   uint16_t start,
                                                   uint16_t count,
                                                   uint8_t* buffer,
                                                   modbus_slave_register_mode_t mode)
{
    (void)ctx;
    (void)buffer;
    (void)mode;

    return reg_map_check(REG_SPACE_DISCRETE, start, count, NULL);
}

static modbus_slave_exception_t ao_modbus_input(void* ctx,
                                                uint16_t start,
                                                uint16_t count,
                                                uint8_t* buffer,
                                                modbus_slave_register_mode_t mode)
{
    ao_modbus_t* me = (ao_modbus_t*)ctx;
    reg_map_input_image_t image;
    modbus_slave_exception_t ex;

    ex = reg_map_check(REG_SPACE_INPUT, start, count, NULL);
    if ( ex != MODBUS_SLAVE_EX_NONE ) {
        return ex;
    }
    if ( mode != MODBUS_SLAVE_REG_READ ) {
        return MODBUS_SLAVE_EX_ILLEGAL_FUNCTION;
    }

    memcpy(image.value, me->value, sizeof(image.value));
    memcpy(image.status, me->status, sizeof(image.status));
    image.cjc_temp   = me->cjc_temp;
    image.fault_word = ao_modbus_fault_word(me);
    reg_map_input_encode(&image, start, count, buffer);
    return MODBUS_SLAVE_EX_NONE;
}

/**
 * @brief Holding register write pipeline: validate, commit, then update.
 */
static modbus_slave_exception_t ao_modbus_holding_write(ao_modbus_t* me,
                                                        uint16_t start,
                                                        const reg_span_t* span,
                                                        uint16_t count,
                                                        const uint8_t* buffer)
{
    param_status_t st;

    if ( span->kind == REG_SPAN_DEV ) {
        uint16_t addr = reg_map_dev_decode(buffer);

        if ( !reg_map_dev_addr_valid(addr) ) {
            return MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE;
        }

        st = param_save_addr(addr);
        if ( st == PARAM_ERR_WRITE ) {
            me->b1 = 1U;
            return MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE;
        }
        if ( st == PARAM_OK ) {
            me->addr_image       = addr;
            me->addr_load_failed = 0U;
            me->b1               = 0U;
        }
        /* PARAM_NO_CHANGE: mirror already holds the requested value. */
        return MODBUS_SLAVE_EX_NONE;
    }

    {
        uint8_t ch = (uint8_t)(span->base / REG_MAP_CH_CFG_STRIDE);
        uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
        ch_config_t cand;

        /* Read-modify-write: any single register write lands correctly. */
        reg_map_ch_encode(&me->cfg_image[ch], block);
        memcpy(&block[2U * (uint16_t)(start - span->base)], buffer, 2U * count);
        reg_map_ch_decode(block, &cand);
        if ( !ch_config_validate(&cand) ) {
            return MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE;
        }

        st = param_save_cfg(ch, &cand);
        if ( st == PARAM_ERR_WRITE ) {
            me->b1 = 1U;
            return MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE;
        }
        if ( st == PARAM_OK ) {
            me->cfg_image[ch] = cand;
            me->load_failed[ch] = 0U;
            me->b1              = 0U;
            ao_modbus_publish_config(me, (uint8_t)(ch + 1U), &cand);
        }
        return MODBUS_SLAVE_EX_NONE;
    }
}

static modbus_slave_exception_t ao_modbus_holding(void* ctx,
                                                  uint16_t start,
                                                  uint16_t count,
                                                  uint8_t* buffer,
                                                  modbus_slave_register_mode_t mode)
{
    ao_modbus_t* me = (ao_modbus_t*)ctx;
    const reg_span_t* span = NULL;
    modbus_slave_exception_t ex;

    ex = reg_map_check(REG_SPACE_HOLDING, start, count, &span);
    if ( ex != MODBUS_SLAVE_EX_NONE ) {
        return ex;
    }

    if ( mode == MODBUS_SLAVE_REG_WRITE ) {
        return ao_modbus_holding_write(me, start, span, count, buffer);
    }

    {
        uint8_t block[REG_MAP_CH_CFG_LEN * 2U];

        if ( span->kind == REG_SPAN_CH_CFG ) {
            uint8_t ch = (uint8_t)(span->base / REG_MAP_CH_CFG_STRIDE);

            reg_map_ch_encode(&me->cfg_image[ch], block);
        }
        else {
            reg_map_dev_encode(me->addr_image, block);
        }
        memcpy(buffer, &block[2U * (uint16_t)(start - span->base)], 2U * count);
    }
    return MODBUS_SLAVE_EX_NONE;
}

static uint8_t ao_modbus_address(void* ctx)
{
    ao_modbus_t* me = (ao_modbus_t*)ctx;

    return (uint8_t)me->addr_image;
}

/*----------------------------------------------------------------------------*/
/* ISR event port                                                             */
/*----------------------------------------------------------------------------*/

static void ao_modbus_on_frame(void* ctx)
{
    ao_modbus_t* me = (ao_modbus_t*)ctx;

    QACTIVE_POST(&me->super, &me->frameEvt, &me->super);
}

static void ao_modbus_on_sent(void* ctx)
{
    ao_modbus_t* me = (ao_modbus_t*)ctx;

    QACTIVE_POST(&me->super, &me->txDoneEvt, &me->super);
}

static void ao_modbus_on_timeout(void* ctx)
{
    ao_modbus_t* me = (ao_modbus_t*)ctx;

    QACTIVE_POST(&me->super, &me->txTimeoutEvt, &me->super);
}

/*----------------------------------------------------------------------------*/
/* Construction                                                               */
/*----------------------------------------------------------------------------*/

void ao_modbus_ctor(ao_modbus_t* me)
{
    static uint8_t const additional[] = "8CH-AI-RTU";
    modbus_slave_callbacks_t cb;
    modbus_slave_identity_t id;
    modbus_slave_port_event_t ev;
    uint8_t ch;

    QActive_ctor(&me->super, Q_STATE_CAST(&ao_modbus_initial));
    QEvt_ctor(&me->frameEvt, SIG_FRAME);
    QEvt_ctor(&me->txDoneEvt, SIG_TX_DONE);
    QEvt_ctor(&me->txTimeoutEvt, SIG_TX_TIMEOUT);

    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        /* Channels report "unconfigured" until the first SIG_MEASURE. */
        me->value[ch]        = 0.0f;
        me->status[ch]       = (uint16_t)REG_MAP_STATUS_UNCONFIGURED;
        me->load_failed[ch]  = 0U;
    }
    memset(me->cfg_image, 0, sizeof(me->cfg_image));

    me->cjc_temp        = 0.0f;
    me->cjc_valid       = 0U; /* not ready: b0 until the first SIG_CJC */
    me->addr_image      = REG_MAP_DEV_ADDR_MIN;
    me->b1              = 0U;
    me->addr_load_failed = 0U;
    me->fdb_init_failed = 0U;
    me->tx_done_cnt     = 0U;
    me->tx_timeout_cnt  = 0U;

    cb.ctx               = me;
    cb.coils             = ao_modbus_coils;
    cb.discrete_inputs   = ao_modbus_discrete;
    cb.holding_registers = ao_modbus_holding;
    cb.input_registers   = ao_modbus_input;
    cb.address           = ao_modbus_address;

    id.slave_id          = 0xFFU;
    id.is_running        = 1U;
    id.additional        = additional;
    id.additional_length = (uint16_t)(sizeof(additional) - 1U);

    (void)modbus_slave_board_ctor(&cb, &id);

    ev.ctx              = me;
    ev.on_frame_received = ao_modbus_on_frame;
    ev.on_frame_sent     = ao_modbus_on_sent;
    ev.on_tx_timeout     = ao_modbus_on_timeout;
    (void)modbus_slave_set_event_port(modbus_slave_board_get(), &ev);
}

/**
 * @brief Top-most initial transition: boot load, then serve.
 *
 * Runs inside QActive_start(), before QF_run(): the loaded configurations
 * are published while every channel is already started and subscribed.
 */
static QState ao_modbus_initial(ao_modbus_t* const me,
                                void const* const par)
{
    param_boot_result_t res;
    uint8_t ch;

    Q_UNUSED_PAR(par);

    param_boot_load(&res);

    me->fdb_init_failed  = (res.db_ok != 0U) ? 0U : 1U;
    me->addr_load_failed = res.addr_load_failed;
    me->addr_image       = res.dev_addr;

    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        me->load_failed[ch] = res.cfg_load_failed[ch];
        if ( res.cfg_valid[ch] != 0U ) {
            me->cfg_image[ch] = res.cfg[ch];
            ao_modbus_publish_config(me, (uint8_t)(ch + 1U), &res.cfg[ch]);
        }
    }

    return Q_TRAN(&ao_modbus_serving);
}

/**
 * @brief Serving: the resident service state after the core starts.
 */
static QState ao_modbus_serving(ao_modbus_t* const me,
                                QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            if ( modbus_slave_start(modbus_slave_board_get()) != MODBUS_SLAVE_OK ) {
                /* Board-level defect; the port was vetted at construction. */
                Q_ERROR();
            }
            status = Q_HANDLED();
            break;
        }
        case SIG_FRAME: {
            (void)modbus_slave_advance(modbus_slave_board_get());
            status = Q_HANDLED();
            break;
        }
        case SIG_TX_DONE: {
            ++me->tx_done_cnt;
            status = Q_HANDLED();
            break;
        }
        case SIG_TX_TIMEOUT: {
            ++me->tx_timeout_cnt;
            status = Q_HANDLED();
            break;
        }
        case SIG_MEASURE: {
            measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

            me->value[m->ch - 1U]  = m->value;
            me->status[m->ch - 1U] = m->status;
            status = Q_HANDLED();
            break;
        }
        case SIG_CJC: {
            cjc_evt_t const* cjc = Q_EVT_CAST(cjc_evt_t);

            me->cjc_temp  = cjc->temp_c;
            me->cjc_valid = cjc->valid;
            status = Q_HANDLED();
            break;
        }
        default: {
            status = Q_SUPER(&QHsm_top);
            break;
        }
    }
    return status;
}
