/**
 * @file modbus_ao.c
 * @brief Modbus RTU 从站活动对象：镜像、写事务、存储。
 *
 * 本 AO 拥有 Modbus 数据空间的类型化镜像、保持寄存器写事务和参数持久化；
 * modbus 核心在本 AO 的执行上下文中直接调用数据空间回调。reg_map 是它
 * 调用的纯线上映射。写事务遵循项目的原子性纪律——校验、带回读校验固化
 * 进 FlashDB 参数数据库、然后才更新镜像——固化成功的通道配置以
 * CONFIG_SET_SIG 发布。
 */

#include "modbus_ao.h"

#include <float.h>
#include <stdbool.h>
#include <string.h>

#include "elog.h"
#include "flashdb.h"
#include "reg_map.h"

Q_DEFINE_THIS_MODULE("modbus_ao")

/* 由 ISR 事件端口投递的静态事件（QEVT_INITIALIZER，ROM）。 */
static QEvt const frameEvt     = QEVT_INITIALIZER(FRAME_SIG);
static QEvt const txDoneEvt    = QEVT_INITIALIZER(TX_DONE_SIG);
static QEvt const txTimeoutEvt = QEVT_INITIALIZER(TX_TIMEOUT_SIG);

static void publish_cfg(modbus_ao_t* me, uint8_t ch, const ch_config_t* cfg);
static QState modbus_ao_initial(modbus_ao_t* const me, void const* const par);
static QState modbus_ao_serving(modbus_ao_t* const me, QEvt const* const e);

/*----------------------------------------------------------------------------*/
/* 故障字                                                                     */
/*----------------------------------------------------------------------------*/

/** @brief 合成设备故障字寄存器的值。 */
static uint16_t fault_word(const modbus_ao_t* me)
{
    uint16_t latched = 0U;
    uint8_t ch;

    if ( me->store.commit_failed != 0U ) {
        latched |= REG_MAP_FAULT_PARAM_WRITE;
    }
    /* b2 仅在存储可用时存在：存储失效时只报 b3。 */
    if ( me->store.db_ok != 0U ) {
        uint8_t load_failed = (me->config.addr_ok == 0U) ? 1U : 0U;

        for ( ch = 0U; ch < BOARD_CHANNEL_COUNT; ++ch ) {
            if ( me->config.cfg_ok[ch] == 0U ) {
                load_failed = 1U;
                break;
            }
        }
        if ( load_failed != 0U ) {
            latched |= REG_MAP_FAULT_PARAM_LOAD;
        }
    }
    if ( me->store.db_ok == 0U ) {
        latched |= REG_MAP_FAULT_FDB_INIT;
    }
    return reg_map_fault_word(me->telemetry.cjc_valid, latched);
}

/*----------------------------------------------------------------------------*/
/* 持久化                                                                     */
/*----------------------------------------------------------------------------*/

/** @brief 一次同步保存的结果。 */
typedef enum {
    COMMIT_OK        = 0, /**< 回读等于请求值；已固化。 */
    COMMIT_NO_CHANGE = 1, /**< 与快照同值；未触碰存储。 */
    COMMIT_ERR       = -1 /**< 回读不一致；视为未固化。 */
} commit_status_t;

/** @brief 出厂默认通道配置：TC K 型，10 Hz。 */
static const ch_config_t default_ch_cfg = {
    (uint8_t)CH_TYPE_TC,   /**< type */
    (uint8_t)CH_TC_TYPE_K, /**< tc_type */
    2U,                    /**< rate_code：10 Hz */
    0.0f,                  /**< linear_min（TC 不使用） */
    0.0f,                  /**< linear_max（TC 不使用） */
    0.0f,                  /**< offset */
    1.0f,                  /**< gain */
};

/** @brief 出厂默认 Modbus 从站地址。 */
static const uint16_t default_dev_addr = REG_MAP_DEFAULT_DEV_ADDR;

/** @brief FlashDB 数据库名（仅用于诊断）。 */
#define PARAM_DB_NAME "param"

/** @brief 唯一的参数数据库；归本模块所有，在构造函数中打开。 */
static struct fdb_kvdb param_db;

/**
 * @brief 默认参数表：键与默认内容的单一来源。
 *
 * 行类型为 fdb_default_kv_node，可被 fdb_kvdb_init() 直接使用。键集合
 * 始终是 8 位置的板级布局，即使实际装配通道更少，因此 SKU 变体保持相同
 * 的 KV 布局。最后一行是设备地址。
 *
 * 厂商类型迫使节点字段非 const；内容本身仍为 const（.rodata）。初始化器
 * 中的强制转换是与厂商边界上的唯一妥协。数组从不会被写入：
 * fdb_kvdb_init() 在数据库生命周期内保留 kvs 指针，这也是数组为 static
 * 的原因。
 */
static struct fdb_default_kv_node param_defaults[] = {
#define X(pos) {(char*)"cfg/ch" #pos, (void*)&default_ch_cfg, sizeof(default_ch_cfg)},
    BOARD_POSITIONS(X)
#undef X
        {(char*)"dev/addr", (void*)&default_dev_addr, sizeof(default_dev_addr)},
};

/* 警戒：删除 dev/addr 行会让 key_addr() 越界。派生值对派生值：表结构行数
 * 无需手工维护。 */
Q_ASSERT_STATIC((sizeof(param_defaults) / sizeof(param_defaults[0]))
                == BOARD_POSITION_COUNT + 1U);

/** @brief 单条通道配置行的 KV 键。 */
static const char* key_cfg(uint8_t ch)
{
    return param_defaults[ch].key;
}

/** @brief 设备地址行的 KV 键。 */
static const char* key_addr(void)
{
    return param_defaults[BOARD_POSITION_COUNT].key;
}

/**
 * @brief 从参数数据库读取一个键的内容。
 *
 * 负责 fdb_blob 生命周期与 saved.len 契约：FlashDB 即使发生截断也会报告
 * 完整存储长度，0 表示键缺失。blob.saved.len 预先清零，因为
 * fdb_blob_make() 不触碰 saved 块。此处从不用默认值替代。
 *
 * @param key 以 NUL 结尾的键。
 * @param buf 目标缓冲区。
 * @param cap @p buf 的容量。
 * @param stored_len 接收完整存储长度（键缺失时为 0）。
 * @return 键存在时返回 true，缺失时返回 false。
 */
static bool param_read(const char* key, void* buf, size_t cap, size_t* stored_len)
{
    struct fdb_blob blob;

    blob.saved.len = 0U;
    (void)fdb_kv_get_blob(&param_db, key, fdb_blob_make(&blob, buf, cap));
    *stored_len = blob.saved.len;
    return blob.saved.len != 0U;
}

/**
 * @brief 同步固化一个内容到参数数据库。
 *
 * 先写入，然后无条件回读；结果由回读比较决定，绝不由库返回码决定（后者
 * 只用于日志）。这里是“固化 = 写入后回读”的唯一实现处。
 *
 * @param key 以 NUL 结尾的键。
 * @param body 要存储的内容。
 * @param len 内容长度，单位字节。
 * @return 仅当存储字节与请求字节相等时返回 COMMIT_OK，否则返回
 *         COMMIT_ERR。
 */
static commit_status_t param_commit(const char* key, const void* body, size_t len)
{
    uint8_t verify[sizeof(ch_config_t)]; /* 最长的内容 */
    struct fdb_blob blob;
    fdb_err_t st;
    size_t stored_len;

    st = fdb_kv_set_blob(&param_db, key, fdb_blob_make(&blob, body, len));
    /* 供下面的日志行使用；宿主 elog 桩会把它们编译掉。只有回读决定结果。 */
    (void)st;

    /* 比校验缓冲区更长的内容永远无法确认相等。 */
    if ( !param_read(key, verify, sizeof(verify), &stored_len)
         || (stored_len != len)
         || (len > sizeof(verify))
         || (memcmp(verify, body, len) != 0) ) {
        elog_error("modbus_ao", "commit failed: %s (fdb %d, read back %u)",
                   key, (int)st, (unsigned)stored_len);
        return COMMIT_ERR;
    }
    elog_info("modbus_ao", "commit ok: %s (fdb %d)", key, (int)st);
    return COMMIT_OK;
}

/**
 * @brief 判断浮点数是否为有限值。
 *
 * @param x 待测值。
 * @return 有限值返回 true，NaN 与正负无穷返回 false。
 */
static bool f32_is_finite(float x)
{
    return (x == x) && (x >= -FLT_MAX) && (x <= FLT_MAX);
}

/**
 * @brief 校验一份配置并就地归一化。
 *
 * 校验与归一化是同一次操作，使语义相同的配置始终逐字节相等（save_cfg 中
 * 的变化检查依赖 memcmp）。契约记录在 ch_config_t 上。
 *
 * @param cfg 待校验的配置；仅在成功时写回。
 * @return 配置有效（且已归一化）时返回 true，必须拒绝时返回 false。
 *         返回 false 时入参保持不变。
 */
static bool config_validate(ch_config_t* cfg)
{
    ch_config_t norm;

    if ( cfg == NULL ) {
        return false;
    }
    norm = *cfg;

    if ( norm.type > (uint8_t)CH_TYPE_0_10V ) {
        return false;
    }

    if ( norm.type == (uint8_t)CH_TYPE_TC ) {
        if ( norm.tc_type > (uint8_t)CH_TC_TYPE_T ) {
            return false;
        }
    } else {
        norm.tc_type = CH_TC_TYPE_NONE;
    }

    if ( norm.rate_code > CH_CONFIG_RATE_CODE_MAX ) {
        return false;
    }

    if ( !f32_is_finite(norm.linear_min) || !f32_is_finite(norm.linear_max)
         || !f32_is_finite(norm.offset) || !f32_is_finite(norm.gain) ) {
        return false;
    }

    if ( norm.type != (uint8_t)CH_TYPE_4_20MA
         && norm.type != (uint8_t)CH_TYPE_0_10V ) {
        norm.linear_min = 0.0f;
        norm.linear_max = 0.0f;
    }

    *cfg = norm;
    return true;
}

/** @brief 判断从站地址是否合法（1..247）。 */
static bool addr_valid(uint16_t addr)
{
    return (addr >= MODBUS_SLAVE_ADDRESS_MIN) && (addr <= MODBUS_SLAVE_ADDRESS_MAX);
}

/**
 * @brief 读取一个配置键并校验其内容。
 *
 * @return 键存在、长度符合预期且校验通过时返回 true。
 */
static bool load_cfg(modbus_ao_t* me, uint8_t ch)
{
    const char* key = key_cfg(ch);
    ch_config_t cfg;
    size_t len;

    if ( !param_read(key, &cfg, sizeof(cfg), &len) ) {
        elog_warn("modbus_ao", "load missing: %s", key);
        return false;
    }
    if ( len != sizeof(cfg) ) {
        elog_warn("modbus_ao", "load size: %s (%u != %u)", key,
                  (unsigned)len, (unsigned)sizeof(cfg));
        return false;
    }
    if ( !config_validate(&cfg) ) {
        elog_warn("modbus_ao", "load invalid: %s", key);
        return false;
    }
    me->config.cfg[ch]    = cfg;
    me->config.cfg_ok[ch] = 1U;
    return true;
}

/**
 * @brief 读取从站地址键并检查其范围。
 *
 * @param addr 接收存储的地址。
 * @return 键存在、长度符合预期且为 1..247 时返回 true。
 */
static bool load_addr(modbus_ao_t* me, uint16_t* addr)
{
    const char* key = key_addr();
    size_t len;

    if ( !param_read(key, addr, sizeof(*addr), &len) ) {
        elog_warn("modbus_ao", "load missing: %s", key);
        return false;
    }
    if ( len != sizeof(*addr) ) {
        elog_warn("modbus_ao", "load size: %s (%u != %u)", key,
                  (unsigned)len, (unsigned)sizeof(*addr));
        return false;
    }
    if ( !addr_valid(*addr) ) {
        elog_warn("modbus_ao", "load range: %s (%u)", key, (unsigned)*addr);
        return false;
    }
    return true;
}

/**
 * @brief 把所有键加载进镜像与变化检查基准。
 *
 * 缺失、损坏或语义非法的键都算加载失败，绝不以默认值替代：受影响的通道
 * 保持未配置。完全空的存储即出厂首次启动：默认表在构造时已落地，加载
 * 成功。
 */
static void load_all(modbus_ao_t* me)
{
    uint8_t ch;
    uint16_t addr;

    if ( me->store.db_ok == 0U ) {
        return;
    }

    for ( ch = 0U; ch < BOARD_CHANNEL_COUNT; ++ch ) {
        me->config.cfg_ok[ch] = 0U;
        (void)load_cfg(me, ch);
    }

    me->config.addr_ok = 0U;
    if ( load_addr(me, &addr) ) {
        me->config.addr    = addr;
        me->config.addr_ok = 1U;
    } else {
        me->config.addr = REG_MAP_DEFAULT_DEV_ADDR;
    }
}

/**
 * @brief 同步持久化一个通道配置。
 *
 * 先对镜像做变化检查；值有变化才走 param_commit()（写入 + 回读），由其
 * 决定结果。
 */
static commit_status_t save_cfg(modbus_ao_t* me,
                                uint8_t ch,
                                const ch_config_t* cfg)
{
    if ( (ch >= BOARD_CHANNEL_COUNT) || (me->store.db_ok == 0U) ) {
        return COMMIT_ERR;
    }

    if ( (me->config.cfg_ok[ch] != 0U)
         && (memcmp(&me->config.cfg[ch], cfg, sizeof(*cfg)) == 0) ) {
        return COMMIT_NO_CHANGE;
    }

    if ( param_commit(key_cfg(ch), cfg, sizeof(*cfg)) != COMMIT_OK ) {
        return COMMIT_ERR;
    }

    me->config.cfg[ch]    = *cfg;
    me->config.cfg_ok[ch] = 1U;
    return COMMIT_OK;
}

/** @brief 同步持久化从站地址（同一纪律）。 */
static commit_status_t save_addr(modbus_ao_t* me, uint16_t addr)
{
    if ( (me->store.db_ok == 0U) || !addr_valid(addr) ) {
        return COMMIT_ERR;
    }

    if ( (me->config.addr_ok != 0U) && (me->config.addr == addr) ) {
        return COMMIT_NO_CHANGE;
    }

    if ( param_commit(key_addr(), &addr, sizeof(addr)) != COMMIT_OK ) {
        return COMMIT_ERR;
    }

    me->config.addr    = addr;
    me->config.addr_ok = 1U;
    return COMMIT_OK;
}

/*----------------------------------------------------------------------------*/
/* 数据空间回调                                                               */
/*----------------------------------------------------------------------------*/

static modbus_slave_exception_t coils(void* ctx,
                                      uint16_t start,
                                      uint16_t count,
                                      uint8_t* buffer,
                                      modbus_slave_register_mode_t mode)
{
    (void)ctx;
    (void)buffer;
    (void)mode;

    /* 本产品无线圈：该数据空间本身未映射。 */
    return reg_map_check(REG_SPACE_COIL, start, count, NULL);
}

static modbus_slave_exception_t discrete(void* ctx,
                                         uint16_t start,
                                         uint16_t count,
                                         uint8_t* buffer,
                                         modbus_slave_register_mode_t mode)
{
    (void)ctx;
    (void)buffer;
    (void)mode;

    return reg_map_check(REG_SPACE_DISCRETE, start, count, NULL);
}

static modbus_slave_exception_t input(void* ctx,
                                      uint16_t start,
                                      uint16_t count,
                                      uint8_t* buffer,
                                      modbus_slave_register_mode_t mode)
{
    modbus_ao_t* me        = (modbus_ao_t*)ctx;
    const reg_span_t* span = NULL;
    modbus_slave_exception_t ex;

    ex = reg_map_check(REG_SPACE_INPUT, start, count, &span);
    if ( ex != MODBUS_SLAVE_EX_NONE ) {
        return ex;
    }
    if ( mode != MODBUS_SLAVE_REG_READ ) {
        return MODBUS_SLAVE_EX_ILLEGAL_FUNCTION;
    }

    reg_map_input_encode(me->telemetry.value, me->telemetry.status,
                         me->telemetry.cjc_temp, fault_word(me),
                         span, start, count, buffer);
    return MODBUS_SLAVE_EX_NONE;
}

/**
 * @brief 保持寄存器写入流水线：校验、固化、然后更新。
 *
 * 配置写入以帧为单位原子：固化成功且回读一致之前，既不更新镜像也不
 * 上报。
 */
static modbus_slave_exception_t holding_write(modbus_ao_t* me,
                                              uint16_t start,
                                              const reg_span_t* span,
                                              uint16_t count,
                                              const uint8_t* buffer)
{
    commit_status_t st;

    if ( span->kind == REG_SPAN_DEV ) {
        uint16_t addr = reg_map_dev_decode(buffer);

        if ( !addr_valid(addr) ) {
            return MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE;
        }

        st = save_addr(me, addr);
        if ( st == COMMIT_ERR ) {
            me->store.commit_failed = 1U;
            return MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE;
        }
        if ( st == COMMIT_OK ) {
            me->store.commit_failed = 0U;
        }
        /* COMMIT_NO_CHANGE：镜像已持有请求的值。 */
        return MODBUS_SLAVE_EX_NONE;
    }

    {
        uint8_t ch = span->channel;
        uint8_t block[REG_MAP_CH_CFG_LEN * 2U];
        ch_config_t cand;

        /* 读-改-写：任何单寄存器写入都能正确落位。 */
        reg_map_ch_encode(&me->config.cfg[ch], block);
        memcpy(&block[2U * (uint16_t)(start - span->base)], buffer, 2U * count);
        reg_map_ch_decode(block, &cand);
        if ( !config_validate(&cand) ) {
            return MODBUS_SLAVE_EX_ILLEGAL_DATA_VALUE;
        }

        st = save_cfg(me, ch, &cand);
        if ( st == COMMIT_ERR ) {
            me->store.commit_failed = 1U;
            return MODBUS_SLAVE_EX_SLAVE_DEVICE_FAILURE;
        }
        if ( st == COMMIT_OK ) {
            me->store.commit_failed = 0U;
            me->config.applied[ch]  = 0U;
            publish_cfg(me, ch, &cand);
        } else if ( me->config.applied[ch] == 0U ) {
            /* 已持久化，但通道尚未接受：同值重写即重试触发。 */
            publish_cfg(me, ch, &cand);
        }
        return MODBUS_SLAVE_EX_NONE;
    }
}

static modbus_slave_exception_t holding(void* ctx,
                                        uint16_t start,
                                        uint16_t count,
                                        uint8_t* buffer,
                                        modbus_slave_register_mode_t mode)
{
    modbus_ao_t* me        = (modbus_ao_t*)ctx;
    const reg_span_t* span = NULL;
    modbus_slave_exception_t ex;

    ex = reg_map_check(REG_SPACE_HOLDING, start, count, &span);
    if ( ex != MODBUS_SLAVE_EX_NONE ) {
        return ex;
    }

    if ( mode == MODBUS_SLAVE_REG_WRITE ) {
        return holding_write(me, start, span, count, buffer);
    }

    {
        uint8_t block[REG_MAP_CH_CFG_LEN * 2U];

        if ( span->kind == REG_SPAN_CH_CFG ) {
            reg_map_ch_encode(&me->config.cfg[span->channel], block);
        } else {
            reg_map_dev_encode(me->config.addr, block);
        }
        memcpy(buffer, &block[2U * (uint16_t)(start - span->base)], 2U * count);
    }
    return MODBUS_SLAVE_EX_NONE;
}

static uint8_t address(void* ctx)
{
    modbus_ao_t* me = (modbus_ao_t*)ctx;

    return (uint8_t)me->config.addr;
}

/*----------------------------------------------------------------------------*/
/* 配置发布                                                                   */
/*----------------------------------------------------------------------------*/

/**
 * @brief 为一个通道发布一条 CONFIG_SET_SIG 事件。
 *
 * 启动加载与保持寄存器写流水线都使用它。
 */
static void publish_cfg(modbus_ao_t* me, uint8_t ch, const ch_config_t* cfg)
{
    config_set_evt_t* evt = Q_NEW(config_set_evt_t, CONFIG_SET_SIG);

    (void)me; /* 发布的发送者；仅在 Q_SPY 构建中被引用 */

    evt->ch  = ch;
    evt->cfg = *cfg;
    QACTIVE_PUBLISH(&evt->super, Q_ACTIVE_UPCAST(me));
}

/*----------------------------------------------------------------------------*/
/* ISR 事件端口                                                               */
/*----------------------------------------------------------------------------*/

static void on_frame(void* ctx)
{
    modbus_ao_t* me = (modbus_ao_t*)ctx;

    QACTIVE_POST(Q_ACTIVE_UPCAST(me), &frameEvt, (void*)0);
}

static void on_sent(void* ctx)
{
    modbus_ao_t* me = (modbus_ao_t*)ctx;

    QACTIVE_POST(Q_ACTIVE_UPCAST(me), &txDoneEvt, (void*)0);
}

static void on_timeout(void* ctx)
{
    modbus_ao_t* me = (modbus_ao_t*)ctx;

    QACTIVE_POST(Q_ACTIVE_UPCAST(me), &txTimeoutEvt, (void*)0);
}

/*----------------------------------------------------------------------------*/
/* 诊断                                                                       */
/*----------------------------------------------------------------------------*/

/** @brief 两次队列水位诊断之间的 CJC 心跳数。 */
#define MODBUS_AO_DIAG_PERIOD_HEARTBEATS 60U

/**
 * @brief 上报每个已启动 AO 事件队列的低水位。
 *
 * docs/qp-application-design.md 第 7 节的容量论证依赖调度性质（QV 每次
 * 激活派发一个事件，且 AoModbus 是最高优先级订阅者），而不依赖队列余量；
 * 本诊断把该论证变成实测证据。getQueueMin 报告自启动以来见过的最小空闲
 * 空间——启动突发与 8×100 Hz 长跑都在每个 AO 的一个数字里留下痕迹。
 */
static void report_queue_watermarks(const modbus_ao_t* me)
{
    uint8_t prio;

    elog_debug("modbus_ao", "tx done=%u timeout=%u",
               (unsigned)me->diag.tx_done_cnt, (unsigned)me->diag.tx_timeout_cnt);

    for ( prio = (uint8_t)APP_PRIO_CH_FIRST; prio <= (uint8_t)APP_PRIO_MODBUS; ++prio ) {
        uint16_t const min_free = QActive_getQueueMin(prio);

        if ( min_free != 0xFFFFU ) { /* 未启动的 AO 返回哨兵值 */
            elog_debug("modbus_ao", "qmin prio=%u free=%u",
                       (unsigned)prio, (unsigned)min_free);
        }
    }
}

/*----------------------------------------------------------------------------*/
/* 构造                                                                       */
/*----------------------------------------------------------------------------*/

/** @brief 报告从站 ID 的载荷；无每实例状态。 */
static const uint8_t additional[] = "8CH-AI-RTU";

/** @brief 由功能码 0x11 上报的身份。 */
static const modbus_slave_identity_t identity = {
    0xFFU,                              /**< slave_id */
    1U,                                 /**< is_running */
    additional,                         /**< additional */
    (uint16_t)(sizeof(additional) - 1U) /**< additional_length */
};

void modbus_ao_ctor(modbus_ao_t* me,
                    modbus_slave_t* slave,
                    const modbus_slave_config_t* slave_cfg)
{
    uint8_t ch;
    modbus_slave_config_t config;
    modbus_slave_port_event_t ev;
    struct fdb_default_kv factory;
    fdb_err_t st;

    /* 两半都必需：从站核心在本步构建。 */
    Q_ASSERT(slave != NULL);
    Q_ASSERT(slave_cfg != NULL);

    QActive_ctor(&me->super, Q_STATE_CAST(&modbus_ao_initial));

    me->slave               = slave;
    me->store.db_ok         = 0U;
    me->store.commit_failed = 0U;
    me->config.addr         = REG_MAP_DEFAULT_DEV_ADDR;
    me->config.addr_ok      = 0U;
    me->telemetry.cjc_temp  = 0.0f;
    me->telemetry.cjc_valid = 0U;
    me->diag.tx_done_cnt    = 0U;
    me->diag.tx_timeout_cnt = 0U;
    me->diag.hb_ctr         = 0U;

    for ( ch = 0U; ch < BOARD_CHANNEL_COUNT; ++ch ) {
        /* 在首条 MEASURE_SIG 之前，通道上报“未配置”。 */
        memset(&me->config.cfg[ch], 0, sizeof(me->config.cfg[ch]));
        me->config.cfg_ok[ch]    = 0U;
        me->config.applied[ch]   = 0U;
        me->telemetry.value[ch]  = 0.0f;
        me->telemetry.status[ch] = (uint16_t)CH_STATUS_UNCONFIGURED;
    }

    /* 参数数据库在此打开（FlashDB 在 _fdb_init_ex 内自行初始化 FAL，故
     * 无需显式 fal_init()）；加载在初始转换中执行。default_kv 包装是栈上
     * 临时变量：厂商按值复制，只保留 kvs 指针。 */
    factory.kvs = param_defaults;
    factory.num = sizeof(param_defaults) / sizeof(param_defaults[0]);
    st          = fdb_kvdb_init(&param_db, PARAM_DB_NAME, BOARD_PARAM_PARTITION,
                                &factory, NULL);
    if ( st != FDB_NO_ERR ) {
        elog_error("modbus_ao", "fdb kvdb init failed (fdb %d)", (int)st);
    } else {
        me->store.db_ok = 1U;
    }

    /* 本模块拥有配置中的回调与身份半边；核心会复制该表，实例内的副本
     * 才是生效的那份。 */
    config                             = *slave_cfg;
    config.callbacks.ctx               = me;
    config.callbacks.coils             = &coils;
    config.callbacks.discrete_inputs   = &discrete;
    config.callbacks.holding_registers = &holding;
    config.callbacks.input_registers   = &input;
    config.callbacks.address           = &address;
    config.identity                    = identity;
    if ( modbus_slave_ctor(slave, &config) != MODBUS_SLAVE_OK ) {
        /* 缺陷：配置在装配时已核验。 */
        Q_ERROR();
    }

    /* 核心会复制该端口；此时核心仍处于停止态。 */
    ev.ctx               = me;
    ev.on_frame_received = on_frame;
    ev.on_frame_sent     = on_sent;
    ev.on_tx_timeout     = on_timeout;
    (void)modbus_slave_set_event_port(slave, &ev);
}

/*----------------------------------------------------------------------------*/
/* 状态机                                                                     */
/*----------------------------------------------------------------------------*/

/**
 * @brief 顶层初始转换：启动加载，然后进入服务。
 *
 * 在 QActive_start() 内、QF_run() 之前执行：发布已加载的配置时，每个
 * 通道都已启动并完成订阅。
 */
static QState modbus_ao_initial(modbus_ao_t* const me, void const* const par)
{
    uint8_t ch;

    Q_UNUSED_PAR(par);

    /* 订阅在初始转换中完成（QP/C 示例风格）。 */
    QActive_subscribe(Q_ACTIVE_UPCAST(me), MEASURE_SIG);
    QActive_subscribe(Q_ACTIVE_UPCAST(me), CJC_SIG);
    QActive_subscribe(Q_ACTIVE_UPCAST(me), CFG_APPLIED_SIG);

    load_all(me);
    for ( ch = 0U; ch < BOARD_CHANNEL_COUNT; ++ch ) {
        if ( me->config.cfg_ok[ch] != 0U ) {
            publish_cfg(me, ch, &me->config.cfg[ch]);
        }
    }

    return Q_TRAN(&modbus_ao_serving);
}

/**
 * @brief Serving：核心启动后的常驻服务状态。
 */
static QState modbus_ao_serving(modbus_ao_t* const me, QEvt const* const e)
{
    QState status;

    switch ( e->sig ) {
        case Q_ENTRY_SIG: {
            if ( modbus_slave_start(me->slave) != MODBUS_SLAVE_OK ) {
                /* 板级缺陷；端口在构造时已核验。 */
                Q_ERROR();
            }
            status = Q_HANDLED();
            break;
        }
        case FRAME_SIG: {
            (void)modbus_slave_advance(me->slave);
            status = Q_HANDLED();
            break;
        }
        case TX_DONE_SIG: {
            ++me->diag.tx_done_cnt;
            status = Q_HANDLED();
            break;
        }
        case TX_TIMEOUT_SIG: {
            ++me->diag.tx_timeout_cnt;
            status = Q_HANDLED();
            break;
        }
        case MEASURE_SIG: {
            measure_evt_t const* m = Q_EVT_CAST(measure_evt_t);

            me->telemetry.value[m->ch]  = m->value;
            me->telemetry.status[m->ch] = m->status;
            status                      = Q_HANDLED();
            break;
        }
        case CFG_APPLIED_SIG: {
            uint8_t ch = Q_EVT_CAST(cfg_applied_evt_t)->ch;

            if ( ch < BOARD_CHANNEL_COUNT ) {
                me->config.applied[ch] = 1U;
            }
            status = Q_HANDLED();
            break;
        }
        case CJC_SIG: {
            cjc_evt_t const* cjc = Q_EVT_CAST(cjc_evt_t);

            me->telemetry.cjc_temp  = cjc->temp_c;
            me->telemetry.cjc_valid = cjc->valid;

            /* 1 Hz 心跳同时充当诊断节拍。 */
            ++me->diag.hb_ctr;
            if ( me->diag.hb_ctr >= MODBUS_AO_DIAG_PERIOD_HEARTBEATS ) {
                me->diag.hb_ctr = 0U;
                report_queue_watermarks(me);
            }
            status = Q_HANDLED();
            break;
        }
        default: {
            status = Q_SUPER(&QHsm_top);
            break;
        }
    }
    return status;
}
