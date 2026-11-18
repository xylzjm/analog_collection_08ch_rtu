#ifndef PARAM_STUB_H_
#define PARAM_STUB_H_

#include <stdint.h>

#include "param.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Scriptable replacement of the param component for host tests. */
typedef struct {
    param_status_t cfg_status;   /**< Result every param_save_cfg() returns. */
    param_status_t addr_status;  /**< Result every param_save_addr() returns. */
    param_boot_result_t boot;    /**< Outcome param_boot_load() hands out. */
    uint16_t save_cfg_calls;     /**< param_save_cfg() invocations. */
    uint16_t save_addr_calls;    /**< param_save_addr() invocations. */
    uint8_t last_ch;            /**< Zero-based channel of the last save_cfg. */
    ch_config_t last_cfg;       /**< Last saved configuration. */
    uint16_t last_addr;         /**< Last saved slave address. */
} param_stub_state_t;

/** @brief The one stub state; the tests script it directly. */
extern param_stub_state_t param_stub;

/**
 * @brief Reset the script: boot fully valid, every save succeeds.
 *
 * The materialized boot configuration is type K at 10 Hz with unit
 * calibration on every channel, db_ok, slave address 1.
 */
void param_stub_reset(void);

/**
 * @brief Script the boot load outcome.
 *
 * @param boot Result structure copied verbatim by param_boot_load().
 */
void param_stub_set_boot(const param_boot_result_t* boot);

/**
 * @brief Script the commit results of the following saves.
 *
 * @param cfg Result returned by param_save_cfg().
 * @param addr Result returned by param_save_addr().
 */
void param_stub_set_save(param_status_t cfg,
                         param_status_t addr);

#ifdef __cplusplus
}
#endif

#endif
