#ifndef REG_MAP_H
#define REG_MAP_H

/**
 * @file reg_map.h
 * @brief Modbus data-space map: addressing, span table and register codecs.
 *
 * Pure functions, no state. The typed mirror and the write transaction live
 * in modbus_image (owned by AoModbus). Data definitions (addresses, encodings,
 * write validation) follow docs/ao/modbus_ao.md section 4.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ch_config.h"
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

/** @brief Lowest legal Modbus slave address (single source: the core). */
#define REG_MAP_DEV_ADDR_MIN MODBUS_SLAVE_ADDRESS_MIN

/** @brief Highest legal Modbus slave address (single source: the core). */
#define REG_MAP_DEV_ADDR_MAX MODBUS_SLAVE_ADDRESS_MAX

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
 * @brief Find the span wholly containing one address range.
 *
 * @param space Data space to search.
 * @param start First address (0-based).
 * @param count Number of registers or bits; must be >= 1.
 * @return The containing span, or NULL when the range is empty, crosses a
 *         block boundary, falls into an unmapped gap or leaves the space.
 */
const reg_span_t* reg_map_find(reg_space_t space,
                               uint16_t start,
                               uint16_t count);

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
 * ch_config_validate() rejects it; semantic validation stays in ch_config.
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

/**
 * @brief Report whether a slave address is legal.
 *
 * @param addr Address to test.
 * @return true for 1..247, false otherwise.
 */
bool reg_map_dev_addr_valid(uint16_t addr);

/*----------------------------------------------------------------------------*/
/* Input area                                                                 */
/*----------------------------------------------------------------------------*/

/**
 * @brief Typed mirror of the input register area.
 *
 * Reserved registers read back as zero and need no mirror fields.
 */
typedef struct {
    float value[CH_CONFIG_CHANNEL_COUNT];     /**< Latest measured value per channel. */
    uint16_t status[CH_CONFIG_CHANNEL_COUNT]; /**< Channel status code per channel. */
    float cjc_temp;                           /**< Cold junction temperature. */
    uint16_t fault_word;                      /**< Device fault word (register 68). */
} reg_map_input_image_t;

/**
 * @brief Assemble input registers from the typed mirror.
 *
 * @param in Typed image to read from.
 * @param start First register address (0-based).
 * @param count Number of registers.
 * @param buf Receives count * 2 bytes in wire order.
 */
void reg_map_input_encode(const reg_map_input_image_t* in,
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

/*----------------------------------------------------------------------------*/
/* Channel status codes (register encoding, channel_ao.md section 3)          */
/*----------------------------------------------------------------------------*/

/** @brief Channel status register codes. */
typedef enum {
    REG_MAP_STATUS_OK               = 0, /**< Valid measurement. */
    REG_MAP_STATUS_UNCONFIGURED     = 1, /**< No effective configuration. */
    REG_MAP_STATUS_TC_BREAK         = 2, /**< Thermocouple loop open. */
    REG_MAP_STATUS_RTD_LEAD_BREAK   = 3, /**< PT100 excitation lead open. */
    REG_MAP_STATUS_RTD_RETURN_BREAK = 4, /**< PT100 return lead open. */
    REG_MAP_STATUS_NO_CJC           = 5, /**< Cold junction invalid (TC). */
    REG_MAP_STATUS_HW_ERROR         = 6, /**< Hardware or IO error. */
    REG_MAP_STATUS_OUT_OF_RANGE     = 7  /**< Conversion over range. */
} reg_map_status_t;

/**
 * @brief Map a wire break check result to the channel status code.
 *
 * The wire status argument uses the analog_input_wire_status_t encoding
 * (0 normal, 1 broken unresolvable, 2 broken lead, 3 broken return).
 *
 * @param cfg_type Channel input type (ch_type_t value).
 * @param wire_status Wire check result encoding.
 * @return The status code: 0 for normal, 2 for any thermocouple break, 3/4
 *         for a resolvable PT100 break, 6 for an unresolvable PT100 break
 *         (front-end contract violation), a non-detectable type or an
 *         unknown wire code.
 */
uint16_t reg_map_wire_status_code(uint8_t cfg_type,
                                  uint8_t wire_status);

#ifdef __cplusplus
}
#endif

#endif
