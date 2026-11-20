#include <string.h>

/* 先包含 fdb_cfg.h 再包含 fal.h：elog 的 log_* 先就位，随后被 FAL 静默覆盖
 * （fal_def.h 先 undef 再重新定义），与中间件各 TU 一致。 */
#include "fdb_cfg.h"

#include "fal.h"

#include "at32f421_flash.h"

/** @brief 长时间擦除/编程循环中的喂狗钩子。 */
#define WATCHDOG_FEED() ((void)0)

static int in_range(long offset, size_t size)
{
    /* 如此书写可保证 offset + size 不会回绕越过设备末尾。 */
    size_t const len = at32f421_onchip_flash.len;

    return (offset >= 0L)
           && ((size_t)offset <= len)
           && (size <= len - (size_t)offset);
}

static int init(void)
{
    /* 闪存控制器无需时钟或引脚配置。 */
    return 1;
}

static int read(long offset, uint8_t* buf, size_t size)
{
    uint32_t addr;

    if ( !in_range(offset, size) ) {
        return -1;
    }
    addr = at32f421_onchip_flash.addr + (uint32_t)offset;

    /* 片上闪存为内存映射访问。 */
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

    /* 字节编程必须与 KV 布局所假设的粒度一致。 */
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

    /* 块对齐可防止循环擦除到请求范围之外。 */
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
    .name     = "at32f421_onchip",
    .addr     = 0x08000000,
    .len      = 64 * 1024,
    .blk_size = 1024,
    .ops      = {init, read, write, erase},
    /* 单一来源：字节编程必须与 KV 布局保持一致。 */
    .write_gran = FDB_WRITE_GRAN};
