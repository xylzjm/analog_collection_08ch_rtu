/**
 * @file param_stub.c
 * @brief Link-time replacement of the param component for host tests.
 *
 * Implements the param.h contract with scriptable outcomes so the AoModbus
 * write pipeline can be driven through every commit result (OK, NO_CHANGE,
 * ERR_WRITE) without FlashDB or any flash hardware.
 */

#include "param_stub.h"

#include "reg_map.h"

param_stub_state_t param_stub;

/** @brief The factory default used by the reset script. */
static ch_config_t default_cfg(void)
{
    ch_config_t cfg;

    cfg.type       = (uint8_t)CH_TYPE_TC;
    cfg.tc_type    = (uint8_t)CH_TC_TYPE_K;
    cfg.rate_code  = 2U; /* 10 Hz */
    cfg.linear_min = 0.0f;
    cfg.linear_max = 0.0f;
    cfg.offset     = 0.0f;
    cfg.gain       = 1.0f;
    return cfg;
}

void param_stub_reset(void)
{
    uint8_t ch;

    param_stub.cfg_status  = PARAM_OK;
    param_stub.addr_status = PARAM_OK;

    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        param_stub.boot.cfg[ch]       = default_cfg();
        param_stub.boot.cfg_valid[ch] = 1U;
        param_stub.boot.cfg_load_failed[ch] = 0U;
    }
    param_stub.boot.dev_addr          = REG_MAP_DEV_ADDR_MIN;
    param_stub.boot.db_ok             = 1U;
    param_stub.boot.addr_load_failed  = 0U;

    param_stub.save_cfg_calls  = 0U;
    param_stub.save_addr_calls = 0U;
    param_stub.last_ch         = 0U;
    param_stub.last_cfg        = default_cfg();
    param_stub.last_addr       = REG_MAP_DEV_ADDR_MIN;
}

void param_stub_set_boot(const param_boot_result_t* boot)
{
    param_stub.boot = *boot;
}

void param_stub_set_save(param_status_t cfg,
                         param_status_t addr)
{
    param_stub.cfg_status  = cfg;
    param_stub.addr_status = addr;
}

void param_boot_load(param_boot_result_t* result)
{
    *result = param_stub.boot;
}

param_status_t param_save_cfg(uint8_t ch,
                              const ch_config_t* cfg)
{
    if ( (cfg == NULL) || (ch >= CH_CONFIG_CHANNEL_COUNT) ) {
        return PARAM_ERR_WRITE;
    }
    ++param_stub.save_cfg_calls;
    param_stub.last_ch  = ch;
    param_stub.last_cfg = *cfg;
    return param_stub.cfg_status;
}

param_status_t param_save_addr(uint16_t addr)
{
    if ( !reg_map_dev_addr_valid(addr) ) {
        return PARAM_ERR_WRITE;
    }
    ++param_stub.save_addr_calls;
    param_stub.last_addr = addr;
    return param_stub.addr_status;
}

void param_set_default_all(void)
{
}
