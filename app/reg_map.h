#ifndef REG_MAP_H
#define REG_MAP_H

/**
 * @file reg_map.h
 * @brief Modbus data-space map: addressing, span table and register codecs.
 *
 * Pure functions, no state. The typed mirror and the write transaction live
 * in modbus_ao.c (owned by AoModbus). Data definitions (addresses,
 * encodings) follow docs/ao/modbus_ao.md section 4.
 */

#include <stdint.h>

#include "app.h"
#include "modbus_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*/
/* Geometry                                                                   */
/*----------------------------------------------------------------------------*/

/** @brief Registers per channel configuration block (holding). */
#define REG_MAP_CH_CFG_LEN 10U

/** @brief Base address of the device register block (holding). */
#define REG_MAP_DEV_BASE 128U

/** @brief Registers per channel telemetry block (input). */
#define REG_MAP_CH_IN_LEN 8U

/** @brief Address stride between channel telemetry blocks (input). */
#define REG_MAP_CH_IN_STRIDE 8U

/** @brief Base address of the system block (input). */
#define REG_MAP_SYS_BASE 64U

/** @brief Factory default Modbus slave address (single source: the core). */
#define REG_MAP_DEFAULT_DEV_ADDR MODBUS_SLAVE_ADDRESS_MIN

/*----------------------------------------------------------------------------*/
/* Data spaces and spans                                                      */
/*----------------------------------------------------------------------------*/

/** @brief Modbus four data spaces. */
typedef enum {
    REG_SPACE_COIL = 0, /**< Coils (FC01/05/15), bit read/write. */
    REG_SPACE_DISCRETE, /**< Discrete inputs (FC02), bit read only. */
    REG_SPACE_HOLDING,  /**< Holding registers (FC03/06/16), register read/write. */
    REG_SPACE_INPUT     /**< Input registers (FC04), register read only. */
} reg_space_t;

/** @brief Span kinds: drive codec dispatch. */
typedef enum {
    REG_SPAN_CH_CFG = 0, /**< Channel configuration block (10 registers). */
    REG_SPAN_DEV,        /**< Device registers (slave address). */
    REG_SPAN_CH_IN,      /**< Channel telemetry block (8 registers). */
    REG_SPAN_SYS         /**< System block (CJC + fault word). */
} reg_span_kind_t;

/** @brief One contiguous mapped region (span). */
typedef struct {
    reg_space_t space;    /**< Data space this span belongs to. */
    uint16_t base;        /**< First address (0-based). */
    uint16_t n;           /**< Length in registers or bits. */
    reg_span_kind_t kind; /**< Span kind; drives codec dispatch. */
    uint8_t channel;      /**< Zero-based channel, or 0xFF for non-channel spans. */
} reg_span_t;

/**
 * @brief Unified callback entry: exception code plus the hit span.
 *
 * @param space Data space being accessed.
 * @param start First address (0-based).
 * @param count Number of registers or bits.
 * @param out Receives the hit span on success; may be NULL.
 * @return MODBUS_SLAVE_EX_NONE on a hit, MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS
 *         (02) for a range that is not inside a single span, or
 *         MODBUS_SLAVE_EX_ILLEGAL_FUNCTION (01) when the space has no spans
 *         at all (this product: coils and discrete inputs).
 */
modbus_slave_exception_t reg_map_check(reg_space_t space,
                                       uint16_t start,
                                       uint16_t count,
                                       const reg_span_t** out);

/*----------------------------------------------------------------------------*/
/* Channel configuration block codec (10 registers)                           */
/*----------------------------------------------------------------------------*/

/**
 * @brief Encode a typed configuration into 10 big-endian registers.
 *
 * @param cfg Configuration to encode; need not be validated or normalized.
 * @param buf Receives REG_MAP_CH_CFG_LEN * 2 bytes in wire order.
 */
void reg_map_ch_encode(const ch_config_t* cfg,
                       uint8_t* buf);

/**
 * @brief Decode 10 big-endian registers into a typed configuration.
 *
 * Layout only: an input type code above 10 decodes to type 0xFF so that
 * the validation in modbus_ao.c rejects it; semantic validation stays
 * there (contract documented on ch_config_t in app.h).
 *
 * @param buf REG_MAP_CH_CFG_LEN * 2 bytes in wire order.
 * @param cfg Receives the decoded configuration.
 */
void reg_map_ch_decode(const uint8_t* buf,
                       ch_config_t* cfg);

/*----------------------------------------------------------------------------*/
/* Device register codec (1 register)                                         */
/*----------------------------------------------------------------------------*/

/** @brief Encode the slave address register. */
void reg_map_dev_encode(uint16_t addr,
                        uint8_t* buf);

/** @brief Decode the slave address register. */
uint16_t reg_map_dev_decode(const uint8_t* buf);

/*----------------------------------------------------------------------------*/
/* Input area                                                                 */
/*----------------------------------------------------------------------------*/

/**
 * @brief Assemble input registers from the input-area state.
 *
 * The parameter lists are the raw state of the input area (the mirror the
 * caller owns); reserved registers read back as zero and need no state.
 * Dispatch runs on span->kind exactly like the holding path: REG_SPAN_CH_IN
 * uses span->channel and the offset from span->base, REG_SPAN_SYS assembles
 * CJC and the fault word; any other kind reads back as zero.
 *
 * @param value Latest measured value per channel (CH_CONFIG_CHANNEL_COUNT).
 * @param status Channel status code per channel.
 * @param cjc_temp Cold junction temperature.
 * @param fault_word Device fault word (register 68).
 * @param span Hit span of the range, as reg_map_check() reports it; must
 *        contain [start, start + count).
 * @param start First register address (0-based).
 * @param count Number of registers.
 * @param buf Receives count * 2 bytes in wire order.
 */
void reg_map_input_encode(const float* value,
                          const uint16_t* status,
                          float cjc_temp,
                          uint16_t fault_word,
                          const reg_span_t* span,
                          uint16_t start,
                          uint16_t count,
                          uint8_t* buf);

/*----------------------------------------------------------------------------*/
/* Device fault word                                                          */
/*----------------------------------------------------------------------------*/

/** @brief Fault word bit 0: cold junction invalid or not ready. */
#define REG_MAP_FAULT_CJC 0x0001U

/** @brief Fault word bit 1: parameter write failure (latched). */
#define REG_MAP_FAULT_PARAM_WRITE 0x0002U

/** @brief Fault word bit 2: parameter load failure (latched). */
#define REG_MAP_FAULT_PARAM_LOAD 0x0004U

/** @brief Fault word bit 3: FlashDB initialization failure (latched). */
#define REG_MAP_FAULT_FDB_INIT 0x0008U

/** @brief Mask of the latched fault bits a caller may pass in. */
#define REG_MAP_FAULT_LATCHED_MASK \
    (REG_MAP_FAULT_PARAM_WRITE | REG_MAP_FAULT_PARAM_LOAD | REG_MAP_FAULT_FDB_INIT)

/**
 * @brief Synthesize the device fault word register value.
 *
 * @param cjc_valid Non-zero when the cold junction reading is valid.
 * @param latched Latched bits, any of REG_MAP_FAULT_PARAM_WRITE,
 *        REG_MAP_FAULT_PARAM_LOAD and REG_MAP_FAULT_FDB_INIT.
 * @return The register value; bit 0 reflects cold junction validity in real
 *         time and bits 1..3 carry the latched state. Bits 4..15 stay zero.
 */
uint16_t reg_map_fault_word(uint8_t cjc_valid,
                            uint16_t latched);

#ifdef __cplusplus
}
#endif

#endif
