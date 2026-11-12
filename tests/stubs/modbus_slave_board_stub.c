#include "modbus_slave_board_stub.h"

#include <string.h>

#include "modbus_slave_board.h"

static modbus_slave_t stub_slave;
static bool stub_running;

const modbus_slave_callbacks_t* stub_modbus_callbacks(void)
{
    return &stub_slave.callbacks;
}

bool stub_modbus_running(void)
{
    return stub_running;
}

const modbus_slave_identity_t* stub_modbus_identity(void)
{
    return &stub_slave.identity;
}

modbus_slave_status_t modbus_slave_board_ctor(
    const modbus_slave_callbacks_t* callbacks,
    const modbus_slave_identity_t* identity)
{
    if ( callbacks == NULL || identity == NULL ) {
        return MODBUS_SLAVE_ERR_ARG;
    }

    memset(&stub_slave, 0, sizeof(stub_slave));
    stub_slave.callbacks = *callbacks;
    stub_slave.identity  = *identity;
    stub_running         = false;
    return MODBUS_SLAVE_OK;
}

uint8_t modbus_slave_board_count(void)
{
    return 1U;
}

modbus_slave_t* modbus_slave_board_get(void)
{
    return &stub_slave;
}

modbus_slave_status_t modbus_slave_set_event_port(
    modbus_slave_t* me,
    const modbus_slave_port_event_t* event_port)
{
    (void)me;
    (void)event_port;
    return MODBUS_SLAVE_OK;
}

modbus_slave_status_t modbus_slave_start(modbus_slave_t* me)
{
    (void)me;
    stub_running = true;
    return MODBUS_SLAVE_OK;
}

modbus_slave_status_t modbus_slave_stop(modbus_slave_t* me)
{
    (void)me;
    stub_running = false;
    return MODBUS_SLAVE_OK;
}

void modbus_slave_set_running(modbus_slave_t* me, uint8_t is_running)
{
    if ( me != NULL ) {
        me->identity.is_running = (uint8_t)((is_running != 0U) ? 1U : 0U);
    }
}

modbus_slave_step_t modbus_slave_advance(modbus_slave_t* me)
{
    (void)me;
    return MODBUS_SLAVE_STEP_NONE;
}
