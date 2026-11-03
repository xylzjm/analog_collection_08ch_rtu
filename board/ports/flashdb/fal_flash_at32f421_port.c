/* FAL flash device port for the AT32F421C8T7 on-chip flash.
 *
 * One 64 KB device, 1 KB erase blocks, 8-bit write granularity (one
 * flash_byte_program() per byte; 4-byte-aligned writes could switch to
 * flash_word_program() for speed if the parameter volume ever grows).
 *
 * All three operations bounds-check against the device size, and erase
 * additionally requires a block-aligned offset.  A failed program or erase
 * returns -1 to FAL; every operation reports the byte count on success.
 */

#include <string.h>

#include "fal.h"

#include "at32f421_flash.h"

/* Hook for a watchdog feed inside the long erase/program loops.  IWDG is not
 * enabled in this build; expand this when it is (see the plan's risk notes). */
#define WATCHDOG_FEED() ((void)0)

static int init(void)
{
    /* The flash controller needs no clock or pin setup; only unlock/lock
     * brackets the programming below. */
    return 1;
}

static int read(long offset, uint8_t* buf, size_t size)
{
    uint32_t const addr = at32f421_onchip_flash.addr + (uint32_t)offset;

    if ( (offset < 0L)
         || ((uint32_t)offset + size > at32f421_onchip_flash.len) ) {
        return -1;
    }

    /* The on-chip flash is memory mapped, so reads are plain memory copies. */
    memcpy(buf, (const void*)addr, size);
    return (int)size;
}

static int write(long offset, const uint8_t* buf, size_t size)
{
    uint32_t const addr = at32f421_onchip_flash.addr + (uint32_t)offset;
    size_t i;

    if ( (offset < 0L)
         || ((uint32_t)offset + size > at32f421_onchip_flash.len) ) {
        return -1;
    }

    flash_unlock();
    for ( i = 0; i < size; ++i ) {
        if ( flash_byte_program(addr + (uint32_t)i, buf[i])
             != FLASH_OPERATE_DONE ) {
            flash_lock();
            return -1;
        }
        WATCHDOG_FEED();
    }

    /* Read back and compare before reporting success. */
    if ( memcmp((const void*)addr, buf, size) != 0 ) {
        flash_lock();
        return -1;
    }
    flash_lock();
    return (int)size;
}

static int erase(long offset, size_t size)
{
    uint32_t const addr = at32f421_onchip_flash.addr + (uint32_t)offset;
    size_t const blk_size = at32f421_onchip_flash.blk_size;
    size_t erased;

    if ( (offset < 0L)
         || ((uint32_t)offset + size > at32f421_onchip_flash.len)
         || (((uint32_t)offset % blk_size) != 0U) ) {
        return -1;
    }

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
