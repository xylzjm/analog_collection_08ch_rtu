/**
 * @file modbus_image.c
 * @brief Typed Modbus mirror and the atomic write transaction.
 *
 * Moved out of modbus_ao.c: the register mirrors, the fault word
 * synthesis and the holding-register write pipeline are pure data-space
 * logic with no QP dependency, so the host tests drive the whole
 * transaction directly through the callbacks. The atomicity discipline
 * is unchanged - validate first, commit through param, only then touch
 * the mirror, and report the commit through the registered notifier.
 */

#include "modbus_image.h"

#include <string.h>

#include "param.h"
#include "reg_map.h"

/** @brief Synthesize the device fault word register value. */
static uint16_t fault_word(const modbus_image_t* img)
{
    uint16_t latched = 0U;
    uint8_t ch;

    if ( img->b1 != 0U ) {
        latched |= REG_MAP_FAULT_PARAM_WRITE;
    }
    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        if ( img->load_failed[ch] != 0U ) {
            latched |= REG_MAP_FAULT_PARAM_LOAD;
            break;
        }
    }
    if ( img->addr_load_failed != 0U ) {
        latched |= REG_MAP_FAULT_PARAM_LOAD;
    }
    if ( img->fdb_init_failed != 0U ) {
        latched |= REG_MAP_FAULT_FDB_INIT;
    }
    return reg_map_fault_word(img->cjc_valid, latched);
}

/*----------------------------------------------------------------------------*/
/* Data-space callbacks                                                       */
/*----------------------------------------------------------------------------*/

static modbus_slave_exception_t coils(void* ctx,
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

static modbus_slave_exception_t discrete(void* ctx,
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

static modbus_slave_exception_t input(void* ctx,
                                      uint16_t start,
                                      uint16_t count,
                                      uint8_t* buffer,
                                      modbus_slave_register_mode_t mode)
{
    modbus_image_t* img = (modbus_image_t*)ctx;
    reg_map_input_image_t image;
    modbus_slave_exception_t ex;

    ex = reg_map_check(REG_SPACE_INPUT, start, count, NULL);
    if ( ex != MODBUS_SLAVE_EX_NONE ) {
        return ex;
    }
    if ( mode != MODBUS_SLAVE_REG_READ ) {
        return MODBUS_SLAVE_EX_ILLEGAL_FUNCTION;
    }

    memcpy(image.value, img->value, sizeof(image.value));
    memcpy(image.status, img->status, sizeof(image.status));
    image.cjc_temp   = img->cjc_temp;
    image.fault_word = fault_word(img);
    reg_map_input_encode(&image, start, count, buffer);
    return MODBUS_SLAVE_EX_NONE;
}

/**
 * @brief Holding register write pipeline: validate, commit, then update.
 *
 * A configuration write is atomic per frame: nothing is mirrored or
 * reported unless the commit succeeded and the read-back matches.
 */
static modbus_slave_exception_t holding_write(modbus_image_t* img,
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
            img->b1 = 1U;
            return MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE;
        }
        if ( st == PARAM_OK ) {
            img->addr_image       = addr;
            img->addr_load_failed = 0U;
            img->b1               = 0U;
        }
        /* PARAM_NO_CHANGE: mirror already holds the requested value. */
        return MODBUS_SLAVE_EX_NONE;
    }

    {
        uint8_t ch = span->channel;
        uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
        ch_config_t cand;

        /* Read-modify-write: any single register write lands correctly. */
        reg_map_ch_encode(&img->cfg[ch], block);
        memcpy(&block[2U * (uint16_t)(start - span->base)], buffer, 2U * count);
        reg_map_ch_decode(block, &cand);
        if ( !ch_config_validate(&cand) ) {
            return MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE;
        }

        st = param_save_cfg(ch, &cand);
        if ( st == PARAM_ERR_WRITE ) {
            img->b1 = 1U;
            return MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE;
        }
        if ( st == PARAM_OK ) {
            img->cfg[ch]         = cand;
            img->load_failed[ch] = 0U;
            img->b1              = 0U;
            img->applied[ch]     = 0U;
            if ( img->cfg_notifier != NULL ) {
                img->cfg_notifier(img->cfg_notifier_ctx, ch, &cand);
            }
        } else if ( (st == PARAM_NO_CHANGE) && (img->applied[ch] == 0U) ) {
            /* Persisted already, but the channel has not accepted it yet. */
            img->cfg[ch]         = cand;
            img->load_failed[ch] = 0U;
            if ( img->cfg_notifier != NULL ) {
                img->cfg_notifier(img->cfg_notifier_ctx, ch, &cand);
            }
        }
        return MODBUS_SLAVE_EX_NONE;
    }
}

static modbus_slave_exception_t holding(void* ctx,
                                        uint16_t start,
                                        uint16_t count,
                                        uint8_t* buffer,
                                        modbus_slave_register_mode_t mode)
{
    modbus_image_t* img    = (modbus_image_t*)ctx;
    const reg_span_t* span = NULL;
    modbus_slave_exception_t ex;

    ex = reg_map_check(REG_SPACE_HOLDING, start, count, &span);
    if ( ex != MODBUS_SLAVE_EX_NONE ) {
        return ex;
    }

    if ( mode == MODBUS_SLAVE_REG_WRITE ) {
        return holding_write(img, start, span, count, buffer);
    }

    {
        uint8_t block[REG_MAP_CH_CFG_LEN * 2U];

        if ( span->kind == REG_SPAN_CH_CFG ) {
            reg_map_ch_encode(&img->cfg[span->channel], block);
        } else {
            reg_map_dev_encode(img->addr_image, block);
        }
        memcpy(buffer, &block[2U * (uint16_t)(start - span->base)], 2U * count);
    }
    return MODBUS_SLAVE_EX_NONE;
}

static uint8_t address(void* ctx)
{
    modbus_image_t* img = (modbus_image_t*)ctx;

    return (uint8_t)img->addr_image;
}

/*----------------------------------------------------------------------------*/
/* Mirror management                                                          */
/*----------------------------------------------------------------------------*/

void modbus_image_init(modbus_image_t* img)
{
    uint8_t ch;

    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        /* Channels report "unconfigured" until the first MEASURE_SIG. */
        img->value[ch]       = 0.0f;
        img->status[ch]      = (uint16_t)REG_MAP_STATUS_UNCONFIGURED;
        img->load_failed[ch] = 0U;
        img->applied[ch]     = 0U;
        memset(&img->cfg[ch], 0, sizeof(img->cfg[ch]));
    }

    img->cjc_temp         = 0.0f;
    img->cjc_valid        = 0U; /* not ready: b0 until the first CJC_SIG */
    img->addr_image       = REG_MAP_DEV_ADDR_MIN;
    img->b1               = 0U;
    img->addr_load_failed = 0U;
    img->fdb_init_failed  = 0U;
    img->cfg_notifier     = NULL;
    img->cfg_notifier_ctx = NULL;
}

void modbus_image_boot(modbus_image_t* img,
                       const param_boot_result_t* res)
{
    uint8_t ch;

    img->fdb_init_failed  = (res->db_ok != 0U) ? 0U : 1U;
    img->addr_load_failed = res->addr_load_failed;
    img->addr_image       = res->dev_addr;

    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        img->load_failed[ch] = res->cfg_load_failed[ch];
        img->applied[ch]     = 0U;
        if ( res->cfg_valid[ch] != 0U ) {
            img->cfg[ch] = res->cfg[ch];
        }
    }
}

const modbus_slave_callbacks_t* modbus_image_callbacks(modbus_image_t* img)
{
    static const modbus_slave_callbacks_t callbacks = {
        NULL, /* ctx is bound per instance below. */
        &coils,
        &discrete,
        &holding,
        &input,
        &address};
    modbus_slave_callbacks_t* table;

    /* The table lives in the image so ctx can point at the caller's storage. */
    table      = &img->callbacks;
    *table     = callbacks;
    table->ctx = img;
    return table;
}

void modbus_image_set_cfg_notifier(modbus_image_t* img,
                                   modbus_image_cfg_commit_fn fn,
                                   void* ctx)
{
    img->cfg_notifier     = fn;
    img->cfg_notifier_ctx = ctx;
}

void modbus_image_set_applied(modbus_image_t* img,
                              uint8_t ch,
                              uint8_t ok)
{
    if ( ch < CH_CONFIG_CHANNEL_COUNT ) {
        img->applied[ch] = (ok != 0U) ? 1U : 0U;
    }
}

void modbus_image_set_measure(modbus_image_t* img,
                              uint8_t ch,
                              float value,
                              uint16_t status)
{
    img->value[ch]  = value;
    img->status[ch] = status;
}

void modbus_image_set_cjc(modbus_image_t* img,
                          float temp_c,
                          uint8_t valid)
{
    img->cjc_temp  = temp_c;
    img->cjc_valid = valid;
}
