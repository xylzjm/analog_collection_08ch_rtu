#ifndef APP_H
#define APP_H

/**
 * @file app.h
 * @brief Application vocabulary: channel model, signals and event payloads.
 *
 * Shared by every active object and by the pure register map. Signal set
 * and payload layouts follow docs/qp-application-design.md section 4
 * (event contract). Static events carry no parameters: parameterless ones
 * are file-scope `QEvt const` (QEVT_INITIALIZER), time events are QTimeEvt
 * members of their AO (QP/C example form).
 */

#include <stdint.h>

#include "board_config.h"
#include "qpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*/
/* Channel model                                                              */
/*----------------------------------------------------------------------------*/

/** @brief Number of measurement channels (board hardware fact, not an app choice). */
#define CH_CONFIG_CHANNEL_COUNT BOARD_CHANNEL_COUNT

/** @brief Input type codes stored in ch_config_t::type. */
typedef enum {
    CH_TYPE_TC     = 0, /**< Thermocouple. */
    CH_TYPE_PT100  = 1, /**< RTD. */
    CH_TYPE_4_20MA = 2, /**< Current input. */
    CH_TYPE_0_10V  = 3  /**< Voltage input. */
} ch_type_t;

/** @brief Thermocouple type codes stored in ch_config_t::tc_type. */
typedef enum {
    CH_TC_TYPE_B = 0,
    CH_TC_TYPE_E = 1,
    CH_TC_TYPE_J = 2,
    CH_TC_TYPE_K = 3,
    CH_TC_TYPE_N = 4,
    CH_TC_TYPE_R = 5,
    CH_TC_TYPE_S = 6,
    CH_TC_TYPE_T = 7
} ch_tc_type_t;

/** @brief tc_type value meaning "not a thermocouple" (normalization result). */
#define CH_TC_TYPE_NONE 0xFFU

/**
 * @brief Sample rates of the rate codes: one row per rate code.
 *
 * X(code, millihertz): the name column builds rate_code_t, the value
 * column builds the driver rate table (it names the board tier constants
 * and is only expanded where those are visible).
 */
#define APP_RATE_LIST(X)                          \
    X(CH_RATE_2_5_HZ, ANALOG_INPUT_BOARD_RATE_2_5_HZ)   \
    X(CH_RATE_5_HZ, ANALOG_INPUT_BOARD_RATE_5_HZ)       \
    X(CH_RATE_10_HZ, ANALOG_INPUT_BOARD_RATE_10_HZ)     \
    X(CH_RATE_12_5_HZ, ANALOG_INPUT_BOARD_RATE_12_5_HZ) \
    X(CH_RATE_15_HZ, ANALOG_INPUT_BOARD_RATE_15_HZ)     \
    X(CH_RATE_20_HZ, ANALOG_INPUT_BOARD_RATE_20_HZ)     \
    X(CH_RATE_25_HZ, ANALOG_INPUT_BOARD_RATE_25_HZ)     \
    X(CH_RATE_30_HZ, ANALOG_INPUT_BOARD_RATE_30_HZ)     \
    X(CH_RATE_40_HZ, ANALOG_INPUT_BOARD_RATE_40_HZ)     \
    X(CH_RATE_50_HZ, ANALOG_INPUT_BOARD_RATE_50_HZ)     \
    X(CH_RATE_60_HZ, ANALOG_INPUT_BOARD_RATE_60_HZ)     \
    X(CH_RATE_80_HZ, ANALOG_INPUT_BOARD_RATE_80_HZ)     \
    X(CH_RATE_100_HZ, ANALOG_INPUT_BOARD_RATE_100_HZ)

/** @brief Sample rate codes; the register encoding of the sample rate. */
typedef enum {
#define X(code, millihertz) code,
    APP_RATE_LIST(X)
#undef X
} rate_code_t;

/** @brief Highest valid rate code (inclusive); 0..CH_CONFIG_RATE_CODE_MAX. */
#define CH_CONFIG_RATE_CODE_MAX CH_RATE_100_HZ

/** @brief Number of entries in the sample rate tables. */
#define CH_CONFIG_RATE_CODE_COUNT (CH_CONFIG_RATE_CODE_MAX + 1U)

/* Drift guard: the bound must match the APP_RATE_LIST row count. */
#define APP_RATE_COUNT_ONE(code, millihertz) +1U
Q_ASSERT_STATIC((0U APP_RATE_LIST(APP_RATE_COUNT_ONE)) == CH_CONFIG_RATE_CODE_COUNT);
#undef APP_RATE_COUNT_ONE

/**
 * @brief One channel's configuration, 20 bytes, no padding, no version field.
 *
 * This body IS the KV value format (key "cfg/chN") and the source of the
 * register block encoding, so the layout must stay stable. Validation and
 * normalization are one operation: semantically equal configurations must
 * compare equal byte for byte (the change check in modbus_ao.c's write
 * pipeline relies on memcmp). The contract, enforced by the validation in
 * modbus_ao.c: type must be CH_TYPE_*; tc_type must be CH_TC_TYPE_B..T for
 * thermocouples and is normalized to CH_TC_TYPE_NONE otherwise; rate_code
 * must be 0..CH_CONFIG_RATE_CODE_MAX; all four floats must be finite;
 * linear_min/linear_max are normalized to 0.0 for non-linear input types.
 */
typedef struct {
    uint8_t  type;      /**< Input type, ch_type_t. */
    uint8_t  tc_type;   /**< Thermocouple type (CH_TC_TYPE_*) or CH_TC_TYPE_NONE. */
    uint16_t rate_code; /**< Sample rate code, rate_code_t (0..12). */
    float    linear_min; /**< Linear input low limit, engineering value. */
    float    linear_max; /**< Linear input high limit, engineering value. */
    float    offset;     /**< Calibration offset: value = raw * gain + offset. */
    float    gain;       /**< Calibration gain. */
} ch_config_t;

/* Compile-time guard: the struct body is the 20 byte persistence format. */
Q_ASSERT_STATIC(sizeof(ch_config_t) == 20U);

/** @brief Channel status register codes (wire values frozen). */
typedef enum {
    CH_STATUS_OK               = 0, /**< Valid measurement. */
    CH_STATUS_UNCONFIGURED     = 1, /**< No effective configuration. */
    CH_STATUS_TC_BREAK         = 2, /**< Thermocouple loop open. */
    CH_STATUS_RTD_LEAD_BREAK   = 3, /**< PT100 excitation lead open. */
    CH_STATUS_RTD_RETURN_BREAK = 4, /**< PT100 return lead open. */
    CH_STATUS_NO_CJC           = 5, /**< Cold junction invalid (TC). */
    CH_STATUS_HW_ERROR         = 6, /**< Hardware or IO error. */
    CH_STATUS_OUT_OF_RANGE     = 7  /**< Conversion over range. */
} ch_status_t;

/*----------------------------------------------------------------------------*/
/* Signals and event payloads                                                 */
/*----------------------------------------------------------------------------*/

/** @brief Application signals. */
typedef enum {
    CONFIG_SET_SIG = Q_USER_SIG, /**< {ch, cfg}: effective configuration. */
    APPLY_SIG,                   /**< Reconfiguration re-entry (static). */
    CFG_APPLIED_SIG,             /**< {ch}: channel accepted its configuration. */
    MEASURE_SIG,                 /**< {ch, value, status}: one sample. */
    CJC_SIG,                     /**< {temp_c, valid}: cold junction heartbeat. */
    SAMPLE_SIG,                  /**< Sampling tick (time event). */
    SETTLE_SIG,                  /**< Settle/check deadline (time event). */
    BREAK_WINDOW_SIG,            /**< 3 s wire break window (time event). */
    TICK_SIG,                    /**< Cold junction 1 Hz tick (time event). */
    FRAME_SIG,                   /**< Modbus frame captured (static). */
    TX_DONE_SIG,                 /**< Modbus reply sent (static). */
    TX_TIMEOUT_SIG,              /**< Modbus TX watchdog fired (static). */
    MAX_SIG                      /**< Number of signals (psInit bound). */
} app_signal_t;

/** @brief CONFIG_SET_SIG payload (pool B). */
typedef struct {
    QEvt        super; /**< Event header. */
    uint8_t     ch;    /**< Zero-based channel index. */
    ch_config_t cfg;   /**< New effective configuration. */
} config_set_evt_t;

/** @brief MEASURE_SIG payload (pool A). */
typedef struct {
    QEvt     super;  /**< Event header. */
    uint8_t  ch;     /**< Zero-based channel index. */
    float    value;  /**< Calibrated engineering value (frozen on faults). */
    uint16_t status; /**< Channel status code (ch_status_t). */
} measure_evt_t;

/** @brief CFG_APPLIED_SIG payload (pool A). */
typedef struct {
    QEvt    super; /**< Event header. */
    uint8_t ch;    /**< Zero-based channel index. */
} cfg_applied_evt_t;

/** @brief CJC_SIG payload (pool A). */
typedef struct {
    QEvt    super;   /**< Event header. */
    float   temp_c;  /**< Latest valid cold junction temperature. */
    uint8_t valid;   /**< Non-zero when temp_c may be used. */
} cjc_evt_t;

/* Pool A holds MEASURE_SIG, CFG_APPLIED_SIG and CJC_SIG; measure_evt_t
 * must be the largest block. */
Q_ASSERT_STATIC(sizeof(cfg_applied_evt_t) <= sizeof(measure_evt_t));
Q_ASSERT_STATIC(sizeof(cjc_evt_t) <= sizeof(measure_evt_t));

/*----------------------------------------------------------------------------*/
/* Active object priorities                                                   */
/*----------------------------------------------------------------------------*/

/** @brief Priority of the first AoChannel (channels take first..first+N-1). */
#define APP_PRIO_CH_FIRST 2U

/** @brief Priority of AoColdJunction. */
#define APP_PRIO_CJC 10U

/**
 * @brief Priority of AoModbus; must stay the highest of all active objects.
 *
 * The QV capacity argument of docs/qp-application-design.md section 7
 * depends on APP_PRIO_MODBUS being the highest: AoModbus must dispatch a
 * published event before any other AO produces the next one.
 */
#define APP_PRIO_MODBUS 11U

/*----------------------------------------------------------------------------*/
/* Event pools                                                                */
/*----------------------------------------------------------------------------*/

/**
 * @brief Pool A block count (MEASURE/CFG_APPLIED/CJC events).
 *
 * Sized by the capacity table of docs/qp-application-design.md section 7.
 */
#define APP_POOL_A_BLOCK_NUM 16U

/**
 * @brief Pool B block count (CONFIG_SET events).
 *
 * Sized by the capacity table of docs/qp-application-design.md section 7.
 */
#define APP_POOL_B_BLOCK_NUM 16U

/*----------------------------------------------------------------------------*/
/* Board position drift guards                                                */
/*----------------------------------------------------------------------------*/

/*
 * The position list itself and its derived count (BOARD_POSITIONS,
 * BOARD_POSITION_COUNT) are board facts and live in board_config.h; the
 * count is expanded from the list there, so no drift guard is possible or
 * needed. Only the compile-time guards stay here: they need
 * Q_ASSERT_STATIC from qpc.h.
 */

/* Drift guard: the SKU channel count must fit the board position list; the
 * modbus_ao bitmasks and the KV key table index by position. */
Q_ASSERT_STATIC(CH_CONFIG_CHANNEL_COUNT <= BOARD_POSITION_COUNT);

#ifdef __cplusplus
}
#endif

#endif
