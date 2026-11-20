#ifndef FDB_FAKE_H_
#define FDB_FAKE_H_

/**
 * @file fdb_fake.h
 * @brief modbus_ao.c 所用四个 FlashDB 入口点的内存假件。
 *
 * 宿主测试链接本文件而不是 FlashDB 引擎，同时针对真实厂商头文件编译：
 * fdb_kvdb_init、fdb_kv_get_blob、fdb_kv_set_blob 与 fdb_blob_make 都在链接期由
 * 这里提供。故障开关可模拟打开失败、写入失败以及回读副本与存储字节不一致。如果
 * modbus_ao.c 将来触及第五种厂商行为，假件必须为它增加场景；test_fake_medium()
 * 固定了这份约定。
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 假件介质的容量。
 *
 * 按 8 个板级位置加器件行再加余量确定大小；溢出的插入会中止测试运行，而不是
 * 丢弃该行。
 */
#define FDB_FAKE_MAX_ENTRIES 12U

/** @brief 键缓冲区大小（键都很短：cfg/chN、dev/addr）。 */
#define FDB_FAKE_MAX_KEY 16U

/**
 * @brief 值缓冲区大小。
 *
 * 32 大于 20 字节的 cfg 正文和 24 字节的超长夹具；更长的正文会中止测试运行，
 * 而不是被静默截断。
 */
#define FDB_FAKE_MAX_VALUE 32U

/** @brief 假件介质的测试开关与观测量。 */
typedef struct {
    uint8_t fail_init;                      /**< 脚本：init 失败。 */
    uint8_t fail_write;                     /**< 脚本：set 失败且不存储任何内容。 */
    uint8_t corrupt_read;                   /**< 脚本：get 返回被改动的副本。 */
    uint16_t init_calls;                    /**< init 调用次数。 */
    uint16_t set_calls;                     /**< set 调用次数（填充不算 set）。 */
    char last_key[FDB_FAKE_MAX_KEY];        /**< 最后一次 set 的键。 */
    uint8_t last_value[FDB_FAKE_MAX_VALUE]; /**< 最后一次 set 的正文。 */
    size_t last_len;                        /**< 最后一次 set 的长度。 */
} fdb_fake_t;

/** @brief 唯一的假件实例：开关进，观测出。 */
extern fdb_fake_t fdb_fake;

/**
 * @brief 丢弃整个介质和所有观测值。
 *
 * 幂等，因此一次调用也可当作场景重置。缺失的键通过干脆不预置它来表达（关闭
 * 数据库的状态也会重置）。
 */
void fdb_fake_reset(void);

/**
 * @brief 预置一个原始值，不触碰观测计数器。
 *
 * 用于在打开之前为已有存储编写脚本（介质一旦存有内容就绝不会被回填）。
 *
 * @param key 以 NUL 结尾的键。
 * @param value 要存储的正文。
 * @param len 正文长度（字节）；更长的正文会中止测试运行。
 */
void fdb_fake_put(const char* key, const void* value, size_t len);

#ifdef __cplusplus
}
#endif

#endif
