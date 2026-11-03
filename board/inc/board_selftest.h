#ifndef BOARD_SELFTEST_H
#define BOARD_SELFTEST_H

/**
 * @file board_selftest.h
 * @brief Offline smoke tests for the logging chain, FAL/KVDB and the board
 *        components.
 *
 * The whole module is compiled in only with BOARD_SELFTEST (in the Debug
 * target's defineList); without it there is no code and no API.  The tests
 * run from main() right after board_init() and report over the log chain
 * (SEGGER RTT): one [PASS]/[FAIL] line per item and a
 * "selftest = 0xXXXXXXXX" summary line.  A failing item never stops the run
 * and never raises Q_onError -- the point is to see all of the results.
 *
 * The failure bitmap, one bit per item:
 *
 * | bit | item        | checks                                              |
 * |-----|-------------|-----------------------------------------------------|
 * | 0   | rtt         | SEGGER_RTT_Write() and printf() lines both go out   |
 * | 1   | elog        | one line per level plus elog_raw(), output enabled  |
 * | 2   | fal         | flash device + partition found, partition head read |
 * | 3   | kvdb        | selftest_tmp write / read back / delete round trip  |
 * | 4   | sig24131    | sig24131_read_config() on every channel             |
 * | 5   | analog_input| configure + read_sample on every channel            |
 * | 6   | modbus      | offline ctor/start/address round trip               |
 *
 * There is no SysTick timing test.  The modbus item runs offline by default
 * (the slave is constructed, started, addressed and stopped again, no bus);
 * with BOARD_SELFTEST_MODBUS_ONLINE the instance stays up and
 * board_selftest_serve() answers real master frames forever.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BOARD_SELFTEST

/**
 * @brief Run the offline selftest items.
 *
 * Every item prints its verdict and the summary line ends the run.
 *
 * @return The failure bitmap above; 0 means every item passed.
 */
uint32_t board_selftest_run(void);

/**
 * @brief Stay in the online modbus selftest loop.
 *
 * With BOARD_SELFTEST_MODBUS_ONLINE this never returns: received frames are
 * advanced from the loop context and answered through the temporary register
 * map.  Without that macro it returns immediately.
 */
void board_selftest_serve(void);

#endif /* BOARD_SELFTEST */

#ifdef __cplusplus
}
#endif

#endif
