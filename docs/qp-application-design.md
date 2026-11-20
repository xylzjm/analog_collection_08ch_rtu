# QP 应用层总体设计（8 通道模拟量采集 RTU）

本文是 `app/` 层设计总体卷：模块地图、事件契约、数据/配置模型、容量与启动。AO 细节见
`docs/ao/`，纯寄存器映射见 `docs/component/reg_map.md`：

| 文档 | 对象 |
| --- | --- |
| `docs/ao/channel_ao.md` | `AoChannel` 通道采集 AO（同类 ×8，`app/channel_ao.h/.c`） |
| `docs/ao/cold_junction_ao.md` | `AoColdJunction` 冷端温度 AO（TMP75，`app/cold_junction_ao.h/.c`） |
| `docs/ao/modbus_ao.md` | `AoModbus` Modbus 从站 AO（镜像+写事务+持久化+故障位域，`app/modbus_ao.h/.c`） |
| `docs/component/reg_map.md` | `reg_map` 纯寄存器映射（寻址/编解码，无状态，`app/reg_map.h/.c`） |

## 1. 基线与关键设计决策

| 项目 | 内容 |
| --- | --- |
| 需求 | 8 路独立模拟输入（TC/PT100/4-20 mA/0-10 V），冷端单片 TMP75；TC/PT100 断线检测与测量互斥、3 s 切换、先检后测；Modbus 按通道配置（类型/速率/线性上下限/校准）与设备参数（从站地址）；通道故障经状态寄存器独立上报，冷端+设备故障合并为设备故障位域；配置写经变化检查后同步固化 |
| 平台/执行 | AT32F421C8T7（120 MHz，60 KB ROM/16 KB SRAM），armclang -O1 + microlib（-O0 超 ROM 预算）；QP/C 8.1.3，QV 协作内核（AO 间不抢占，ISR 只投递）；SysTick 1 ms 唯一 tick 源；QHsm 手写状态处理函数 |
| 构建/测试 | EIDE（uid `2b2c107904df4cdb1e97e57d6947db29`）；断言统一 `Q_ASSERT`/`Q_onError`（RTT 直写）；`tests/` 7 个宿主测试可执行（§9） |

### 架构形状的理由（why）

1. **一个 AO 一个文件**（QP/C 示例式）：AO 是"状态机+定时器+事件接口"的自然封装，
   `channel_ao.*`/`cold_junction_ao.*`/`modbus_ao.*` 各自独立成对，`app.h` 只留共享
   词汇（通道模型、信号、载荷）。旧版按 inc/src 分层把同一状态机拆散在多级目录里；
   文件即对象后，导航、评审、测试都以对象为单位。
2. **回调网必须消失**：旧设计里被动组件经函数指针回调 AO（apply 结果 `on_applied`、
   提交通知 `cfg_notifier`）——被动模块持有指向 AO 的边，所有权倒挂、事件契约被
   旁路、测试要造假回调。现在 apply 结果就是 `CFG_APPLIED_SIG` 事件（AoChannel
   发布，AoModbus 订阅记账 `config.applied[]`），提交成功由 `publish_cfg()` 直接发布
   `CONFIG_SET_SIG`：AO 之间只经事件通信，被动代码不再回调活动对象。
   `cfg_applied_evt_t` 不带 ok 字段——提交时先清 `config.applied[ch]` 再发布 CONFIG_SET，
   失败的 apply 无从改变镜像状态，ok 可证冗余。
3. **镜像即变化检查快照**："同值免写"需要最近一次成功固化的值作比较基准，而类型化
   镜像 `config.cfg[]` 本来就在固化成功后才更新——再存一份 snapshot 是第二个可漂移的源。
   变化检查就是 `memcmp(新值, me->config.cfg[ch])`（`config.cfg_ok[ch]` 标记有效性）；写事务纪律
   "先固化、后动镜像"同时保住"镜像=已固化配置"与比较基准的正确性。
4. **故障位 b2/b3 是推导量**：b1（写失败）是瞬时事件的锁存，必须显式存；b2（加载
   失败）与 b3（存储初始化失败）从加载终局状态每帧现算（`modbus_ao.md` §5），
   零维护且不可能"位与状态不一致"。推导有两道守卫：b2 仅在 `store.db_ok` 时存在（存储
   没打开只报 b3，不把"未加载"误报成"加载失败"）；b2 扫描只看已装配通道（SKU
   掩码，未装配位置不报故障）。
5. **reg_map 独立纯模块，存储折叠进 modbus_ao**：`reg_map` 是互操作规格的函数化
   （地址、编解码、异常口径），无状态、可独立宿主测试、读回调与写事务共用——独立
   成模块的理由不变。存储则与写事务同呼吸（变化检查基准=镜像、EX 00 ⟺ 读回一致、
   b1/b2 记账），拆成 `param` 组件时接口只是把 AO 字段搬出搬进（旧回调网正源于此）；
   折叠后持久化是 `modbus_ao.c` 的私有 static，直接使用 FlashDB（`fdb_kvdb`；
   四个 `fdb_*` 入口构成本 TU 的 FlashDB 边界面，宿主测试在链接边界以
   `tests/stubs/fdb_fake.c` 替换这四个入口）。
6. **速率 tick 表算出来，8 位置表只有一个宏**：13 档速率只有两个用途——驱动配置
   （mHz）与采样节拍（tick 数）。`APP_RATE_LIST` 一个 X 宏生成枚举与 mHz 表，tick
   数由 `ceil(1e6/mHz)` 现算（每次 Measuring entry 一次整数除法），不存在第二份
   手写表。同理 `board_config.h` 的 `BOARD_POSITIONS` 是唯一 8 位置清单：`reg_map` 跨度表与 KV
   默认表都由它展开，SKU 布局不可能漂移。

### 冻结的 wire 契约（各归其文）

寄存器映射/异常码/故障位域/通道状态码见 `modbus_ao.md` §4–§5 与 `channel_ao.md` §3；
写入=同步固化+写后读回（EX 00 ⟺ 已固化，`modbus_ao.md` §6）；先检后测与改配隔离
（`channel_ao.md` §6）；启动序 channels→modbus→cold_junction（§8）。

## 2. 需求与可度量约束

| 类别 | 要求 |
| --- | --- |
| 外部输入 | Modbus RTU 帧（9600 8N1，USART1 ISR）；TMP75 温度（软 I2C）；8 通道 SIG24131 采样（SPI1） |
| 周期/响应 | 每通道 2.5~100 Hz（13 档 `rate_code_t`）；冷端 1 Hz；断线检测窗口 3 s + 4 拍切换空窗；Modbus 应答 ≤ 一个最长他人 RTC（目标 ≤ 40 ms） |
| 最坏突发 | 启动 ≤8×`CONFIG_SET_SIG`；写帧限单通道块（1 帧 1 条配置事件）；测量发布 8×100 Hz = 800 事件/s |
| 最长 RTC | 通道采样步 ≤ 1 ms；冷端步 ≤ 3 ms；Modbus 读帧 ≤ 0.5 ms；写帧（含同步固化）≤ 30 ms |
| RAM 预算 | 新增 ≈ 4.5~5 KB，目标总占用 ≤ 10/16 KB |
| 故障与安全 | 故障位域与通道状态实时上报；加载失败不下发不顶替（b2）；固化失败整笔拒绝（EX 04 + b1）；断言 fail-safe 停机留现场 |

## 3. 活动对象划分与职责

```text
                    ┌────────────────────────────┐
   USART1/TMR14 ─── │ AoModbus         prio 11   │ 镜像 / 写事务 / 持久化 / 故障位域
   (板级端口)        │  modbus_ao.c               │◄── MEASURE_SIG ──┐
   FlashDB(param) ─ │  （reg_map 纯映射被调用）  │◄── CJC_SIG ──┐   │
                    └──────────┬─────────────────┘        ┌───┴───┴────────┐
                  发布 CONFIG_SET_SIG                      │ AoColdJunction │ TMP75+软I2C
                  订阅 CFG_APPLIED_SIG                     │    prio 10     │
                    ┌────────────┐ ×8 SIG24131[ch]+前端开关 └────────────────┘
   SPI1(共享) ───── │ AoChannel  │ prio 2..9
                    └────────────┘
```

| 对象 | 职责/不变量 | 独占资源 | 优先级 | 最长 RTC | 选择理由 |
| --- | --- | --- | --- | --- | --- |
| `AoChannel` ×8 | 该通道采集全生命周期：配置应用、采样换算、校准、断线检测循环、通道状态；不变量：测量/断线硬件模式互斥 | 本通道 `analog_input_t` | 2..9 | ≤ 1 ms | 8 路各有速率/模式/故障生命周期 |
| `AoColdJunction` | 冷端温度供给与有效性；不变量：`CJC_SIG` 每 1 s 心跳（含失败时 invalid） | TMP75 + 软 I2C 总线 | 10 | ≤ 3 ms | 整板唯一 TMP75/I2C 主，多 TC 共享一个冷端源 |
| `AoModbus` | 协议终结；镜像=已固化配置+各事件最近状态；写事务（校验→同步固化+读回→镜像→发布）；持久化；故障位域合成 | `modbus_slave` 实例（USART1/TMR14 经板级端口）、参数分区（FlashDB `fdb_kvdb`） | 11 | ≤ 30 ms（写帧）/ ≤ 0.5 ms（其余） | 帧事件来自 ISR 需独立队列吸收；EX 04 语义要求固化在写回调返回前完成 |

被动模块只有 `reg_map`：纯函数无状态、无独立时限，由 AoModbus 的数据空间回调内联
调用。曾并列的"被动组件"（配置校验、类型化镜像、写事务、参数存储）已折叠进
`modbus_ao.c`：它们的接口只是把 AO 字段搬出搬进，独立成组件的代价就是回调网
（§1 理由 2/3/5）。

## 4. 事件契约总表

| 信号/事件类型 | 参数布局 | 生产者 → 消费者 | 路由 | 存储 | 顺序/过期 | 最坏产生率 |
| --- | --- | --- | --- | --- | --- | --- |
| `FRAME_SIG` | 无参 | TMR14/USART ISR（`on_frame_received`）→ `AoModbus` | 直接 FIFO | 静态 | 每帧一次 | ≤1 在途（核心 CAPTURED 期丢弃字节） |
| `TX_DONE_SIG`/`TX_TIMEOUT_SIG` | 无参 | TX ISR → `AoModbus` | 直接 FIFO | 静态 | 仅诊断计数 | ≤1 在途 |
| `CONFIG_SET_SIG` | `{ch, cfg}`（`ch` **0 基**，与镜像/KV/寄存器块同基） | `AoModbus`（启动加载 / 写帧提交成功）→ 8×`AoChannel` | 发布—订阅 | 动态 池B | 仅提交成功后发布；接收者按 `ch` 过滤；命中→改配隔离（channel_ao.md §6） | 启动 ≤8；写帧 1 |
| `APPLY_SIG` | 无 | `applyEvt` 自投（静态，8 通道共用） | 自投 FIFO | 静态 | 仅 Unconfigured 响应：`configure(pending)`→先检后测重入 | 每次改配 1 |
| `CFG_APPLIED_SIG` | `{ch}` | `AoChannel` → `AoModbus` | 发布—订阅 | 动态 池A | apply 成功即发布，AoModbus 记 `config.applied[ch]=1`；失败不发布（状态 6 + 同值重写重试） | 每次改配成功 1 |
| `MEASURE_SIG` | `{ch, value, status}`（status 为 `ch_status_t`） | `AoChannel` → `AoModbus` | 发布—订阅 | 动态 池A | 只保留最新，镜像覆盖写 | 8×100 Hz = 800/s |
| `CJC_SIG` | `{temp_c, valid}` | `AoColdJunction` → 8×`AoChannel`、`AoModbus` | 发布—订阅 | 动态 池A | 心跳 1 Hz，通道缓存最近一条 | 1 Hz + 启动 1 |
| `SAMPLE_SIG`/`SETTLE_SIG`/`BREAK_WINDOW_SIG`/`TICK_SIG` | 无参 | 各 AO 时间事件自投 | 自投 FIFO | 静态 `QTimeEvt` | 迟到事件经改配隔离落 Unconfigured（channel_ao.md §6） | 见各 AO 文档 |

动态事件由生产者 `Q_NEW` 填完字段再移交，发布后不得再访问；无订阅者时框架回收。
动态事件只三类（池A：MEASURE/CFG_APPLIED/CJC；池B：CONFIG_SET），池容量定义在
`main.c`、尺寸守卫在 `app.h`。不使用 defer/recall——配置写不积压，变化检查吸收重复。

**已删除的旁路**（重构要点）：`on_applied` 回调（apply 结果）、`cfg_notifier`
（提交通知）、`param_bind_store`/`param_boot_load` 胶合一并删除——apply 结果走
`CFG_APPLIED_SIG`，提交直接发布 `CONFIG_SET_SIG`，存储是 `modbus_ao.c` 的私有
static（FlashDB 直连），
启动加载是 AoModbus 初始转换内的普通函数调用。已知良性差异：同值重写在通道的
`CFG_APPLIED` 排空前到达时可能多触发一次重通知（配置相同，主站看到的 EX 00 不变）。

## 5. 数据与配置模型

### 通道配置 `ch_config_t`（`app.h`）

| 字段 | 类型 | 语义 | 校验/归一化 |
| --- | --- | --- | --- |
| `type` | `uint8_t` | 0=TC 1=PT100 2=4-20 mA 3=0-10 V | ∈{0..3} |
| `tc_type` | `uint8_t` | 0=B 1=E 2=J 3=K 4=N 5=R 6=S 7=T | TC 时 ∈{0..7}；非 TC 归一 0xFF |
| `rate_code` | `uint16_t` | 速率枚举码（0..12，2=10 Hz 出厂默认） | ∈ 0..12 |
| `linear_min`/`linear_max` | `float` | 线性输入上下限工程值（仅 2/3 型） | 有限浮点；非线性型归一 0.0 |
| `offset`/`gain` | `float` | 校准：`value = raw × gain + offset` | 有限浮点 |

结构本体 20 B（编译期守卫），**就是** KV 值格式与寄存器块的编解码源，布局不得变动。
校验+归一化是同一操作（`modbus_ao.c` 的 `config_validate`，写回调与启动加载共用）：
语义等价的配置必须字节相等——变化检查（memcmp）依赖这一点。寄存器层的类型/分度
**合成单码**（0..7=TC B..T，8=PT100，9=4-20 mA，10=0-10 V），与 struct 字段的映射
由 `reg_map` 编解码。

速率：13 档 `rate_code_t`（`APP_RATE_LIST` 展开，2.5 Hz..100 Hz）；驱动配置用 mHz
（表值即 `ANALOG_INPUT_BOARD_RATE_*`），采样节拍 tick = `ceil(1e6/mHz)` 现算，
速率恒 ≤ 标称（13 档中 9 档恰为整毫秒，15/30/60/80 Hz 向上取整，误差 ≤ 3.8%）。
8 位置板卡布局由 `board_config.h` 的 `BOARD_POSITIONS` 单宏承载：`reg_map` 跨度表与 KV 默认表
（`cfg/ch0..7` + `dev/addr`，共 9 行，编译期守卫）都由它展开。

### 持久化格式（无版本管理）

KV 键 `cfg/ch0`..`cfg/ch7`（值 = `ch_config_t` 20 B 本体）、`dev/addr`（值 = u16，
出厂默认 1，范围 1..247）。加载侧唯一防线是 `config_validate` 语义校验：缺失/损坏/
非法一律加载失败——不下发、不顶替、报 b2（详见 `modbus_ao.md` §6）。空 flash 首启
由默认表物化、不报故障；`FDB_KV_AUTO_UPDATE` 保持关闭（防静默补键掩盖损坏）。
固件出厂即固定，KV 布局仅开发期演进（全片擦除/重下发即足），不设版本字段。

## 6. 定时与过期（总表）

| 时间事件 | 所有者 | 一次性/周期 | arm 时点 | 迟到防护 |
| --- | --- | --- | --- | --- |
| `te_sample` | AoChannel[ch] | 周期（tick=ceil(1e6/mHz)） | Measuring entry | 信号仅 Measuring 处理 |
| `te_settle` | AoChannel[ch] | one-shot | Detecting/Settling entry | 改配隔离（channel_ao.md §6） |
| `te_break` | AoChannel[ch] | 周期 3000 ms | Measuring entry（可检测型） | 同 `te_sample` |
| `te_tick` | AoColdJunction | 周期 1000 ms | Starting entry | 无切换场景 |

tick 分辨率 1 ms；0 延迟至少 arm 1 拍；非整毫秒周期向上取整（§5）。定时器迟到事件
的结构性规避（改配经 Unconfigured 隔离 + `APPLY_SIG` 跨派发重入，零守卫）见
`channel_ao.md` §6。

## 7. 并发与容量预算（总表）

QV 协作内核：任一 AO 的响应窗口 = 他人最长 RTC（AoModbus 写回调同步固化 ≤ 30 ms）
+ 在途积压。队列安全依赖两条**调度性质**：QV 每次激活只派发一条事件；AoModbus
优先级 11 高于全部发布者（2..10），发布事件一入队它就被激活、立刻抽干。**改优先级
或新增高于 11 的发布者会让此论证静默失效，须重新核算**。实测证据：`QActive_getQueueMin()`
低水位上报（AoModbus 每 60 个 CJC 心跳输出一次全部 AO 水位）。

| 队列 | 深度 | 依据 | RAM |
| --- | --- | --- | --- |
| AoChannel[ch] ×8 | 13 | 启动 8×CONFIG_SET + 1×CJC + ≤3 tick + 1×APPLY + 裕量 | 416 B |
| AoColdJunction | 4 | 1 Hz tick + 裕量 | 16 B |
| AoModbus | 16 | 峰值 ≈ 单次 RTC 扇出（≤8×MEASURE+1×CJC）+ 1×FRAME ≈ 10 + 裕量 | 64 B |

| 事件池 | 块 | 块数 | 依据 | RAM |
| --- | --- | --- | --- | --- |
| 池A（MEASURE/CFG_APPLIED/CJC） | 20 B | 16 | 一轮发布 8×MEASURE+1×CJC + 裕量 | 320 B |
| 池B（CONFIG_SET） | 32 B | 16 | 启动 ≤8 实例 + 裕量 | 512 B |

其余：QTimeEvt 25 个 ≈ 800 B；静态事件 4 个（ROM）；AO 结构 ≈ 1.2 KB；Modbus
缓冲 512 B；FDB 缓存 1.5~2 KB——合计新增 ≈ 4.5~5 KB。CPU 预算**待实测**（-O1）：
8 通道全开 100 Hz ≈ 64% CPU 估算；超预算处置顺序：限 Σ 采样率 → `analog_input_math`
单独 -O1 → 降最大档位，不得丢样本维持"稳定"。

## 8. 启动、停止与故障安全

```c
log_init();                       /* RTT + EasyLogger，必须最先 */
board_init();
QF_init(); QActive_psInit(subscrSto, MAX_SIG); QF_poolInit(...);   /* 池A、池B */
modbus_ao_ctor(&modbus,
               modbus_slave_board_get(), modbus_slave_board_config());
        /* 参数库在 ctor 内打开；回调/身份在 AO 内补齐，一步构造 */
channel_ao_ctor(...); cold_junction_ao_ctor(...);            /* 设备注入 */
QActive_start(ch1..ch8);          /* prio 2..9 */
QActive_start(modbus);            /* prio 11：初始转换内启动加载并发布 CONFIG_SET_SIG */
QActive_start(cold_junction);     /* prio 10：首拍 CJC 晚于全部订阅者 */
QF_run();
```

1. **订阅者先于发布者**：AoModbus 初始转换（`QActive_start` 内、`QF_run` 前）加载
   参数并发布启动 `CONFIG_SET_SIG`，故 8 通道必须先启动（订阅在各 AO 初始转换内）。
   向未启动对象投递由 `QActive_post_` 断言兜底。
2. **参数库在构造内打开**：`modbus_ao_ctor()` 内调用 `fdb_kvdb_init()` 打开
   `fdb_kvdb` 分区（宿主测试以 `tests/stubs/fdb_fake.c` 在链接边界替换四个
   FlashDB 入口）；打开失败 = `store.db_ok=0` → b3、后续提交恒 EX 04（fail-safe）。
3. prioSpec 一律 `Q_PRIO(prio, 0U)`（QV 无抢占阈值，共享主栈）；SysTick 1 ms 经
   `QTIMEEVT_TICK_X` 接入（`board/src/board.c`）。
4. `modbus_slave_start()` 在 AoModbus Serving entry 执行；启动前地址已加载完成，
   不存在地址竞态。

停止与故障安全：无正常停止路径；断言 `Q_onError` RTT 直写后停机；合法外部异常
（Flash 写失败、TMP75 失效、SIG24131 IO 错、断线）走状态机恢复路径。掉电安全由
FlashDB 分阶段写 + CRC + 上电恢复托管（`modbus_ao.md` §6）：配置写任意时刻断电
只可能得到旧值或完整新值。

## 9. 可观测性与验证

- 日志：`elog`（转换关键点、固化 key/结果、队列低水位）；异常现场 `Q_onError` RTT
  直写（ISR 安全）；不使用 QS/QSpy。
- 宿主测试 7 个可执行（`tests/run_host_tests.ps1`）：`test_reg_map`（纯映射）、
  `test_modbus_data`（写事务+持久化，回调级直驱）、`test_channel_ao`、
  `test_cold_junction_ao`（冷端心跳/失效/自愈，tmp75_port_fake 注入）、
  `test_modbus_ao`、`test_boot`（场景 1/2）、`test_analog_input_convert`。
- 上板验证要点（尚未在硬件收口）：写事务全路径（EX 00=已固化 / 03 整帧拒绝 /
  04+b1）、掉电循环（旧值或新值）、断线码（TC 2 / PT100 3、4）、冷端失效 1 s 内
  b0、8×100 Hz 压测低水位、Modbus 应答时序。
