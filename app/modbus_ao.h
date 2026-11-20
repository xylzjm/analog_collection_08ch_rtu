#ifndef MODBUS_AO_H
#define MODBUS_AO_H

/**
 * @file modbus_ao.h
 * @brief Modbus RTU slave active object: routing, mirror and persistence.
 *
 * AoModbus owns the typed mirror of the data space, the holding-register
 * write transaction (validate, synchronous commit with read-back verify,
 * then mirror) and the parameter persistence behind the kv_store_t
 * interface (library/interface/kv_store). reg_map is the pure wire map it
 * calls. See docs/ao/modbus_ao.md.
 */

#include "app.h"
#include "kv_store.h"
#include "modbus_slave.h"
#include "qpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Queue depth (telemetry round plus frame plus margin). */
#define MODBUS_AO_QUEUE_LEN 16U

/** @brief Configuration-area mirror: committed configurations and address
 *  (the change-check base; written only in AoModbus's RTC). */
typedef struct {
    ch_config_t cfg[CH_CONFIG_CHANNEL_COUNT];  /**< Last committed configuration per channel. */
    uint8_t  cfg_ok[CH_CONFIG_CHANNEL_COUNT];  /**< cfg[ch] is committed & usable. */
    uint8_t  applied[CH_CONFIG_CHANNEL_COUNT]; /**< Channel accepted cfg[ch] (CFG_APPLIED_SIG). */
    uint16_t addr;        /**< Effective slave address. */
    uint8_t  addr_ok;     /**< addr is committed & usable. */
} modbus_ao_config_image_t;

/** @brief Telemetry-area mirror: latest telemetry feeding the input registers. */
typedef struct {
    float    value[CH_CONFIG_CHANNEL_COUNT];  /**< Latest measured values. */
    uint16_t status[CH_CONFIG_CHANNEL_COUNT]; /**< Channel status codes (ch_status_t). */
    float    cjc_temp;    /**< Latest cold junction temperature. */
    uint8_t  cjc_valid;   /**< Fault word b0 source. */
} modbus_ao_telemetry_image_t;

/** @brief Parameter persistence: the injected handle and its outcome state. */
typedef struct {
    kv_store_t* kv;         /**< Borrowed store handle; NULL = unbound. */
    uint8_t  db_ok;         /**< Store opened; fault word b3 = !db_ok. */
    uint8_t  commit_failed; /**< Latch: last commit failed (fault word b1). */
} modbus_ao_store_t;

/** @brief Runtime diagnostics (queue watermarks / TX counters). */
typedef struct {
    uint16_t tx_done_cnt; /**< Replies sent. */
    uint16_t tx_timeout_cnt; /**< TX watchdog events. */
    uint16_t hb_ctr;      /**< CJC heartbeats since the last water-level report. */
} modbus_ao_diag_t;

/** @brief AoModbus active object: protocol routing, mirror and persistence. */
typedef struct {
    QActive                     super;     /**< Active object base. */
    modbus_slave_t*             slave;     /**< Slave core; built in the ctor. */
    modbus_ao_config_image_t    config;    /**< Configuration-area mirror. */
    modbus_ao_telemetry_image_t telemetry; /**< Telemetry-area mirror. */
    modbus_ao_store_t           store;     /**< KV persistence handle & state. */
    modbus_ao_diag_t            diag;      /**< Diagnostics. */
} modbus_ao_t;

/**
 * @brief Construct the Modbus active object and its slave core in one step.
 *
 * @param me Storage owned by the caller.
 * @param kv KV store handle; borrowed, must outlive this AO. NULL leaves the
 *        store unbound (`store.db_ok` = 0, fault b3).
 * @param slave Caller-owned slave storage (board's static instance); required.
 * @param slave_cfg Platform half of modbus_slave_config_t: line/timer/critical
 *        ports, rx/tx buffers, baudrate; required. The callbacks and identity
 *        fields are overwritten by this module and their input values are
 *        ignored.
 */
void modbus_ao_ctor(modbus_ao_t* me,
                    kv_store_t* kv,
                    modbus_slave_t* slave,
                    const modbus_slave_config_t* slave_cfg);

#ifdef __cplusplus
}
#endif

#endif
