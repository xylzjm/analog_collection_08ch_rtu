#ifndef TMP75_PORT_FAKE_H_
#define TMP75_PORT_FAKE_H_

#include <stddef.h>
#include <stdint.h>

#include "tmp75.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Scriptable I2C transfer behind the real TMP75 driver. */
typedef struct {
    tmp75_status_t status;   /**< Transfer result to return; TMP75_OK hands out rx_frame. */
    uint16_t calls;          /**< Number of transfers served. */
    uint8_t rx_frame[2];     /**< Bytes returned when a read is requested. */
} tmp75_port_fake_t;

/** @brief Default script: transfers succeed and read 25.0 degC. */
void tmp75_port_fake_init(tmp75_port_fake_t* f);

/**
 * @brief Script the next temperature reads.
 *
 * @param f Fake to program.
 * @param temp_c Temperature the transfer reads hand out.
 */
void tmp75_port_fake_set_temperature(tmp75_port_fake_t* f,
                                      float temp_c);

/** @brief Script every following transfer to fail with TMP75_ERR_IO. */
void tmp75_port_fake_fail(tmp75_port_fake_t* f);

/** @brief Fill a port struct bound to this fake. */
void tmp75_port_fake_port(tmp75_port_fake_t* f,
                           tmp75_port_t* out);

#ifdef __cplusplus
}
#endif

#endif
