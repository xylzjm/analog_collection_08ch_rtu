# AoModbus — Modbus 从站活动对象

协议终结者：拥有 `modbus_slave` 实例（USART1/TMR14 经板级端口）、数据空间类型化
镜像、保持寄存器写事务与参数持久化（FlashDB `fdb_kvdb` 直连，宿主测试在
链接边界以 `tests/stubs/fdb_fake.c` 替换四个 FlashDB 入口）；
`reg_map` 是它调用的纯映射。
一个 AO 一个文件（`app/modbus_ao.h/.c`）。总体设计见
`docs/qp-application-design.md`。

## 1. 基线与范围

| 项目 | 内容 |
| --- | --- |
| 需求 | 通道配置写（类型/速率/线性上下限/校准）、遥测读、从站地址写；通道故障经状态寄存器独立上报，冷端+设备故障合并为设备故障位域经输入寄存器读取 |
| 协议 | Modbus RTU 9600 8N1；FC03/FC06/FC16 保持寄存器、FC04 输入寄存器、FC17 报告 ID；FC01/02/05/15 无数据空间（回 01） |
| 数值编码 | **float32（IEEE-754），2 寄存器，大端字序（高字在低地址）**；u16 单寄存器 |
| 独占资源 | `modbus_slave_board_get()` 实例与 `modbus_slave_board_config()` 平台配置、RX/TX 缓冲（板级 256 B 各）、参数分区（FlashDB `fdb_kvdb`） |
| 镜像（本 AO 字段，分组 `config`/`telemetry`/`store`） | `config.cfg[]`（已固化配置，**兼变化检查基准**）、`config.cfg_ok[]`、`config.applied[]`、`telemetry.value[]/status[]`、`telemetry.cjc_temp/cjc_valid`、`config.addr/addr_ok`、锁存 b1（`store.commit_failed`）、`store.db_ok` |
| 假设 | 从站地址经保持寄存器 128 可写（1..247）并固化（KV `dev/addr`，出厂默认 1）；波特率可配置列为扩展（届时在设备级块新增定义） |

## 2. 职责与不变量

1. **写入语义（同步固化，整帧拒绝）**：一次 FC06/FC16 = 一次"写入动作"；通道块按
   16 寄存器步进分隔（块间空档未映射），跨通道写命中空档天然回 02——每帧恰作用于
   一个通道块或设备寄存器。整帧先全量校验，任一字段非法整帧拒绝（03，无部分生效）；
   合法则在回调内**同步固化 + 写后读回校验**（应答以存储实际状态为准：读回=下发值
   才算成功）：成功才更新镜像、发布 `CONFIG_SET_SIG` 并回 00；固化失败整笔拒绝——
   镜像不动、不发布、回 04 并置 b1。同值免写直接回 00。
2. **不做任何兜底**：固化失败无自动重试/补写/延迟合并；操作人员重写寄存器即重试。
   多通道配置请上位机分帧（每帧一个通道块），同时保证帧级原子性与写回调 RTC ≤ 30 ms。
3. **从站地址语义**：寄存器 128 同路径同步固化；成功后新地址对**下一帧**生效（本帧
   应答照常完成），失败回 04 且地址不变；`callbacks.address` 返回镜像。
4. **镜像不变量**：保持寄存器镜像 = 当前**已固化**配置（仅固化成功后更新）；同时它
   就是变化检查快照（总体设计 §1 理由 3），不再另存 snapshot。`config.applied[]` 记录通道
   是否已接受；输入寄存器镜像 = 各事件最近状态；故障锁存随镜像维护（§5）。
5. **同步可读**：寄存器回调在 `modbus_slave_advance()`（本 AO RTC）内同步执行，
   事件之外无共享数据旁路。帧事件用静态事件（核心 CAPTURED 期丢弃字节，至多一条
   在途）。
6. **单步构造（消除装配环）**：`modbus_ao_ctor(me, slave, slave_cfg)`
   接收板级从站实例与平台半配置（`modbus_slave_board_config()`：端口表、收发缓冲、
   波特率），在 AO 内补齐自己的半边（回调表 ctx=me、身份）后调用
   `modbus_slave_ctor()` 并安装事件端口——回调/身份单源在本模块，配置组装不绕
   组合根，板级因此只是纯提供者（平台配置）。事件端口安装是构造的内部
   细节（此时核心必处停止态），启动仍在 Serving entry 的 `modbus_slave_start()`。

## 3. 事件契约（本 AO 相关行）

| 信号 | 参数 | 来源 | 处理 |
| --- | --- | --- | --- |
| `FRAME_SIG` | 无 | TMR14 T3.5 ISR | `modbus_slave_advance()`（NONE/HANDLED/SENDING 均安全） |
| `TX_DONE_SIG`/`TX_TIMEOUT_SIG` | 无 | TX ISR | 诊断计数，核心已自行恢复 |
| `MEASURE_SIG` | `{ch,value,status}` | 发布（AoChannel） | `telemetry.value/status[ch]` 覆盖写 |
| `CJC_SIG` | `{temp_c,valid}` | 发布（AoColdJunction） | `telemetry.cjc_temp/cjc_valid` 覆盖写 + b0 判定；兼诊断节拍 |
| `CFG_APPLIED_SIG` | `{ch}` | 发布（AoChannel） | `config.applied[ch]=1`（接受路径记账） |
| `CONFIG_SET_SIG` | `{ch,cfg}` | **输出**：本 AO（启动加载 / 写帧提交成功）→ 8×`AoChannel` | 每通道一条；仅提交成功后发布 |

## 4. 寄存器映射（0 基线地址）

实现形态（跨度表、寻址判定、编解码接口）见 `docs/component/reg_map.md`。

### 保持寄存器（FC03 读 / FC06、FC16 写）

通道配置 8 块 × 10 寄存器，按 **16 寄存器步进**分隔，通道 n 基址 = `(n-1)*16`；
块内 +10..+15 为空档（不在映射内，访问回 02）。有效地址：0..9、16..25、32..41、
48..57、64..73、80..89、96..105、112..121、128（设备寄存器），其余一律回 02。

| 偏移 | 名称 | 编码 | 写校验 |
| --- | --- | --- | --- |
| +0 | 输入类型（合成码） | u16：0..7=热电偶分度 B/E/J/K/N/R/S/T；8=PT100；9=4-20 mA；10=0-10 V | 0..10 |
| +1 | 采样速率 | u16 枚举码 `rate_code_t` | 0..12 |
| +2..+3 | 线性下限工程值 | float32 | 有限浮点；非线性型归一 0.0 |
| +4..+5 | 线性上限工程值 | float32 | 同上 |
| +6..+7 | 校准偏移 | float32 | 有限浮点（出厂 0.0） |
| +8..+9 | 校准增益 | float32 | 有限浮点（出厂 1.0） |

速率码 0..12 = 2.5/5/10（出厂默认）/12.5/15/20/25/30/40/50/60/80/100 Hz。

设备级寄存器（128，与通道区之间 122..127 空档）：

| 偏移 | 名称 | 编码 | 写校验 |
| --- | --- | --- | --- |
| 128 | 从站地址 | u16 | 1..247，非法整帧回 ILLEGAL_DATA_VALUE(03) |

地址写语义见 §2.3；若遗忘新地址只能经 SWD 找回——运维须知。129 以上未实现，访问
回 02。

### 输入寄存器（FC04 读）：0..71

通道块 8 寄存器，通道 n 基址 = `(n-1)*8`：+0..+1 工程量测量值（校准后
`raw×gain+offset`，float32）；+2 通道状态 u16（0..7 码表见 `channel_ao.md` §3）；
+3..+7 保留读 0。通道故障不占用设备故障位域：状态寄存器是通道健康度唯一上报点。

系统块基址 64：+0..+1 冷端温度 float32；+2..+3 保留；+4 **设备故障位域** u16（§5）；
+5..+7 保留。冷端有效性不在寄存器层单列，由故障位 b0 表达。

## 5. 设备故障位域（u16，输入寄存器 68）

| 位 | 含义 | 置位/清除语义 |
| --- | --- | --- |
| b0 | 冷端故障 | 实时：冷端无效（`!telemetry.cjc_valid`，含未就绪/传感器失效）——冷端有效性唯一上报点 |
| b1 | 参数写入失败 | 锁存（写侧）：写回调固化失败（EX 04 路径）置位；下一次成功固化清除 |
| b2 | 参数加载失败 | 锁存（读侧）：启动加载失败置位；对应参数随后成功固化后清除；该参数不下发、对应通道不投入运行 |
| b3 | FlashDB 初始化失败 | 锁存（不可恢复，产品报废级）：`fdb_kvdb_init()` 失败置位；**复位前不清除**；无任何配置可下发，Modbus 按默认地址运行 |
| b4..b15 | 保留 | 恒 0 |

**b1 显式锁存、b2/b3 推导**（总体设计 §1 理由 4）：读回调现算

```text
load_failed = (!config.addr_ok) || (任一已装配通道 config.cfg_ok[ch] == 0)
latched = (store.commit_failed        ? REG_MAP_FAULT_PARAM_WRITE)
        | (store.db_ok && load_failed ? REG_MAP_FAULT_PARAM_LOAD)
        | (!store.db_ok               ? REG_MAP_FAULT_FDB_INIT)
```

两道守卫：b2 仅在 `store.db_ok` 时存在——存储没打开时只报 b3，不把"没加载"误报成
"加载失败"；`load_failed` 扫描只看 `ch < CH_CONFIG_CHANNEL_COUNT`（SKU 掩码，
未装配位置不报故障）。b0 实时、b1..b3 锁存，合成经 `reg_map_fault_word()`（纯函数）。

三类 flash 故障语义：

- **写入失败（b1）**：EX 04 整笔拒绝（既不生效也未固化）+ 锁存；无兜底；重写即重试，
  成功清 b1。
- **加载失败（b2）**：与初始化失败同级严重——**绝不私自以默认参数让产品运行**。部分
  成功部分启动；失败参数不下发、通道保持未配置态（状态 1）等上位机下发。唯一例外：
  `dev/addr` 加载失败 → 默认地址 1（保证上位机可达），照常报 b2。三种成因：键缺失
  （异常擦除/写中断）、KV 损坏（CRC 失败被跳过）、语义非法/长度不符（`config_validate`
  拒绝）。
- **初始化失败（b3）**：无任何参数可加载 → 无通道投入运行；按默认地址运行并把情况
  反映给上位机。

## 6. 写事务、持久化与状态机

### 状态机

```text
AoModbus (top, 初始 → Serving)
└── Serving         （modbus_slave_start 后的常驻服务态，全部内部转换）
```

初始转换（`QActive_start` 上下文，`QF_run` 之前）：订阅 `MEASURE_SIG`/`CJC_SIG`/
`CFG_APPLIED_SIG` → 启动加载（`load_all`：逐键读取+校验入镜像，失败如实记 `config.cfg_ok`
清零，绝不顶替）→ 为 `config.cfg_ok` 通道逐个发布 `CONFIG_SET_SIG` → `Q_TRAN(Serving)`
（entry `modbus_slave_start()`，失败=断言）。地址在开接收前已知，不存在地址竞态。
Serving：`FRAME_SIG`→`advance()`；`TX_*`→计数；`MEASURE/CJC`→镜像覆盖写（CJC 兼
诊断节拍，每 60 个心跳上报各 AO 队列低水位）；`CFG_APPLIED_SIG`→`config.applied[ch]=1`。

### 保持寄存器写事务（FC06/FC16 同路径，原子性全部所在）

```text
reg_map_check                        缺席/空档/跨块 → EX 02
CH_CFG：encode(config.cfg[ch]) → 帧覆盖 → decode（读-改-写，单寄存器写天然正确）
        → config_validate（含归一化）；非法 → EX 03（整帧拒绝）
DEV：  addr_valid（1..247）；非法 → EX 03
变化检查 vs me->config.cfg[]/me->config.addr（= 变化检查快照）：
    无变化且 config.applied[ch]=1 → EX 00（不写 flash、不通知）
    无变化且 config.applied[ch]=0 → EX 00（重新发布 CONFIG_SET_SIG，让通道重试 apply）
    有变化 → save_cfg/save_addr 同步固化（param_commit：写入 + 读回 memcmp）：
        读回=下发值 → 更新镜像、config.applied[ch]=0；发布 CONFIG_SET_SIG；清 b1 → EX 00
        读回≠下发值 → 镜像不动、不发布、b1 置位 → EX 04（SLAVE_DEVICE_FAILURE）
```

**EX 00 = 已固化**（不承诺通道已接受——apply 结果经 `CFG_APPLIED_SIG` 记账，失败
用状态 6 表达，同值重写即重试）；**EX 04 = 本次写入未固化**。应答以读回为准，不依赖
存储库返回码——"EX 04 但已固化"的分歧角被读回校验收口，残余仅为写与读回双重误判
（报废级故障域）。设备寄存器写不触发 CONFIG_SET（地址变更不通知通道）。
发布在回调内完成（`publish_cfg()`，启动加载与写事务共用）。

### 持久化（本 AO 私有 static，FlashDB 直连）

| 键 | 值 | 说明 |
| --- | --- | --- |
| `cfg/ch0`..`cfg/ch7` | `ch_config_t` 20 B 本体（无版本头） | 每通道独立键，单通道写不牵动他通道 |
| `dev/addr` | u16 本体（2 B） | 从站地址 1..247 |

- **KV 布局单源**：键名与出厂默认值在 `modbus_ao.c` 的默认表（由
  `BOARD_POSITIONS` 展开 + `dev/addr` 行，行数编译期守卫）；键集是底板 8 位置
  布局，少通道 SKU 的未装配键在出厂首启随默认表一并物化，但不加载/不使用。
- **加载语义（绝不顶替）**：键缺失/损坏/语义非法 = 加载失败——不下发、不顶替、报
  b2（§5）；空 flash 首启由默认表物化（FlashDB `fdb_kv_set_default`），加载即有效值，
  **出厂首启不报故障**。`FDB_KV_AUTO_UPDATE` 保持关闭：它会静默补写缺失键，把 CRC
  坏键掩盖成"从未损坏"。
- **变化检查 + 读回**：变化检查基准就是镜像（§2.4），语义等价配置字节相等由
  `config_validate` 归一化保证；固化 = `param_commit()`（`fdb_kv_set_blob` 写入
  + `fdb_kv_get_blob` 读回 memcmp），成败以读回
  为准。**不做任何兜底**（无自动重试/补写/延迟合并）。
- **边界面**：`modbus_ao.c` 直接调用四个 FlashDB 入口（`fdb_kvdb_init`/`fdb_kv_get_blob`/
  `fdb_kv_set_blob`/`fdb_blob_make`；modbus_ao.c 是唯一包含 flashdb.h 的生产 TU）；
  `fdb_kvdb` 分区名单源 `BOARD_PARAM_PARTITION`（`fal_cfg.h` 与 `fdb_kvdb_init()` 共同
  消费）；宿主测试以 `tests/stubs/fdb_fake.c` 在链接边界替换这四个入口（可注入读回
  不一致；`test_fake_medium()` 钉住其契约）。
- 固化为**同步**执行是协议决策（EX 04 语义要求固化在写回调返回前完成）；异步方案
  否决理由：先生效后固化会掉电静默回退，先应答后生效破坏"应答即提交"语义。

## 7. 容量与时序

| 项目 | 数值 | 依据 |
| --- | --- | --- |
| 队列 | 16 | QV 抽干性质 + 发布收敛（本 AO 最高优先级），论证见总体设计 §7；水位经 `QActive_getQueueMin()` 取证 |
| RTC（写帧，含同步固化） | ≤30 ms | advance + 校验 + 1 次 KV 固化 + 读回校验（写帧限单块）——本 AO 最长 RTC |
| RTC（普通帧/遥测事件） | ≤0.5 ms | advance + 镜像覆盖写 |
| 应答延迟 | ≤30 ms 最坏 | 仅写帧含固化；读帧 ≤1 ms（端到端应答延迟，含排队等待，区别于上方单步 RTC） |
| 静态事件 | 3（FRAME/TX_DONE/TX_TIMEOUT） | 在途 ≤1 的互斥已证 |

## 8. 故障路径与验证要点

| 故障 | 分类 | 处理 |
| --- | --- | --- |
| 非法配置写 | 合法异常（主站错误） | EX 03 整帧拒绝 |
| 地址越界/空档/跨块 | 合法异常 | EX 02 |
| 固化失败 | 合法异常（不可恢复） | EX 04 整笔拒绝 + b1 锁存；无兜底；重写寄存器重试 |
| `modbus_slave_start()` 失败 | Bug | `Q_ERROR()`（断言） |
| TX 看门狗超时 | 合法异常 | 核心 resync + `TX_TIMEOUT_SIG`，诊断计数 |

宿主测试（存储介质为 `tests/stubs/fdb_fake.c` 假件）：`tests/test_modbus_data.c`（回调级直驱写事务+持久化：校验矩阵、提交/读回、同值免写计数、提交失败 EX 04+b1、同值重写重通知、地址写、初始化失败存储、假件契约 `test_fake_medium()`）与
`tests/test_modbus_ao.c`（帧级端到端，含缺键/超长本体的加载拒绝）。上板验证要点（尚未在硬件收口）：读写回环、
掉电循环（写入瞬间反复断电，旧值或新值）、设备故障位 b0..b3 组合实验、地址变更、
应答时序。
