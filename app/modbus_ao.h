#ifndef MODBUS_AO_H
#define MODBUS_AO_H

/**
 * @file modbus_ao.h
 * @brief Modbus RTU slave active object: routing, mirror and persistence.
 *
 * AoModbus owns the typed mirror of the data space, the holding-register
 * write transaction (validate, synchronous commit with read-back verify,
 * then mirror) and the parameter persistence behind the modbus_kv_ops_t
 * seam. reg_map is the pure wire map it calls. See docs/ao/modbus_ao.md.
 */

#include <stddef.h>

#include "app.h"
#include "modbus_slave.h"
#include "qpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Queue depth (telemetry round plus frame plus margin). */
#define MODBUS_AO_QUEUE_LEN 16U

/** @brief One factory-default key/value pair of the parameter schema. */
typedef struct {
    const char* key;   /**< NUL-terminated key name. */
    const void* value; /**< Default value body. */
    size_t len;        /**< Value length in bytes. */
} modbus_kv_default_t;

/**
 * @brief Key-value store operations behind the persistence seam.
 *
 * Every operation runs in the caller's execution context (the AoModbus RTC
 * step); implementations add no locking of their own.
 */
typedef struct {
    /**
     * @brief Open the store and materialize missing default keys.
     *
     * @param ctx Implementation context; may be NULL.
     * @param defaults Default table; may be NULL when count is 0.
     * @param count Number of entries in @p defaults.
     * @return 0 on success, a negative value on failure.
     */
    int (*init)(void* ctx, const modbus_kv_default_t* defaults, size_t count);

    /**
     * @brief Read one value.
     *
     * @param ctx Implementation context; may be NULL.
     * @param key NUL-terminated key name.
     * @param buf Destination buffer.
     * @param cap Destination capacity in bytes.
     * @return The stored length (which may exceed @p cap, in which case the
     *         copy is truncated), or a negative value when the key is missing
     *         or the read failed.
     */
    int (*get)(void* ctx, const char* key, void* buf, size_t cap);

    /**
     * @brief Write one value.
     *
     * @param ctx Implementation context; may be NULL.
     * @param key NUL-terminated key name.
     * @param buf Value body.
     * @param len Value length in bytes.
     * @return 0 on success, a negative value on failure.
     */
    int (*set)(void* ctx, const char* key, const void* buf, size_t len);
} modbus_kv_ops_t;

/** @brief AoModbus active object: protocol routing, mirror and persistence. */
typedef struct {
    QActive super;                /**< Active object base. */
    modbus_slave_t* slave;        /**< Slave instance built by the ctor; NULL in data-only mode. */
    modbus_slave_port_event_t ev; /**< ISR event port routed into this AO. */
    modbus_slave_callbacks_t cb;  /**< Data-space callbacks bound to this instance. */
    ch_config_t cfg[CH_CONFIG_CHANNEL_COUNT];  /**< Committed configs; also the change-check base. */
    uint8_t  cfg_ok[CH_CONFIG_CHANNEL_COUNT];  /**< cfg[ch] committed & usable. */
    uint8_t  applied[CH_CONFIG_CHANNEL_COUNT]; /**< Channel accepted cfg[ch]. */
    float    value[CH_CONFIG_CHANNEL_COUNT];   /**< Latest measured values. */
    uint16_t status[CH_CONFIG_CHANNEL_COUNT];  /**< Channel status codes (ch_status_t). */
    float    cjc_temp;   /**< Latest cold junction temperature. */
    uint8_t  cjc_valid;  /**< Cold junction validity (fault word b0). */
    uint16_t addr;       /**< Effective slave address; also the change-check base. */
    uint8_t  addr_ok;    /**< addr committed & usable. */
    uint8_t  b1;         /**< Latch: commit failure (fault word b1). */
    uint8_t  db_ok;      /**< Store opened (fault word b3 = !db_ok). */
    const modbus_kv_ops_t* kv; /**< Storage backend; NULL leaves it unbound. */
    void* kv_ctx;              /**< Context handed to every storage operation. */
    uint16_t tx_done_cnt;      /**< Diagnostic: replies sent. */
    uint16_t tx_timeout_cnt;   /**< Diagnostic: TX watchdog events. */
    uint16_t diag_ctr;         /**< CJC heartbeats since the last water-level report. */
} modbus_ao_t;

/**
 * @brief Construct the Modbus active object and its slave core in one step.
 *
 * @param me Storage owned by the caller.
 * @param kv KV storage port; NULL leaves the store unbound (db_ok = 0, fault b3).
 * @param kv_ctx Context handed to every kv operation.
 * @param slave Caller-owned slave storage (board's static instance). NULL and
 *        @p slave_cfg NULL together select data-only mode (no slave core; for
 *        callback-level tests). Mixing NULL and non-NULL is a defect (Q_ASSERT).
 * @param slave_cfg Platform half of modbus_slave_config_t: line/timer/critical
 *        ports, rx/tx buffers, baudrate. The callbacks and identity fields are
 *        overwritten by this module and their input values are ignored.
 */
void modbus_ao_ctor(modbus_ao_t* me,
                    const modbus_kv_ops_t* kv,
                    void* kv_ctx,
                    modbus_slave_t* slave,
                    const modbus_slave_config_t* slave_cfg);

#ifdef __cplusplus
}
#endif

#endif
