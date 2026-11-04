/* FAL flash device port for the AT32F421C8T7 on-chip flash.
 *
 * One 64 KB device, 1 KB erase blocks, 8-bit write granularity (one
 * flash_byte_program() per byte; 4-byte-aligned writes could switch to
 * flash_word_program() for speed if the parameter volume ever grows).
 *
 * All three operations bounds-check against the device size without allowing
 * offset + size to wrap, and erase additionally requires both offset and size
 * to be block-aligned (so it can never erase past the requested range).  A
 * failed program or erase returns -1 to FAL; every operation reports the byte
 * count on success.
 */

#include <string.h>

#include "fal.h"

#include "at32f421_flash.h"

/* Hook for a watchdog feed inside the long erase/program loops.  IWDG is not
 * enabled in this build; expand this when it is (see the plan's risk notes). */
#define WATCHDOG_FEED() ((void)0)

/* Offset and size fit the device; written so offset + size cannot wrap past
 * the check. */
static int in_range(long offset, size_t size)
{
    size_t const len = at32f421_onchip_flash.len;

    return (offset >= 0L)
           && ((size_t)offset <= len)
           && (size <= len - (size_t)offset);
}

static int init(void)
{
    /* The flash controller needs no clock or pin setup; only unlock/lock
     * brackets the programming below. */
    return 1;
}

static int read(long offset, uint8_t* buf, size_t size)
{
    uint32_t addr;

    if ( !in_range(offset, size) ) {
        return -1;
    }
    addr = at32f421_onchip_flash.addr + (uint32_t)offset;

    /* The on-chip flash is memory mapped, so reads are plain memory copies. */
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
    uint32_t addr;
    size_t const blk_size = at32f421_onchip_flash.blk_size;
    size_t erased;

    /* Both offset and size are block-aligned, so the loop below erases exactly
     * the requested range and never steps over the device end. */
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
