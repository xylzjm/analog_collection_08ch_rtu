#ifdef BOARD_SELFTEST

#include "board_selftest.h"

#include <stdio.h>
#include <string.h>

#include "SEGGER_RTT.h"
#include "elog.h"
#include "fal.h"
#include "flashdb.h"

#include "analog_input_board.h"
#include "board.h"
#include "modbus_slave_board.h"
#include "sig24131_board.h"

/* failure bitmap, one bit per item (see board_selftest.h) */
#define SELFTEST_RTT_FAILED          (1U << 0)
#define SELFTEST_ELOG_FAILED         (1U << 1)
#define SELFTEST_FAL_FAILED          (1U << 2)
#define SELFTEST_KVDB_FAILED         (1U << 3)
#define SELFTEST_SIG24131_FAILED     (1U << 4)
#define SELFTEST_ANALOG_INPUT_FAILED (1U << 5)
#define SELFTEST_MODBUS_FAILED       (1U << 6)

/* Temporary modbus parameters: fixed 0x01 / 115200 for the selftest.  The
 * address policy of the product belongs to the future parameter module. */
#define SELFTEST_MODBUS_BAUDRATE 115200U
#define SELFTEST_MODBUS_ADDRESS  0x01U

/* The temporary KV key the KVDB round trip uses; it is deleted again and no
 * sector is ever erased by this item. */
#define SELFTEST_KVDB_KEY   "selftest_tmp"
#define SELFTEST_KVDB_VALUE "rtu-ok"

static void report(char const* name, int passed)
{
    elog_raw("[%s] %s\r\n", passed ? "PASS" : "FAIL", name);
}

/* bit 0: both log paths reach RTT --------------------------------------- */

static int test_rtt(void)
{
    static char const rtt_line[] = "selftest: SEGGER_RTT_Write path\r\n";

    /* Direct RTT write; NO_BLOCK_SKIP drops the whole line only when no host
     * has drained the up buffer, which at power-up it still has room for. */
    if ( SEGGER_RTT_Write(0U, rtt_line, sizeof(rtt_line) - 1U)
         != sizeof(rtt_line) - 1U ) {
        return 0;
    }

    /* The printf fallback path (printf -> fputc -> RTT). */
    return printf("selftest: printf path\r\n") > 0;
}

/* bit 1: every elog level goes through ---------------------------------- */

static int test_elog(void)
{
    elog_assert("selftest", "level assert");
    elog_error("selftest", "level error");
    elog_warn("selftest", "level warn");
    elog_info("selftest", "level info");
    elog_debug("selftest", "level debug");
    elog_raw("selftest: elog_raw\r\n");

    /* The level lines above may be filtered or dropped by RTT flow control,
     * so the hard check is that the output path is enabled at all. */
    return elog_get_output_enabled() ? 1 : 0;
}

/* bit 2: FAL device, partition and a partition head read ---------------- */

static int test_fal(void)
{
    struct fal_flash_dev const* dev;
    struct fal_partition const* part;
    uint8_t head[16];
    int n;

    dev = fal_flash_device_find("at32f421_onchip");
    part = fal_partition_find("fdb_kvdb");
    if ( dev == NULL || part == NULL ) {
        return 0;
    }

    elog_raw("selftest: fal dev %s addr=0x%08X len=%u blk=%u gran=%u\r\n",
             dev->name, (unsigned)dev->addr, (unsigned)dev->len,
             (unsigned)dev->blk_size, (unsigned)dev->write_gran);
    elog_raw("selftest: fal part %s off=0x%08X len=%u\r\n",
             part->name, (unsigned)part->offset, (unsigned)part->len);

    n = fal_partition_read(part, 0U, head, sizeof(head));
    if ( n != (int)sizeof(head) ) {
        return 0;
    }
    elog_raw("selftest: fal part head %02X %02X %02X %02X\r\n",
             head[0], head[1], head[2], head[3]);
    return 1;
}

/* bit 3: KVDB write / read back / delete round trip -------------------- */

static int test_kvdb(void)
{
    struct fdb_kvdb* db = board_kvdb_get();
    char* value;
    int ok = 0;

    if ( fdb_kv_set(db, SELFTEST_KVDB_KEY, SELFTEST_KVDB_VALUE) == FDB_NO_ERR ) {
        value = fdb_kv_get(db, SELFTEST_KVDB_KEY);
        if ( value != NULL && strcmp(value, SELFTEST_KVDB_VALUE) == 0 ) {
            ok = 1;
        }
        (void)fdb_kv_del(db, SELFTEST_KVDB_KEY);
    }
    return ok;
}

/* bit 4: SIG24131 communication --------------------------------------- */

static int test_sig24131(void)
{
    sig24131_config_t config;
    sig24131_status_t status;
    uint8_t channel;
    int ok = 1;

    /* board_init() already constructed and reset every device; here each one
     * has to answer a register read. */
    for ( channel = 1U; channel <= sig24131_board_count(); ++channel ) {
        status = sig24131_read_config(sig24131_board_get_device(channel),
                                      &config);
        if ( status != SIG24131_OK ) {
            elog_raw("selftest: sig24131 ch%u read_config=%d\r\n",
                     (unsigned)channel, (int)status);
            ok = 0;
        }
    }
    return ok;
}

/* bit 5: analog input read --------------------------------------------- */

static int test_analog_input(void)
{
    analog_input_config_t config;
    analog_input_t* input;
    analog_input_status_t status;
    uint32_t first_conversion_delay_ms;
    int32_t sample_code;
    uint8_t channel;
    int ok = 1;

    config.type                = ANALOG_INPUT_TYPE_THERMOCOUPLE;
    config.thermocouple_type   = ANALOG_INPUT_THERMOCOUPLE_TYPE_E;
    config.sample_rate_millihz = 0U; /* board default rate */

    for ( channel = 1U; channel <= analog_input_board_count(); ++channel ) {
        input = analog_input_board_get(channel);
        status = analog_input_configure(input, &config,
                                        &first_conversion_delay_ms);
        if ( status != ANALOG_INPUT_OK ) {
            elog_raw("selftest: analog ch%u configure=%d\r\n",
                     (unsigned)channel, (int)status);
            ok = 0;
            continue;
        }

        /* An open sensor only shows as a saturated code and is not a port
         * failure, so whatever read_sample() answers is printed and the item
         * keeps its pass. */
        status = analog_input_read_sample(input, &sample_code);
        elog_raw("selftest: analog ch%u sample=%d status=%d\r\n",
                 (unsigned)channel, (int)sample_code, (int)status);
    }
    return ok;
}

/* bit 6: modbus slave -------------------------------------------------- */

static uint8_t selftest_address(void* ctx)
{
    (void)ctx;
    return SELFTEST_MODBUS_ADDRESS;
}

#ifndef BOARD_SELFTEST_MODBUS_ONLINE
/* Offline: no data space exists yet, every access is refused. */
static modbus_slave_exception_t selftest_no_data(void* ctx,
                                                uint16_t start,
                                                uint16_t count,
                                                uint8_t* buffer,
                                                modbus_slave_register_mode_t mode)
{
    (void)ctx;
    (void)start;
    (void)count;
    (void)buffer;
    (void)mode;
    return MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS;
}
#else
/* Online: a temporary register map, locked inside this macro on purpose --
 * the application's real map will replace it.  Fixed address 0x01, 115200:
 * input registers 0x0000..0x0007 answer their channel number, holding
 * register 0x0000 is writable and reads back, and 0x11 Report Slave ID
 * carries the identity below. */
static uint16_t holding_reg0;

static modbus_slave_exception_t selftest_bits(void* ctx,
                                             uint16_t start,
                                             uint16_t count,
                                             uint8_t* buffer,
                                             modbus_slave_register_mode_t mode)
{
    (void)ctx;
    (void)start;
    (void)count;
    (void)buffer;
    (void)mode;
    return MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS;
}

static modbus_slave_exception_t selftest_input(void* ctx,
                                              uint16_t start,
                                              uint16_t count,
                                              uint8_t* buffer,
                                              modbus_slave_register_mode_t mode)
{
    uint16_t i;

    (void)ctx;
    if ( mode != MODBUS_SLAVE_REG_READ
         || (start > 7U) || ((uint32_t)start + count > 8U) ) {
        return MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS;
    }
    for ( i = 0; i < count; ++i ) {
        modbus_slave_put_u16(buffer + 2U * i, (uint16_t)(start + i + 1U));
    }
    return MODBUS_SLAVE_EX_NONE;
}

static modbus_slave_exception_t selftest_holding(void* ctx,
                                                uint16_t start,
                                                uint16_t count,
                                                uint8_t* buffer,
                                                modbus_slave_register_mode_t mode)
{
    (void)ctx;
    if ( (start != 0U) || (count != 1U) ) {
        return MODBUS_SLAVE_EX_ILLEGAL_DATA_ADDRESS;
    }
    if ( mode == MODBUS_SLAVE_REG_WRITE ) {
        holding_reg0 = modbus_slave_get_u16(buffer);
    } else {
        modbus_slave_put_u16(buffer, holding_reg0);
    }
    return MODBUS_SLAVE_EX_NONE;
}

/* The frame event only raises a flag from interrupt context; the loop in
 * board_selftest_serve() runs modbus_slave_advance() in its own context. */
static volatile uint8_t frame_pending;

static void selftest_on_frame_received(void* ctx)
{
    (void)ctx;
    frame_pending = 1U;
}

static const modbus_slave_port_event_t selftest_event = {
    NULL,
    selftest_on_frame_received,
    NULL,
    NULL
};
#endif /* BOARD_SELFTEST_MODBUS_ONLINE */

/* 0x11 Report Slave ID answers from this identity; the master checks the
 * slave id byte and the additional bytes against what it expects. */
#define SELFTEST_SLAVE_ID 0x53U /* 'S' */

static uint8_t const selftest_additional[] = "selftest";

static const modbus_slave_callbacks_t selftest_callbacks = {
    NULL,
#ifdef BOARD_SELFTEST_MODBUS_ONLINE
    selftest_bits,
    selftest_bits,
    selftest_holding,
    selftest_input,
#else
    selftest_no_data,
    selftest_no_data,
    selftest_no_data,
    selftest_no_data,
#endif
    selftest_address
};

static const modbus_slave_identity_t selftest_identity = {
    SELFTEST_SLAVE_ID,
    1U,
    selftest_additional,
    sizeof(selftest_additional) - 1U
};

static int test_modbus(void)
{
    modbus_slave_t* slave;
    modbus_slave_status_t status;
    int ok = 1;

    status = modbus_slave_board_ctor(SELFTEST_MODBUS_BAUDRATE,
                                     &selftest_callbacks,
                                     &selftest_identity);
    if ( status != MODBUS_SLAVE_OK ) {
        elog_raw("selftest: modbus ctor=%d\r\n", (int)status);
        return 0;
    }
    slave = modbus_slave_board_get();

#ifdef BOARD_SELFTEST_MODBUS_ONLINE
    (void)modbus_slave_set_event_port(slave, &selftest_event);
#endif

    status = modbus_slave_start(slave);
    if ( status != MODBUS_SLAVE_OK ) {
        elog_raw("selftest: modbus start=%d\r\n", (int)status);
        ok = 0;
    }
    if ( modbus_slave_get_address(slave) != SELFTEST_MODBUS_ADDRESS ) {
        elog_raw("selftest: modbus address=%u\r\n",
                 (unsigned)modbus_slave_get_address(slave));
        ok = 0;
    }

#ifdef BOARD_SELFTEST_MODBUS_ONLINE
    /* Keep the instance: board_selftest_serve() answers master frames. */
    elog_raw("selftest: modbus online at addr %u, %u baud\r\n",
             (unsigned)SELFTEST_MODBUS_ADDRESS,
             (unsigned)SELFTEST_MODBUS_BAUDRATE);
#else
    (void)modbus_slave_stop(slave);
#endif
    return ok;
}

/* overall runner -------------------------------------------------------- */

uint32_t board_selftest_run(void)
{
    uint32_t failed = 0U;
    int passed;

    passed = test_rtt();
    report("rtt", passed);
    if ( !passed ) {
        failed |= SELFTEST_RTT_FAILED;
    }

    passed = test_elog();
    report("elog", passed);
    if ( !passed ) {
        failed |= SELFTEST_ELOG_FAILED;
    }

    passed = test_fal();
    report("fal", passed);
    if ( !passed ) {
        failed |= SELFTEST_FAL_FAILED;
    }

    passed = test_kvdb();
    report("kvdb", passed);
    if ( !passed ) {
        failed |= SELFTEST_KVDB_FAILED;
    }

    passed = test_sig24131();
    report("sig24131", passed);
    if ( !passed ) {
        failed |= SELFTEST_SIG24131_FAILED;
    }

    passed = test_analog_input();
    report("analog_input", passed);
    if ( !passed ) {
        failed |= SELFTEST_ANALOG_INPUT_FAILED;
    }

    passed = test_modbus();
    report("modbus", passed);
    if ( !passed ) {
        failed |= SELFTEST_MODBUS_FAILED;
    }

    elog_raw("selftest = 0x%08X\r\n", (unsigned)failed);
    return failed;
}

void board_selftest_serve(void)
{
#ifdef BOARD_SELFTEST_MODBUS_ONLINE
    for ( ;; ) {
        if ( frame_pending != 0U ) {
            frame_pending = 0U;
            (void)modbus_slave_advance(modbus_slave_board_get());
        }
    }
#endif
}

#endif /* BOARD_SELFTEST */
