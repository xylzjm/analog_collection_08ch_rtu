#include <string.h>

#include "fal.h"

#include "at32f421_flash.h"

/** @brief Watchdog feed hook for long erase/program loops. */
#define WATCHDOG_FEED() ((void)0)

static int in_range(long offset, size_t size)
{
    /* Written so offset + size cannot wrap past the device end. */
    size_t const len = at32f421_onchip_flash.len;

    return (offset >= 0L)
           && ((size_t)offset <= len)
           && (size <= len - (size_t)offset);
}

static int init(void)
{
    /* The flash controller needs no clock or pin setup. */
    return 1;
}

static int read(long offset, uint8_t* buf, size_t size)
{
    uint32_t addr;

    if ( !in_range(offset, size) ) {
        return -1;
    }
    addr = at32f421_onchip_flash.addr + (uint32_t)offset;

    /* On-chip flash is memory mapped. */
    memcpy(buf, (const void*)addr, size);
    return (int)size;
}

static int write(long offset, const uint8_t* buf, size_t size)
{
    uint32_t addr;
    size_t i;

    if ( !in_range(offset, size) ) {
        return -1;
    }
    addr = at32f421_onchip_flash.addr + (uint32_t)offset;

    /* Byte programming must stay in step with FDB_WRITE_GRAN/write_gran. */
    flash_unlock();
    for ( i = 0; i < size; ++i ) {
        if ( flash_byte_program(addr + (uint32_t)i, buf[i])
             != FLASH_OPERATE_DONE ) {
            flash_lock();
            return -1;
        }
        WATCHDOG_FEED();
    }

    if ( memcmp((const void*)addr, buf, size) != 0 ) {
        flash_lock();
        return -1;
    }
    flash_lock();
    return (int)size;
}

static int erase(long offset, size_t size)
{
    uint32_t addr;
    size_t const blk_size = at32f421_onchip_flash.blk_size;
    size_t erased;

    /* Block alignment keeps the loop from erasing outside the request. */
    if ( !in_range(offset, size)
         || (((size_t)offset % blk_size) != 0U)
         || ((size % blk_size) != 0U) ) {
        return -1;
    }
    addr = at32f421_onchip_flash.addr + (uint32_t)offset;

    flash_unlock();
    for ( erased = 0; erased < size; erased += blk_size ) {
        if ( flash_sector_erase(addr + (uint32_t)erased)
             != FLASH_OPERATE_DONE ) {
            flash_lock();
            return -1;
        }
        WATCHDOG_FEED();
    }
    flash_lock();
    return (int)size;
}

const struct fal_flash_dev at32f421_onchip_flash = {
    .name       = "at32f421_onchip",
    .addr       = 0x08000000,
    .len        = 64 * 1024,
    .blk_size   = 1024,
    .ops        = {init, read, write, erase},
    .write_gran = 8
};
