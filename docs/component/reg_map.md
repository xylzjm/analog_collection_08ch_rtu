# reg_map — Modbus 数据空间映射组件（纯函数无状态）

被动组件（**非活动对象**）：Modbus 四个数据空间的寻址判定与寄存器编解码，经 `AoModbus`
的寄存器回调在 `modbus_slave` 执行上下文内调用。无状态、无定时器、无自有数据——类型化
镜像归 `AoModbus`，写事务纪律（原子性/固化）在写管线收口。寄存器**数据定义**（地址/
编码/写校验）见 `docs/ao/modbus_ao.md` §4，本文是**实现形态**（接口、寻址、编解码、
异常口径、写管线编排）。总体设计见 `docs/qp-application-design.md`。

> 不建独立 AO 的依据：纯函数无自主行为、无独立响应期限；寄存器访问本身就是
> `AoModbus` RTC 步的一部分（回调内联执行）。

## 1. 基线与范围

| 项目 | 内容 |
| --- | --- |
| 需求 | 四数据空间寻址判定（空档/跨块/越界 → EX 02）、寄存器↔类型化数据编解码（大端）、写管线编排（原子性骨架） |
| 对手方 | `modbus_slave` 核心区间回调 `(ctx, start, count, buffer, mode)`：缓冲区 = count 个大端 u16（`modbus_slave_get_u16`/`put_u16` 访问），回调返回异常码即应答 |
| 状态 | 无（全部纯函数）；跨度表为 ROM 常量 |
| 依赖 | `ch_config.h`（校验/归一化）；写管线经 `AoModbus` 调 `param` 组件 |
| API | `reg_map_find`/`reg_map_check`/编解码函数（`app/component/inc/reg_map.h`） |

## 2. 数据空间与跨度表（地图唯一出处）

```c
/** @brief Modbus 四个数据空间。 */
typedef enum {
    REG_SPACE_COIL = 0,     /**< 线圈（FC01/05/15），位读写 */
    REG_SPACE_DISCRETE,     /**< 离散量（FC02），位只读 */
    REG_SPACE_HOLDING,      /**< 保持寄存器（FC03/06/16），寄存器读写 */
    REG_SPACE_INPUT         /**< 输入寄存器（FC04），寄存器只读 */
} reg_space_t;

/** @brief 区块种类：驱动编解码分派。 */
typedef enum {
    REG_SPAN_CH_CFG = 0,    /**< 通道配置块（10 寄存器） */
    REG_SPAN_DEV,           /**< 设备寄存器（从站地址） */
    REG_SPAN_CH_IN,         /**< 通道遥测块（8 寄存器） */
    REG_SPAN_SYS            /**< 系统块（CJC/故障字） */
    /* 将来：REG_SPAN_COIL_x / REG_SPAN_DISCRETE_x */
} reg_span_kind_t;

/** @brief 一个连续映射区（跨度）。 */
typedef struct {
    reg_space_t     space;  /**< 所属数据空间 */
    uint16_t        base;   /**< 起始地址（0 基） */
    uint16_t        n;      /**< 长度：寄存器数或位数 */
    reg_span_kind_t kind;   /**< 区块种类，驱动编解码分派 */
} reg_span_t;
```

一张按 `space` 分段的静态 const 表即寄存器地图的**唯一出处**：holding 段 9 项
（8×`CH_CFG` @ 16n + `DEV` @ 128）、input 段 9 项（8×`CH_IN` @ 8n + `SYS` @ 64..71）、
coil/discrete 段**为空**。"空间有无映射"由表回答，产品裁剪零成本。

**SKU 装配数过滤**：表描述底板 8 通道位置的全布局；`CH_CONFIG_CHANNEL_COUNT`
（装配数，编译期固定）把未装配位置过滤为 EX 02——同一工程支持少于 8 通道的产品，
既有通道地址不随装配数变化。KV 键集（`cfg/ch1..8`）同为 8 位置布局，未装配通道的键
存在但不被加载/使用。

字段一律枚举类型直用（不用 `uint8_t` 退化）：类型安全、MISRA essential type 干净、
自文档；表在 ROM，打包收益为零。

## 3. 寻址与异常口径

```c
/** @brief [start, start+count) 整体落在 space 的某个跨度内 → 该跨度；否则 NULL。 */
const reg_span_t* reg_map_find(reg_space_t space, uint16_t start, uint16_t count);

/** @brief 回调统一入口：EX_NONE/01/02 判定 + 命中跨度输出。 */
modbus_slave_exception_t reg_map_check(reg_space_t space,
                                       uint16_t start, uint16_t count,
                                       const reg_span_t** out);
```

| 情形 | 码 | 归属 |
| --- | --- | --- |
| 空间无任何跨度（本产品 coil/discrete） | EX 01 | `reg_map_check` |
| 有跨度但区间不整体落在单跨度内（越界/空档/跨块） | EX 02 | `reg_map_check` |
| 命中但写值语义非法（类型码/速率码/非有限浮点/地址 1..247） | EX 03 | 写管线（`ch_config_validate`） |
| 固化/读回失败 | EX 04 | 写管线（`param_save_*`） |

EX 01 的口径：空间整体不存在 = 函数不支持（比 EX 02 诚实——不是地址错，是本产品
没有线圈/离散量）。

## 4. 编解码

- **字节序**只在本模块出现一次：u16 经 `modbus_slave_get_u16`/`put_u16`；float32 =
  2 寄存器大端字序（高字在低地址）。
- **通道块 codec**（布局见 `modbus_ao.md` §4）：
  - `reg_map_ch_encode(cfg, words)`：类型化 → 10 字；
  - `reg_map_ch_decode(words, cfg)`：10 字 → 类型化（只管布局，语义校验归 `ch_config`）；
  - 子块写 = **读-改-写**：`encode(当前) → 帧覆盖 → decode(全块)`——任意单寄存器写
    天然正确，且 `ch_config_validate` 的归一化顺带保证同义配置字节一致（memcmp 不抖动）。
- **设备寄存器 codec**：`dev` u16 直通 + 1..247 校验。
- **输入区编码**：`reg_map_input_encode(in, start, count, buf)` 从类型化镜像
  （值/状态/CJC/故障字）逐段拼装，保留槽读 0；无写路径。
- **往返无损**：`decode(encode(x)) == normalize(x)`（宿主测试守护）。

## 5. 回调接线与写管线

`modbus_slave_ctor` 不接受 NULL 回调——四个数据空间回调 + 地址回调**全部实装**：

| 回调 | 接线 | 行为 |
| --- | --- | --- |
| `coils`/`discrete_inputs` | 转发 `reg_map_check` | 本产品段为空 → 恒 EX 01 |
| `input_registers` | `reg_map_check` + `reg_map_input_encode` | 只读 |
| `holding_registers` | 读：`reg_map_check` + encode；写：写管线（下） | 读写 |
| `address` | `modbus_image` 镜像 | 1..247 |

写管线（原子性全部所在，由 `modbus_image` 组件承载，与 `docs/component/modbus_image.md` 同源）：

```text
holding 写回调：
    reg_map_check                       缺席 → EX 02
    CH_CFG：encode(cfg[ch]) → 帧覆盖 → decode → ch_config_validate（含归一化）
    DEV：  校验 1..247（非法 → EX 03）
    param_save_*（同步固化 + 写后读回）
        读回=下发值 → 提交类型化镜像（通道块发布 CONFIG_SET_SIG）→ EX 00
        读回≠下发值 → 什么都不动 → EX 04（b1）
```

EX 00 ⟺ 生效且已固化（同值免写）；03/04 整帧不动——原子性纪律在 `AoModbus` 写回调层
收口，本组件只提供编排所需的寻址与编解码。

## 6. 设计取舍记录

| 方案 | 要害 | 结论 |
| --- | --- | --- |
| 原始 u16 镜像数组 | raw 与类型化双源同步，三处提交点可漂移；RAM +400 B；9600 波特率下 memcpy 收益是噪声 | 否 |
| 手写编解码（无表） | 地址常量散落 2-3 个函数，读写对称靠测试 | codec 手写采纳 |
| 描述符表全驱动 | 遍历框架机制 > 需求（本地图两种块 codec、出厂冻结） | 寻址表思想采纳 |
| **类型化单源 + 跨度表 + 手写块 codec（采用）** | 镜像不漂移；地址唯一出处；机制最小 | 采用 |

通用性预留：未来加线圈/离散量 = 表加跨度行 + 一个位编解码（LSB-first 打包格式见
`modbus_slave.h`）+ 状态存储；`find`/`check`、回调接线、EX 口径**零改动**，FC01/02/05/15
自动从 EX 01 转为正常服务。

## 7. 定时与容量

| 项目 | 数值 | 依据 |
| --- | --- | --- |
| 定时器/队列/事件 | 无 | 纯函数，回调内联执行 |
| ROM 跨度表 | ~18 行 × 12 B ≈ 220 B | 枚举字段直用的自然布局，无打包需求 |
| RAM | 0 | 无状态；类型化镜像归 `AoModbus` |
| RTC | 含在帧处理内（≤0.1 ms 量级，估算） | 查表+编解码，相对 30 ms 写回调预算是噪声 |

## 8. 验证矩阵（宿主 `CHECK` 测试）

| 测试 | 激励 | 预期 | 证据 |
| --- | --- | --- | --- |
| 往返无损 | 任意合法 cfg：encode→decode | 得到 normalize 后等价的 cfg | gcc 宿主可执行 |
| 子块覆盖合并 | 逐寄存器单独写（类型/速率/各 float） | 全块语义正确，未写字段不变 | gcc 宿主可执行 |
| 跨度穷举 | 表驱动遍历 4 空间全地址 × 起点/长度组合 | 命中/空档/跨块/越界判定与 §3 口径一致 | gcc 宿主可执行 |
| 语义非法 | 类型码 >10、速率码 >12、NaN/Inf、地址 0/248 | 校验拒绝（EX 03 路径） | gcc 宿主可执行 |
| 位空间裁剪 | coil/discrete 任意请求 | 恒 EX 01 | gcc 宿主可执行 |
| 输入区布局 | 全字段填充→encode→比对字序列 | 字序/浮点拼字/保留槽 0 正确 | gcc 宿主可执行 |
