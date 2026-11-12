#ifndef SIG24131_BOARD_H
#define SIG24131_BOARD_H

#include <stdint.h>

#include "at32f421_wk_config.h"
#include "sig24131.h"

#define SIG24131_BOARD_INSTANCE_COUNT 8U

sig24131_t* sig24131_board_get_device(uint8_t channel);

#endif
