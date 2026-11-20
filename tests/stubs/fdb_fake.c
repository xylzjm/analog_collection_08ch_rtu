/**
 * @file fdb_fake.c
 * @brief modbus_ao.c 所用四个 FlashDB 入口点的内存假件。
 *
 * 这些行为镜像厂商引擎（fdb_blob_make 不改动 saved，get 报告完整存储长度并
 * 复制 min(len, size)，set 先计数再遵从 fail_write，init 只向空介质填充默认
 * 表并把零长度行读作 C 字符串，关闭的数据库按厂商的 db_init_ok 守卫应答，
 * NULL 正文删除键）；test_fake_medium() 固定这些约定。未建模：真实的 set_kv
 * 在 GC 压力下可能返回 FDB_SAVED_FULL；假件除非 fail_write 否则总是成功
 *（总线行为不受影响，因为 param_commit 依据回读决定）。
 */

#include "fdb_fake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flashdb.h"

/** @brief 一对已存储的键值；在此不透明，是介质中的一个条目。 */
typedef struct {
    char key[FDB_FAKE_MAX_KEY];        /**< 以 NUL 结尾的键。 */
    uint8_t value[FDB_FAKE_MAX_VALUE]; /**< 存储的正文。 */
    size_t len;                        /**< 存储长度（字节）。 */
    uint8_t used;                      /**< 条目被占用时非零。 */
} medium_entry_t;

/** @brief 唯一的介质；只能通过假件 API 访问。 */
static medium_entry_t entries[FDB_FAKE_MAX_ENTRIES];

/** @brief 打开标志：init 成功时置位，reset 与 fail_init 时清零。 */
static uint8_t open_ok;

fdb_fake_t fdb_fake;

/** @brief 中止测试运行；假件绝不静默降级。 */
static void fake_die(const char* msg)
{
    fprintf(stderr, "fdb_fake: %s\n", msg);
    abort();
}

/** @brief 把键复制进定长缓冲区，截断且不溢出。 */
static void copy_key(char* dst,
                     size_t cap,
                     const char* src)
{
    size_t i = 0U;

    while ( (i + 1U < cap) && (src[i] != '\0') ) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

/** @brief 按键查找一个条目；不存在时返回 NULL。 */
static medium_entry_t* find_entry(const char* key)
{
    uint8_t i;

    for ( i = 0U; i < FDB_FAKE_MAX_ENTRIES; ++i ) {
        if ( (entries[i].used != 0U)
             && (strcmp(entries[i].key, key) == 0) ) {
            return &entries[i];
        }
    }
    return NULL;
}

/** @brief 插入或覆盖一个条目，不触碰计数器。 */
static void put_entry(const char* key,
                      const void* value,
                      size_t len)
{
    medium_entry_t* entry = find_entry(key);
    uint8_t i;

    if ( entry == NULL ) {
        for ( i = 0U; i < FDB_FAKE_MAX_ENTRIES; ++i ) {
            if ( entries[i].used == 0U ) {
                entry = &entries[i];
                break;
            }
        }
    }
    if ( entry == NULL ) {
        fake_die("medium full: raise FDB_FAKE_MAX_ENTRIES");
    }
    if ( len > sizeof(entry->value) ) {
        fake_die("body longer than FDB_FAKE_MAX_VALUE");
    }

    copy_key(entry->key, sizeof(entry->key), key);
    memset(entry->value, 0, sizeof(entry->value));
    if ( len != 0U ) {
        memcpy(entry->value, value, len);
    }
    entry->len  = len;
    entry->used = 1U;
}

/** @brief 按键删除一个条目；键不存在时为空操作。 */
static void drop_entry(const char* key)
{
    medium_entry_t* entry = find_entry(key);

    if ( entry != NULL ) {
        memset(entry, 0, sizeof(*entry));
    }
}

/** @brief 报告介质是否不含任何条目。 */
static int store_empty(void)
{
    uint8_t i;

    for ( i = 0U; i < FDB_FAKE_MAX_ENTRIES; ++i ) {
        if ( entries[i].used != 0U ) {
            return 0;
        }
    }
    return 1;
}

fdb_blob_t fdb_blob_make(fdb_blob_t blob, const void* value_buf, size_t buf_len)
{
    /* 规则：只设置 buf/size；saved 块保持调用者留下的样子。 */
    blob->buf  = (void*)value_buf;
    blob->size = buf_len;
    return blob;
}

size_t fdb_kv_get_blob(fdb_kvdb_t db, const char* key, fdb_blob_t blob)
{
    medium_entry_t* entry;
    size_t n;

    (void)db;

    /* 规则：关闭的数据库返回 0，且不改动 saved.len
     *（fdb_kvdb.c fdb_kv_get_blob）。 */
    if ( open_ok == 0U ) {
        return 0U;
    }

    entry = find_entry(key);
    if ( entry == NULL ) {
        blob->saved.len = 0U;
        return 0U;
    }

    /* 规则：即使复制被截断，saved.len 也是完整存储长度。 */
    blob->saved.len = entry->len;
    n               = (entry->len < blob->size) ? entry->len : blob->size;
    if ( (n != 0U) && (blob->buf != NULL) ) {
        memcpy(blob->buf, entry->value, n);
        if ( fdb_fake.corrupt_read != 0U ) {
            ((uint8_t*)blob->buf)[0] ^= 0x5AU;
        }
    }
    return n;
}

fdb_err_t fdb_kv_set_blob(fdb_kvdb_t db, const char* key, fdb_blob_t blob)
{
    size_t len = blob->size;

    (void)db;

    /* 规则：关闭的数据库在记录任何尝试之前就拒绝该调用
     *（fdb_kvdb.c fdb_kv_set_blob）。 */
    if ( open_ok == 0U ) {
        return FDB_INIT_FAILED;
    }

    /* 规则：set_kv 忽略 NULL 正文的长度并删除该键
     *（fdb_kvdb.c set_kv）；超长正文显式失败，绝不截断。 */
    if ( blob->buf == NULL ) {
        len = 0U;
    } else if ( len > FDB_FAKE_MAX_VALUE ) {
        fake_die("body longer than FDB_FAKE_MAX_VALUE");
    }

    /* 规则：尝试在 fail_write 拒绝之前就被计数并记录——
     * 由调用方依据回读决定，而不是依据返回码。 */
    ++fdb_fake.set_calls;
    copy_key(fdb_fake.last_key, sizeof(fdb_fake.last_key), key);
    memset(fdb_fake.last_value, 0, sizeof(fdb_fake.last_value));
    if ( len != 0U ) {
        memcpy(fdb_fake.last_value, blob->buf, len);
    }
    fdb_fake.last_len = len;

    if ( fdb_fake.fail_write != 0U ) {
        return FDB_WRITE_ERR;
    }
    if ( blob->buf == NULL ) {
        drop_entry(key);
        return FDB_NO_ERR;
    }
    put_entry(key, blob->buf, len);
    return FDB_NO_ERR;
}

fdb_err_t fdb_kvdb_init(fdb_kvdb_t db, const char* name, const char* path,
                        struct fdb_default_kv* default_kv, void* user_data)
{
    size_t i;
    size_t len;

    (void)db;
    (void)name;
    (void)path;
    (void)user_data;

    ++fdb_fake.init_calls;
    if ( fdb_fake.fail_init != 0U ) {
        open_ok = 0U;
        return FDB_INIT_FAILED;
    }
    open_ok = 1U;

    /* 规则：出厂启动填充——整张默认表只写入空介质，绝不回填
     * 或覆盖；零长度行读作 C 字符串（fdb_kvdb.c fdb_kv_set_default）；
     * 填充不算一次 set。 */
    if ( (default_kv != NULL) && (default_kv->kvs != NULL) && store_empty() ) {
        for ( i = 0U; i < default_kv->num; ++i ) {
            len = default_kv->kvs[i].value_len;
            if ( len == 0U ) {
                len = strlen((const char*)default_kv->kvs[i].value);
            }
            put_entry(default_kv->kvs[i].key, default_kv->kvs[i].value, len);
        }
    }
    return FDB_NO_ERR;
}

void fdb_fake_reset(void)
{
    memset(&fdb_fake, 0, sizeof(fdb_fake));
    memset(entries, 0, sizeof(entries));
    open_ok = 0U;
}

void fdb_fake_put(const char* key, const void* value, size_t len)
{
    put_entry(key, value, len);
}
