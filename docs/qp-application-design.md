# QP 应用层总体设计（8 通道模拟量采集 RTU）

本文是 `app/` 层活动对象（AO）设计的总体卷：基线、AO 划分、事件契约总表、调度与容量、
启动与故障安全、验证总纲。每个 AO 的细节（状态机、动作轨迹、容量推导、验证矩阵）见
`docs/ao/`（活动对象）与 `docs/component/`（被动组件）下的独立文档：

| 文档 | 对象 |
| --- | --- |
| `docs/ao/channel_ao.md` | `AoChannel` 通道采集 AO（同一类 ×8 实例） |
| `docs/ao/cold_junction_ao.md` | `AoColdJunction` 冷端温度 AO（TMP75） |
| `docs/ao/modbus_ao.md` | `AoModbus` Modbus 从站 AO（含寄存器映射与设备故障位域） |
| `docs/component/param.md` | `param` 参数存储组件（FlashDB 托管，AoModbus 执行上下文，非活动对象） |
| `docs/component/reg_map.md` | `reg_map` 数据空间映射组件（寻址/编解码，纯函数，非活动对象） |

## 1. 基线与范围

| 项目 | 内容 |
| --- | --- |
| 需求 | 8 路互相独立的模拟输入（热电偶/PT100/4-20 mA/0-10 V）；热电偶冷端取自单片 TMP75；TC/PT100 断线检测与正常测量互斥、3 s 周期切换；Modbus 上位机按通道配置（类型/速率/线性上下限/偏移增益校准）与设备参数（从站地址）；通道故障经各通道状态寄存器独立上报，冷端故障+设备类故障合并为设备故障位域；配置写入经变化检查后固化到 FlashDB |
| QP 版本 | QP/C 8.1.3（`middleware/qp/qpc`，`version-8.1.3`）；`QP_API_VERSION 0`（最大兼容层）；仅完整 QP/C，不用 QP-nano、不用 QSpy/QS |
| 状态机实现 | QHsm 风格手写状态处理函数（`Q_STATE_CAST`/`Q_TRAN`/`Q_SUPER`，`QActive_ctor` 构造，与 `examples/arm-cm/blinky` 同款用法）；转换动作次序以本版本 `qep_hsm.c` 实现为准（处理函数内动作先于框架执行的 exit/entry 链） |
| 执行模型 | QV 协作式内核（`board/ports/qp/qv_port.c`）；AO 之间不抢占，ISR 可抢占 RTC；单栈（主栈）；临界区经 `board_critical_enter/exit` 与 QP 共享嵌套计数 |
| 目标平台 | AT32F421C8T7，120 MHz，60 KB ROM / 16 KB SRAM；armclang -O1 + microlib（-O0 下 ROM 超预算）；SysTick 1 ms（已接 QP tick：`board.c` SysTick_Handler） |
| QF 能力 | 动态事件 + 2 个事件池、发布—订阅（`QF_psInit`）、时间事件（`QTimeEvt_ctorX/armX`）、每 AO FIFO 队列；无 defer/recall 需求 |
| 构建和测试 | EIDE（uid `2b2c107904df4cdb1e97e57d6947db29`）；断言统一 `Q_ASSERT`/`Q_onError`（RTT 直写）；观测用 EasyLogger/RTT + `QActive_getQueueMin()` 低水位接口；纯逻辑模块配宿主 `CHECK` 测试 |
| 项目惯例 | `../library` 驱动均提供非阻塞"延迟出参"接口（`analog_input_*`、`sig24131_*`、`tmp75_*`）；board 层已构造好全部设备对象；`modbus_slave` 契约要求应用把 `on_frame_received` 接到 `modbus_slave_advance()` |
| 假设与待确认 | ① 8 通道全开 100 Hz 时的 CPU 预算依赖 -O1 下浮点换算实测（§7，若超预算需限总量化程）；② Modbus 从站地址经保持寄存器 128 可写（1..247）并固化 flash（KV `dev/addr`，默认 1），波特率可配置列为扩展（§5） |

### 已定设计决策（本次确认）

1. **先检后测**：热电偶/PT100 通道在上电或配置变更后，先执行一次断线检测，确认传感器完好
   才输出首个测量值。理由：热电偶断线时 burnout 电流把读数顶到一个"看起来合理"的高温值，
   3 s 周期揭示之前上位机无法区分真假；代价只是首值延迟 4 个转换周期（10 Hz 约 0.4 s）。
2. **寄存器数值编码 float32**：IEEE-754 单精度、2 个寄存器、大端字序（高字在低地址）。
   测量值与线性上下限/偏移/增益全部无损，与内部浮点管线一致。
3. **FlashDB 完全托管持久化**，但"参数无变化不写"的检查保留在 `param` 组件（FlashDB
   `set_kv()` 不比较新旧值，见 `param.md` §2）；固化在写回调内**同步**执行，失败
   整笔拒绝（EX 04）。
4. **固化方案复审结论**：异步 V2（先生效+仅报 b1）因"掉电静默回退"淘汰；异步 V1
   （固化后生效+失败回退）守住"运行=已保存"但破坏"应答即提交"语义且需代次号处理并发
   写竞态；维持同步固化，改换条件见 `param.md` §4 选型记录。
5. **写后读回校验（设计内）**：`param_save_*` 固化后 `fdb_kv_get_blob` 读回比对，
   **EX 00 ⟺ 读回=下发值**——应答以 flash 实际状态为准，收口"EX 04 但已固化"分歧角
   （两条具体路径与残余窗口见 `param.md` §4）。
6. **全片擦除不区分**：分区全空当作崭新空片、以出厂配置运行（默认表物化），不报故障；
   运维约束 = 校准参数上位机留档 + 禁全片擦除（README）。
7. **定时器竞态结构规避（方案 B）**：改配经 Unconfigured 隔离 + `APPLY_SIG` 延迟一代
   重入，定时器迟到事件经 FIFO 必落无人认领态——零守卫、零旗标、零计数
   （定理与纪律见 `channel_ao.md` §6）；PSiCC2 §7.7.3 `QTimeEvt_wasDisarmed` 守卫
   弃用（锁存覆盖/boot 陷阱，失败模式为静默挂死）。

## 2. 需求与可度量约束

| 类别 | 要求 |
| --- | --- |
| 外部输入 | Modbus RTU 帧（9600 8N1，USART1 ISR）；TMP75 温度（软 I2C）；8 通道 SIG24131 采样（SPI1） |
| 输出 | Modbus 应答（TX ISR）；测量/故障遥测（输入寄存器）；配置固化（片内 Flash，4 KB 分区）；RTT 日志 |
| 周期/响应 | 每通道采样 2.5~100 Hz（13 档枚举 `rate_code_t`）；冷端 1 Hz；断线检测窗口 3 s + 4 拍切换空窗；Modbus 应答延迟 ≤ 一个最长他人 RTC（目标 ≤ 40 ms） |
| 最坏突发 | 启动：≤8×`CONFIG_SET_SIG` 发布（每订阅者队列 8 深）；写帧限单通道块（1 帧 1 条配置事件）；测量发布 8 通道 × 100 Hz = 800 事件/s |
| 最长 RTC 目标 | 通道采样步 ≤ 1 ms；冷端步 ≤ 3 ms；Modbus 读帧步 ≤ 0.5 ms；Modbus 写帧（含同步固化）≤ 30 ms（Flash GC 最坏） |
| RAM 预算 | 新增 ≤ 6 KB（现占用 4.2/16 KB，目标总占用 ≤ 10 KB）；ROM 余量按 30.8/60 KB 现状 |
| 故障与安全 | 设备故障位域与各通道状态实时上报；加载失败不下发不顶替（b2）；固化失败整笔拒绝（EX 04 + b1）；断言 `Q_onError` 直写 RTT（ISR 安全） |
| 启动/停止 | 裸机持续运行，无正常停止路径；断言 = fail-safe 停机留现场（与 QP 端口约定一致） |

### 场景清单

| 场景 | 初始条件 | 激励 | 预期状态/动作/输出 | 时序与资源验收 |
| --- | --- | --- | --- | --- |
| 上电建链 | 冷启动，flash 已有配置 | `QF_run` | AoModbus 启动加载→各加载成功通道 `CONFIG_SET_SIG`→先检后测→测量发布 | 首个有效值 ≤ 4 拍 + 1 采样周期 |
| 出厂首启 | 空 flash | 上电 | 默认表物化→加载有效→无配置故障位；通道按默认配置先检后测 | 无码 2、b2 不置位 |
| 正常测量 | 通道 Measuring | `SAMPLE_SIG` 周期到 | 读样→换算→校准→`MEASURE_SIG` | 固定周期零抖动（表定 tick 数，速率恒 ≤ 标称） |
| 断线检测循环 | TC/PT100 Measuring 3 s | `BREAK_WINDOW_SIG` | 进 Detecting（值冻结、状态维持）→检查→回 Settling→Measuring | 每 3 s 一次，空窗 = 4 转换拍 |
| 上位机改配 | 任意 | FC16 写通道 3（单块） | 校验→同步固化→发布→通道隔离重入（Unconfigured→`APPLY_SIG`→先检后测）→回读一致 | 应答含固化 ≤ 30 ms |
| 地址变更 | 任意 | FC06 写寄存器 128 | 校验→同步固化→镜像生效；本帧应答收尾，新地址对后续帧生效 | 掉电后新地址保持；旧地址静默 |
| 参数固化失败 | 固化时 Flash 异常 | `fdb_kv_set_blob` 失败 | EX 04 整笔拒绝（配置不生效）+b1 锁存；**无兜底**（无自动重试/补写） | 操作人员重写寄存器即重试，成功清 b1 |
| 参数加载失败 | 某通道 KV 损坏/缺失 | 上电加载 | 该通道不投入运行（状态 1）+b2；其余通道正常启动 | 绝不默认顶替 |
| 参数库初始化失败 | 分区损坏 | `fdb_kvdb_init` 失败 | b3（复位前不清）；无通道启动；Modbus 按默认地址运行 | 产品报废级；上位机可读到故障 |
| 冷端失效 | TMP75 脱落/温度越界 | 连续 3 次读失败或越界（-30..+85 之外） | CJC 发布 invalid→TC 通道状态 5（值冻结）→设备故障位 b0 置位 | 1 s 内揭示；恢复后自愈 |
| 重复/非法写 | 任意 | 非法枚举/NaN/未支持速率 | 整帧回 ILLEGAL_DATA_VALUE(03)，无部分生效 | 镜像与 flash 不变 |
| 超时竞争 | Detecting 中 | `CONFIG_SET_SIG` 到达 | 改配隔离重入（迟到 SETTLE 落 Unconfigured 被忽略）→重新进入先检后测 | 无提前检查、无迟到事件达处理态 |
| 启动竞态 | 启动序列 | AoModbus 启动加载发布先于通道启动 | 不允许发生：启动顺序保证订阅者先于发布者 | `QActive_post_` 断言兜底 |
| 最大合法突发 | 8×100 Hz + 轮询 + 连续写配置 | 压测 1 h | 队列低水位 ≥ 余量，池无耗尽 | `getQueueMin` 证据 |

## 3. 活动对象划分与职责

```text
                    ┌──────────────────────┐
   USART1/TMR14 ─── │ AoModbus             │ 寄存器映像 / 设备故障位域
   (板级端口)        │ prio 11              │◄──── MEASURE_SIG ────┐
                    │  ┌────────────────┐  │◄──── CJC_SIG ────┐   │
   fdb_kvdb/FAL ─── │  │ param 组件(同步)│  │          ┌──────┴───┴───┐
                    │  └────────────────┘  │          │ AoColdJunction│ TMP75+软I2C
                    └──────────┬───────────┘          │    prio 10    │
              发布 CONFIG_SET_SIG                     └───────────────┘
                    ┌────────────┐ ×8  SIG24131[ch] + 前端开关
   SPI1(共享) ───── │ AoChannel  │ prio 2..9
                    └────────────┘
```

| 对象 | 职责/不变量 | 独占资源 | 状态机 | 输入事件 | 输出事件 | 优先级 | 最长 RTC | 选择理由 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `AoChannel` ×8 | 该通道采集全生命周期：配置应用、采样换算、校准、断线检测循环、通道故障状态；不变量：测量/断线硬件模式互斥，任一时刻其一 | 本通道 `analog_input_board_t`（含 SIG24131 片选与前端开关） | QHsm | `CONFIG_SET_SIG` `CJC_SIG` `SAMPLE_SIG` `SETTLE_SIG` `BREAK_WINDOW_SIG` | `MEASURE_SIG` | 2..9（ch1..ch8） | ≤ 1 ms | 8 路互相独立、各有速率/模式/故障生命周期，各自队列+定时器 |
| `AoColdJunction` | 冷端温度供给与有效性；不变量：CJC_SIG 每 1 s 心跳（含失败时的 invalid） | TMP75 + `i2c_soft` 总线 | QHsm | `TICK_SIG` | `CJC_SIG` | 10 | ≤ 3 ms | 整板唯一 TMP75/唯一 I2C 主；多 TC 通道共享一个冷端源 |
| `AoModbus` | 协议终结、遥测/镜像的 QP 路由与发布、参数固化调度（经 `param` 组件）；不变量：镜像=生效配置+各事件最近状态（镜像与写事务由 `modbus_image` 组件承载，见下表）；写帧=生效+固化一体 | `modbus_slave` 实例（USART1/TMR14 经板级端口）、`modbus_image` 组件、`param` 组件（`fdb_kvdb`/FAL） | QHsm | `FRAME_SIG/TX_DONE/TX_TIMEOUT` `MEASURE_SIG` `CJC_SIG` | `CONFIG_SET_SIG` | 11 | ≤ 30 ms（写帧含同步固化）/ ≤ 0.5 ms（其余） | 帧事件来自 ISR，需独立队列吸收；EX 04 语义要求固化在写回调返回前完成 |

### 不建独立活动对象的候选

| 状态机/行为 | 所属执行上下文 | 不独立成 AO 的理由 | 初始化/分派/通知责任 |
| --- | --- | --- | --- |
| 寄存器编解码（`reg_map`） | `AoModbus` 回调内联调用 | 纯函数无状态 | `app/component/src/reg_map.c`（设计见 `docs/component/reg_map.md`），宿主 `CHECK` 测试 |
| 配置校验/归一化（`ch_config`） | Modbus 写回调、param 启动加载 | 纯函数无状态 | `app/component/src/ch_config.c`，宿主 `CHECK` 测试 |
| 类型化镜像与写事务（`modbus_image`） | `AoModbus` 执行上下文（modbus 核心以镜像为 ctx 直调四个数据空间回调） | 无自主行为/独立时限；写事务的原子性纪律（校验→固化→镜像→通知）与数据空间读写同体 | `app/component/src/modbus_image.c`（设计见 `docs/component/modbus_image.md`），宿主 `CHECK` 测试直接驱动回调 |
| 设备故障位域合成 | `modbus_image` 内部 | 是寄存器映像的一部分 | `modbus_image` 随各事件与锁存更新 |
| 参数持久化（`param` 组件） | `AoModbus` 执行上下文 | 无自主行为/独立时限；EX 04 语义要求固化在写回调返回前完成 | `AoModbus` 初始转换与 `modbus_image` 写事务直接调用 |
| flash 掉电恢复 | `param`（AoModbus 初始转换） | FlashDB 内部机制 | `fdb_kvdb_init()` |

纯函数组件（`ch_config`/`reg_map`/`modbus_image`/`param`）**不使用 QP 工具宏**（保持宿主测试零 QP 依赖）；
`QF_MPOOL_EL`/`QEVT_INITIALIZER`/`Q_ACTIVE_UPCAST` 等只用于 AO 侧代码。配置提交经
`modbus_image` 的普通函数指针通知器回流 `AoModbus`，由 AO 折算成 `CONFIG_SET_SIG` 发布。

## 4. 事件契约总表

| 信号/事件类型 | 参数布局 | 生产者 → 消费者 | 路由 | 静态/动态与存储 | 所有权/回收 | 顺序/过期 | 最坏产生率与突发 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `FRAME_SIG` | 无参 | TMR14/USART ISR（`on_frame_received`）→ `AoModbus` | 直接 FIFO | 静态（`QEVT_INITIALIZER`） | 无动态寿命 | 每帧一次；`advance()` 返回 NONE 时显式忽略 | ≤1 在途（核心在 CAPTURED 期丢弃字节，天然互斥） |
| `TX_DONE_SIG` / `TX_TIMEOUT_SIG` | 无参 | TX ISR → `AoModbus` | 直接 FIFO | 静态 | — | 仅诊断计数 | ≤1 在途 |
| `SAMPLE_SIG` | 无参 | 时间事件（周期） | 自投 FIFO | 静态 `QTimeEvt`/通道 | — | 固定周期（表定 tick 数）；无须陈旧防护（见 channel_ao §6） | 100 Hz/通道 |
| `SETTLE_SIG` | 无参 | 时间事件（one-shot） | 自投 FIFO | 静态 `QTimeEvt`/通道 | — | 语义随状态（检查就绪/转换就绪）；迟到事件结构上不可达（channel_ao §6） | ≤1 次/模式切换 |
| `BREAK_WINDOW_SIG` | 无参 | 时间事件（周期 3000 ms） | 自投 FIFO | 静态 `QTimeEvt`/通道 | — | 仅 Measuring 有效，他态显式忽略 | 1/3 s/通道 |
| `CONFIG_SET_SIG` | `{uint8_t ch; ch_config_t cfg}`（24 B） | `AoModbus`（启动加载 / 写帧固化成功）→ 8×`AoChannel` | **发布—订阅** FIFO | 动态 池B（32 B 块） | 发布后生产者不得再访问；框架按引用计数回收 | 仅固化成功后发布；接收者按 `ch` 过滤；命中→改配隔离（`pending`+`APPLY_SIG`+转 Unconfigured） | 启动 ≤8 条；写帧 1 条 |
| `APPLY_SIG` | 无 | `applyEvt` 自投（文件级 `QEvt const`，8 通道共用） | 自投 FIFO | 静态 `QEVT_INITIALIZER` | — | Unconfigured 处理：`configure(pending)`→先检后测重入；改配隔离用（channel_ao §6） | 每次改配 1 条 |
| `MEASURE_SIG` | `{uint8_t ch; float value; uint16_t status}`（12 B） | `AoChannel` → `AoModbus` | 发布—订阅 FIFO | 动态 池A（20 B 块） | 同上 | 只保留最新；Modbus 映像覆盖写 | 8×100 Hz=800/s |
| `CJC_SIG` | `{float temp_c; uint8_t valid}` | `AoColdJunction` → 8×`AoChannel`、`AoModbus` | 发布—订阅 FIFO | 动态 池A | 同上 | 心跳 1 Hz；通道缓存最近一条 | 1 Hz + 启动 1 条 |

### 所有权补充

- 动态事件（`CONFIG_SET_SIG`/`MEASURE_SIG`/`CJC_SIG`）由生产者 `Q_NEW`
  创建并在移交前完成全部字段写入；`QACTIVE_PUBLISH`/`QACTIVE_POST` 之后生产者不得再访问。
- 发布无订阅者时框架自动回收：通道状态 1 首拍不发布（由 ctor 镜像初值表达）；启动序
  channels→modbus→cold_junction 使 CJC 首拍晚于全部订阅者。
- 当前事件在 RTC 内只读；通道需要长期保留 CJC 时复制 `temp_c/valid` 到扩展变量，
  不保存事件指针。
- 不使用 defer/recall；配置写不排队积压，`param` 的差异检查吸收重复。
- 静态时间事件各通道 3 个实例（`te_sample/te_settle/te_break`），构造一次信号不变；
  另每通道 1 个静态 `APPLY_SIG` 事件（改配善后，channel_ao §6）。

## 5. 数据与配置模型

### 通道配置 `ch_config_t`（`app/component/inc/ch_config.h`）

| 字段 | 类型 | 语义 | 校验/归一化 |
| --- | --- | --- | --- |
| `type` | `uint8_t` | 0=TC 1=PT100 2=4-20 mA 3=0-10 V | ∈{0..3} |
| `tc_type` | `uint8_t` | 0=B 1=E 2=J 3=K 4=N 5=R 6=S 7=T | TC 时 ∈{0..7}；非 TC 归一为 0xFF |
| `rate_code` | `uint16_t` | 采样速率枚举码（`rate_code_t`，0..12，2=10 Hz 出厂默认） | ∈ 0..12；码→mHz 查表（与 `ANALOG_INPUT_BOARD_RATE_*` 同源）供驱动配置，码→tick 查表供采样节拍 |
| `linear_min` / `linear_max` | `float` | 线性输入上下限工程值（仅 2/3 型） | 有限浮点；非线性型归一为 0.0 |
| `offset` / `gain` | `float` | 校准：`value = raw × gain + offset` | 有限浮点 |

`ch_config_validate()` 同时承担**校验**与**归一化**（Modbus 写回调与 param 启动加载共用），
归一化保证同义配置字节一致，变化检查（memcmp）不会因无意义字段抖动误判。

寄存器层的输入类型与分度号**合成单码**（0..7=热电偶 B/E/J/K/N/R/S/T，8=PT100，
9=4-20 mA，10=0-10 V），与 struct 字段的映射由 `reg_map` 编解码：`code<8` →
`type=TC, tc_type=code`；8/9/10 → `type=PT100/4-20MA/0-10V, tc_type=0xFF`。

设备级配置：`dev_addr`（u16，1..247，KV `dev/addr`，出厂默认 1）；寄存器位置与
写入语义见 `modbus_ao.md` §4，固化路径与通道配置相同。

### 持久化格式（代码内不管理版本）

KV 键 `cfg/ch1`..`cfg/ch8`，值 = `ch_config_t` 本体（20 B，无版本头）；`dev/addr`，值 =
`u16` 地址（2 B）。加载侧唯一防线是 `ch_config_validate()` 语义校验（拒绝即加载失败：
不顶替、不下发、置 b2）。固件出厂即固定、无在线升级，KV 布局仅在开发期演进（全片擦除/
恢复出厂/重下发即足），不做版本管理。详细策略见 `param.md`。

出厂默认参数由 FlashDB 默认表承载（`param_defaults.c` 的 `fdb_kv_pair_t` 表，含
`cfg/ch1..8` 与 `dev/addr`）：空 flash 首启由 FlashDB 物化全部默认键、不报故障；
`FDB_KV_AUTO_UPDATE` 保持关闭，已用 DB 上缺失/损坏键如实报码 2 且**不下发**
（对应通道不投入运行，绝不默认顶替；仅 `dev/addr` 允许默认地址兜底）；
恢复出厂 = `fdb_kv_set_default()`。

### 寄存器映射

完整映射（保持寄存器 = 通道配置 8×10 分块按 16 步进分隔 + 设备级 128，块间空档回 02；
输入寄存器 0..71 遥测区连续、16 bit 设备故障位域）定义在 `docs/ao/modbus_ao.md` §4，本文不重复。数值编码：float32
占 2 寄存器大端字序，u16 单寄存器。

## 6. 定时与过期（总表）

| 时间事件 | 所有者 | 一次性/周期 | arm 时点 | disarm 路径 | 陈旧识别 | 并行实例 |
| --- | --- | --- | --- | --- | --- | --- |
| `te_sample` | AoChannel[ch] | 周期（表定 tick 数） | Measuring entry | Measuring exit；CONFIG_SET 重配 | 无须（信号仅 Measuring 处理） | 8 |
| `te_settle` | AoChannel[ch] | one-shot | Detecting/Settling entry | 两态 exit；重配 | 无须（方案 B 结构规避，channel_ao §6） | 8 |
| `te_break` | AoChannel[ch] | 周期 3000 ms | Measuring entry（可检测型） | Measuring exit；重配 | 无须（同 te_sample） | 8 |
| `te_tick` | AoColdJunction | 周期 1000 ms | Starting entry | 无（永续） | 无切换场景 | 1 |

tick 分辨率 1 ms（SysTick 1 kHz）；等待 0 tick 至少 arm 1 拍（≥1 tick 原样 arm，`settle_ms` 向上取整）；
非整毫秒采样周期向上取整为固定 tick 数（速率恒 ≤ 标称，误差表见 `channel_ao.md` §6）。

## 7. 并发与容量预算（总表）

QV 协作内核：AO 之间不抢占，故任一 AO 的响应窗口 W = 他人最长 RTC + 在途积压。
最长 RTC = `AoModbus` 写回调同步固化（≤30 ms），据此核算最坏突发：

| 队列 | 深度 | 依据（30 ms 窗口最坏） | RAM |
| --- | --- | --- | --- |
| AoChannel[ch] ×8 | 13 | 启动 8×CONFIG_SET + 1×CJC + ≤3 tick + 1×APPLY | 416 B |
| AoColdJunction | 4 | 1 Hz tick + 余量 | 16 B |
| AoModbus | 16 | QV 协作 + 本 AO 最高优先级：写回调 30 ms 内他人不运行、无发布积压；峰值 ≈ 一轮发布（≤8×MEASURE+1×CJC）+1×FRAME ≈ 10 + 余量 | 64 B |

| 事件池 | 块尺寸 | 块数 | 依据 | RAM |
| --- | --- | --- | --- | --- |
| 池A（MEASURE/CJC） | 20 B | 16 | 块=`QF_MPOOL_EL(measure_evt_t)` 元素（=载荷结构尺寸）；在途峰值 ≈ 一轮发布 8×MEASURE+1×CJC + 裕量 | 320 B |
| 池B（CONFIG_SET） | 32 B | 16 | 块=`QF_MPOOL_EL(config_set_evt_t)` 元素；启动 ≤8 的实例数 | 512 B |

| 其他 | 预算 | 备注 |
| --- | --- | --- |
| QTimeEvt 25 个 | ~800 B | 8×3 + CJC 1 |
| 静态 QEvt const 4 个 | 32 B（ROM） | `APPLY_SIG` 8 通道共用 + Modbus 3 个（`QEVT_INITIALIZER`） |
| AO 结构/扩展变量 | ~1.2 KB | 含每通道配置与缓存 |
| Modbus RX/TX 缓冲 | 512 B | 板级已有 |
| FDB 缓存 | 1.5~2 KB | 默认 `FDB_KV_CACHE_TABLE_SIZE=64`，可裁至 16 |
| **合计新增** | **≈ 4.5~5 KB** | 目标总占用 ≤ 10/16 KB |

CPU 预算（**待实测**，-O1 口径）：通道采样步 = SPI 读（≈0.3 ms）+ 浮点换算（≈0.5 ms 估算）
≈ 0.8 ms；8 通道全开 100 Hz ≈ 640 ms/s ≈ 64% CPU。若实测超预算，处置顺序：
① 按产品规格限制 Σ 采样率；② `analog_input_math.c` 单独 -O1；③ 降低最大档位。
不得以丢样本维持"稳定"。低水位闭环用 `QActive_getQueueMin()` + 压测取证。

## 8. 启动、停止与故障安全

### 启动序列（`app/main.c` 组合根）

```c
log_init();                       /* RTT + EasyLogger，必须最先 */
board_init();                     /* 时钟/外设/SIG24131/TMP75/analog_input 构造 */
QF_init();
QF_psInit(subscrSto, MAX_SIG);    /* 发布订阅存储 */
QF_poolInit(...);                 /* 池A、池B */
/* 构造 10 个 AO（QActive_ctor）与 25 个 QTimeEvt_ctorX */
QActive_start(ch1..ch8);          /* 优先级 2..9 */
QActive_start(modbus);            /* 11：初始转换内 param_boot_load() 并发布启动配置 */
QActive_start(cold_junction);     /* 10：首拍 CJC 晚于全部订阅者 */
QF_run();                         /* QV 调度器接管 */
```

约束：

1. **订阅者先于发布者启动**：`AoModbus` 的初始转换（`QActive_start` 内同步执行
   `param_boot_load()`，约数十 ms，发生在 `QF_run()` 之前）会发布启动 `CONFIG_SET_SIG`，
   因此 8 个通道必须先 `QActive_start`（订阅在各 AO 的状态机初始转换内完成）。
   向未启动对象投递由 `QActive_post_` 断言兜底。
2. `QActive_start` 的 prioSpec 用 `Q_PRIO(prio, 0U)`（QV 无抢占阈值）；QV 全部 AO 共享
   主栈（`stkSto=NULL, stkSize=0`）。
3. **板级 tick 接入（已实现）**：`board/src/board.c` 的 `SysTick_Handler()` 调用
   `QTIMEEVT_TICK_X(0U, (void*)0);`（本版本 ISR 侧 tick API）。
4. `modbus_slave_board_ctor()` 由组合根传入 callbacks/identity；`modbus_slave_start()`
   在 `AoModbus` Serving entry 执行。

### 停止与故障安全

- 裸机产品无正常停止路径；`QACTIVE_CAN_STOP` 不启用。
- 断言（bug 级）：`Q_onError` 直写 RTT 留现场后停机；FlashDB/EasyLogger 断言已桥接。
- 合法外部异常（Flash 写失败、TMP75 失效、SIG24131 IO 错误、断线）走状态机恢复路径，
  不断言，见各 AO 文档"故障路径"。
- 掉电安全：FlashDB 分阶段状态 + CRC + 上电恢复（见 `param.md` §2），配置写任意时刻
  断电只可能得到旧值或完整新值。

## 9. 可观测性

| 问题/假设 | 手段 | 预期结论 |
| --- | --- | --- |
| 通道状态机轨迹 | `elog` 在 entry/转换点打状态名与关键出参（限 DEBUG 级） | 核对先检后测、3 s 循环 |
| RTC 长度 | GPIO 翻转 + 逻辑分析仪，或 DWT 循环计数打日志 | 收紧 §7 预算 |
| 队列/池余量 | `QActive_getQueueMin()` 周期上报（DEBUG 构建） | 低水位证据 |
| 配置固化 | param 每次固化打 key/结果 | 掉电测试对账 |
| 异常现场 | `Q_onError` RTT 直写 | ISR 可用 |

不使用 QS/QSpy；DEBUG 与 RELEASE 对照构建评估日志开销（RTT NO_BLOCK_SKIP 整行丢弃）。

## 10. 验证总纲

| 测试 | 对象 | 预期 | 证据 |
| --- | --- | --- | --- |
| 纯逻辑宿主测试 | `ch_config`/`reg_map`/设备故障位域/通道状态码/采样周期表 | `N checks, 0 failures` | gcc 宿主可执行 |
| 启动瞬态 | 全系统 | 8 通道先检后测、首值时延达标 | RTT 轨迹 |
| 断线检测时序 | AoChannel | 3 s 周期、空窗 4 拍、检测期间状态维持→断线揭示（TC 2 / PT100 3、4） | 波形/日志 |
| 配置写全路径 | Modbus→param→Channel | EX 00=生效+固化；固化失败 EX 04 整笔拒绝；非法写 EX 03；跨块写 EX 02 | 上位机脚本 |
| 掉电测试 | param | 写入瞬间反复断电：旧值或新值+恢复日志 | ≥100 次循环 |
| 压测/长稳 | 全系统 | 8×100 Hz + 轮询 + 连续写 1 h，队列池无溢出 | 低水位日志 |
| 冷端失效注入 | CJC + TC 通道 | 1 s 内 invalid→状态 5→设备故障位 b0；恢复自愈 | 拔线实验 |
| Modbus 时序 | AoModbus | 读帧 ≤ 1 ms、写帧（含同步固化）≤ 30 ms | 上位机抓包 |

各 AO 的专属验证矩阵见对应文档。
