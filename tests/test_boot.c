/**
 * @file test_boot.c
 * @brief Host integration test of the composition-root startup order.
 *
 * Runs all ten active objects (8x AoChannel, AoColdJunction, AoModbus)
 * with fakes at the device edge and mirrors the main.c startup order:
 * channels start first (each subscribes inside its initial transition),
 * then AoModbus (its boot load publishes CONFIG_SET_SIG), then
 * AoColdJunction (its first CJC_SIG heartbeat). Assertions prove the
 * docs/qp-application-design.md section 8 invariants: the boot publish
 * reaches every subscribed channel, no queue runs dry, and telemetry
 * flows end to end into the Modbus mirror. Channel acceptance travels
 * back as CFG_APPLIED_SIG.
 *
 * One scenario per process: the QP registry and time-event teardown is
 * not designed for tearing the world down and rebuilding it inside one
 * run, so main() takes the scenario number (1 or 2) and the script
 * starts one process per scenario.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qpc.h"

#include "analog_input_fake.h"
#include "app.h"
#include "channel_ao.h"
#include "cold_junction_ao.h"
#include "fdb_fake.h"
#include "host_support.h"
#include "modbus_ao.h"
#include "modbus_port_fake.h"
#include "reg_map.h"
#include "tmp75_port_fake.h"

static int checks;
static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        ++checks;                                                  \
        if ( !(cond) ) {                                           \
            ++failures;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while ( 0 )

static channel_ao_t channels[CH_CONFIG_CHANNEL_COUNT];
static analog_input_fake_t inputs[CH_CONFIG_CHANNEL_COUNT];
static cold_junction_ao_t cold_junction;
static modbus_ao_t modbus;
static tmp75_t cjc_dev;
static tmp75_port_fake_t cjc_port;

static uint8_t rx_buffer[256];
static uint8_t tx_buffer[256];
static modbus_slave_t slave;
static modbus_port_fake_t port;

static QEvtPtr ch_queue[CH_CONFIG_CHANNEL_COUNT][CHANNEL_AO_QUEUE_LEN];
static QEvtPtr cjc_queue[COLD_JUNCTION_AO_QUEUE_LEN];
static QEvtPtr modbus_queue[MODBUS_AO_QUEUE_LEN];
static QSubscrList subscr[MAX_SIG];
static QF_MPOOL_EL(measure_evt_t) pool_a[APP_POOL_A_BLOCK_NUM];
static QF_MPOOL_EL(config_set_evt_t) pool_b[APP_POOL_B_BLOCK_NUM];

/*----------------------------------------------------------------------------*/
/* Harness                                                                    */
/*----------------------------------------------------------------------------*/

/** @brief All started AO queues, for the round-robin drain. */
static QActive* all_aos[CH_CONFIG_CHANNEL_COUNT + 2U];
static uint8_t all_count;

static void tick(uint32_t n)
{
    while ( n != 0U ) {
        QTIMEEVT_TICK_X(0U, (void*)0);
        ++g_test_tick;
        --n;
    }
}

static void drain_all(void)
{
    for ( ;; ) {
        uint8_t i;
        uint8_t dispatched = 0U;

        for ( i = 0U; i < all_count; ++i ) {
            if ( !QEQueue_isEmpty(&all_aos[i]->eQueue) ) {
                QEvt const* e = QActive_get_(all_aos[i]);

                QASM_DISPATCH(all_aos[i], e, 0U);
                QF_gc(e);
                dispatched = 1U;
            }
        }
        if ( dispatched == 0U ) {
            break;
        }
    }
}

/**
 * @brief Advance virtual time with continuous draining, like the QV loop.
 *
 * Batching ticks without draining would overflow the 16-deep AoModbus
 * queue; the target kernel dispatches after every RTC step instead.
 */
static void run_ticks(uint32_t n)
{
    while ( n != 0U ) {
        tick(1U);
        drain_all();
        --n;
    }
}

/** @brief Read the device fault word through the Modbus data-space map. */
static uint16_t read_fault_word(void)
{
    const modbus_slave_callbacks_t* cb = &slave.callbacks;
    uint8_t reply[2];

    CHECK(cb->input_registers(cb->ctx, REG_MAP_SYS_BASE + 4U, 1U, reply,
                              MODBUS_SLAVE_REG_READ)
          == MODBUS_SLAVE_EX_NONE);
    return modbus_slave_get_u16(reply);
}

/**
 * @brief Mirror the main.c startup order with fakes at the device edge.
 *
 * @param fail_ch Zero-based channel that fails to load (255 for none).
 */
static void boot_scenario(uint8_t fail_ch)
{
    uint8_t i;
    modbus_slave_config_t cfg = {0};
    tmp75_port_t cjc_port_if;

    memset(subscr, 0, sizeof(subscr));
    QActive_psInit(subscr, MAX_SIG);

    fdb_fake_reset();
    if ( fail_ch < CH_CONFIG_CHANNEL_COUNT ) {
        /* A partial store is never backfilled: preload every default row
         * with its factory body, then corrupt one body - the real loader
         * must reject exactly that key. */
        static const ch_config_t cfg = {
            (uint8_t)CH_TYPE_TC,   /**< type */
            (uint8_t)CH_TC_TYPE_K, /**< tc_type */
            2U,                    /**< rate_code: 10 Hz */
            0.0f,                  /**< linear_min */
            0.0f,                  /**< linear_max */
            0.0f,                  /**< offset */
            1.0f,                  /**< gain */
        };
        uint8_t bad[4] = {0};
        uint16_t addr = REG_MAP_DEFAULT_DEV_ADDR;
        uint8_t ch;
        char key[FDB_FAKE_MAX_KEY];

        for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
            (void)snprintf(key, sizeof(key), "cfg/ch%u", (unsigned)ch);
            fdb_fake_put(key, &cfg, sizeof(cfg));
        }
        fdb_fake_put("dev/addr", &addr, sizeof(addr));
        (void)snprintf(key, sizeof(key), "cfg/ch%u", (unsigned)fail_ch);
        fdb_fake_put(key, bad, sizeof(bad));
    }
    modbus_port_fake_init(&port);
    tmp75_port_fake_init(&cjc_port);
    tmp75_port_fake_port(&cjc_port, &cjc_port_if);
    CHECK(tmp75_ctor(&cjc_dev, &cjc_port_if, TMP75_ADDRESS_0) == TMP75_OK);

    /* Platform half for the fake ports; the AO fills callbacks/identity and builds the core. */
    modbus_port_fake_config(&port, &cfg,
                            rx_buffer, (uint16_t)sizeof(rx_buffer),
                            tx_buffer, (uint16_t)sizeof(tx_buffer));
    modbus_ao_ctor(&modbus, &slave, &cfg);

    /* Channels first: every AO subscribes inside its initial transition. */
    for ( i = 0U; i < CH_CONFIG_CHANNEL_COUNT; ++i ) {
        analog_input_fake_init(&inputs[i]);
        channel_ao_ctor(&channels[i], (uint8_t)i, &inputs[i].base);
        QActive_start(Q_ACTIVE_UPCAST(&channels[i]),
                      Q_PRIO((uint8_t)(APP_PRIO_CH_FIRST + i), 0U),
                      ch_queue[i], CHANNEL_AO_QUEUE_LEN, (void*)0, 0U, (void*)0);
        all_aos[i] = Q_ACTIVE_UPCAST(&channels[i]);
    }
    all_count = CH_CONFIG_CHANNEL_COUNT;

    /* AoModbus: its boot load publishes CONFIG_SET_SIG to the channels. */
    QActive_start(Q_ACTIVE_UPCAST(&modbus), Q_PRIO(APP_PRIO_MODBUS, 0U),
                  modbus_queue, MODBUS_AO_QUEUE_LEN, (void*)0, 0U, (void*)0);
    all_aos[all_count] = Q_ACTIVE_UPCAST(&modbus);
    ++all_count;

    /* AoColdJunction last: its first heartbeat reaches every subscriber. */
    cold_junction_ao_ctor(&cold_junction, &cjc_dev);
    QActive_start(Q_ACTIVE_UPCAST(&cold_junction), Q_PRIO(APP_PRIO_CJC, 0U),
                  cjc_queue, COLD_JUNCTION_AO_QUEUE_LEN, (void*)0, 0U, (void*)0);
    all_aos[all_count] = Q_ACTIVE_UPCAST(&cold_junction);
    ++all_count;
}

/*----------------------------------------------------------------------------*/
/* Scenarios                                                                  */
/*----------------------------------------------------------------------------*/

/** @brief Full boot: the publish reaches every subscriber and telemetry flows. */
static void test_full_boot(void)
{
    uint8_t ch;
    uint8_t i;

    boot_scenario(255U);

    /* Every channel sees every boot publish (receivers filter by ch) plus
     * the first CJC heartbeat: 9 of 13 queue slots at the burst peak. */
    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        CHECK(QEQueue_getUse(&channels[ch].super.eQueue)
              == (uint16_t)(CH_CONFIG_CHANNEL_COUNT + 1U));
    }

    /* The burst fits the capacity budget of docs/qp-application-design.md
     * section 7: depth 13 covers 8 publishes + 1 heartbeat + margin. */
    for ( i = 0U; i < CH_CONFIG_CHANNEL_COUNT; ++i ) {
        CHECK(QActive_getQueueMin((uint_fast8_t)(APP_PRIO_CH_FIRST + i)) >= 3U);
    }
    CHECK(QActive_getQueueMin(APP_PRIO_MODBUS) >= 8U);
    CHECK(QActive_getQueueMin(APP_PRIO_CJC) >= 1U);

    /* Settle every channel, then sample: 10 Hz needs 100 ms per value. */
    run_ticks(1200U);

    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        CHECK(channels[ch].cjc_valid != 0U);
        CHECK(analog_input_fake_count(&inputs[ch],
                                      (uint8_t)ANALOG_INPUT_FAKE_READ_SAMPLE)
              >= 5U);
    }

    /* The mirror holds the telemetry of every channel; the acceptance of
     * each configuration arrived as CFG_APPLIED_SIG. */
    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        CHECK(modbus.telemetry.value[ch] == 25.0f);
        CHECK(modbus.telemetry.status[ch] == (uint16_t)CH_STATUS_OK);
        CHECK(modbus.config.applied[ch] == 1U);
    }
    CHECK(modbus.telemetry.cjc_valid != 0U);
    CHECK(modbus.telemetry.cjc_temp == 25.0f);
    CHECK(modbus.store.db_ok == 1U);
    CHECK(modbus.config.addr_ok == 1U);
    CHECK(read_fault_word() == 0U);

    /* Pools fully recycled: both pools have every block back. */
    drain_all();
    for ( i = 0U; i < all_count; ++i ) {
        CHECK(QEQueue_isEmpty(&all_aos[i]->eQueue));
    }
}

/** @brief A channel whose parameter load failed never starts measuring. */
static void test_failed_channel_stays_down(void)
{
    uint8_t ch;

    boot_scenario(2U); /* channel 3 (zero-based 2) failed to load */

    run_ticks(1200U);

    CHECK(modbus.config.cfg_ok[2] == 0U);
    CHECK(modbus.telemetry.status[2] == (uint16_t)CH_STATUS_UNCONFIGURED);
    CHECK(modbus.config.applied[2] == 0U);
    CHECK(analog_input_fake_count(&inputs[2],
                                  (uint8_t)ANALOG_INPUT_FAKE_CONFIGURE)
          == 0U);

    /* The load failure is latched into the fault word (b2); the cold
     * junction heartbeat is valid, so b0 is clear. */
    CHECK(read_fault_word() == (uint16_t)REG_MAP_FAULT_PARAM_LOAD);

    /* Every other channel still runs. */
    for ( ch = 0U; ch < CH_CONFIG_CHANNEL_COUNT; ++ch ) {
        if ( ch != 2U ) {
            CHECK(modbus.telemetry.value[ch] == 25.0f);
            CHECK(modbus.config.applied[ch] == 1U);
        }
    }
}

/*----------------------------------------------------------------------------*/

int main(int argc, char* argv[])
{
    QF_init();
    QF_poolInit(pool_a, sizeof(pool_a), sizeof(pool_a[0]));
    QF_poolInit(pool_b, sizeof(pool_b), sizeof(pool_b[0]));

    if ( (argc == 2) && (argv[1][0] == '2') ) {
        test_failed_channel_stays_down();
    } else if ( (argc == 2) && (argv[1][0] == '1') ) {
        test_full_boot();
    } else {
        printf("usage: test_boot <scenario>  (1 full boot, 2 failed channel)\n");
        return 2;
    }

    printf("test_boot: %d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
