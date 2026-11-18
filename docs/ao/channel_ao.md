# AoChannel — 通道采集活动对象（×8 实例）

单类 8 实例（ch1..ch8），每实例独占一路 SIG24131（SPI1 共享、独立片选）与该路前端开关。
总体设计见 `docs/qp-application-design.md`。

## 1. 基线与范围

| 项目 | 内容 |
| --- | --- |
| 需求 | 按配置（TC/PT100/4-20 mA/0-10 V）周期采样并输出校准后工程量；TC/PT100 断线检测与测量互斥、3 s 周期切换；**先检后测**（上电/改配后先断线检查再出首值）；上报通道状态 |
| 状态机 | QHsm 手写（`QStateHandler` + `Q_TRAN`/`Q_SUPER`），一状态一处理函数 |
| 独占资源 | `analog_input_t` 实例（构造注入，组合根经 `analog_input_board_get(ch)` 接线）：SIG24131 配置/读数、CV/COM 前端开关、IDAC |
| 依赖接口 | `analog_input.h` 六操作：`configure / read_sample / convert / enable_break_detection / disable_break_detection / check_wire_break`（全部非阻塞，延迟经出参返回） |
| 扩展变量 | `cfg`（生效配置）、`cjc`（冷端缓存）、`wire`（断线结果）、`last_value`、`settle_ms` |
| 假设 | `convert()` 需要的冷端/线性上下文由本对象按 `cfg` 组装；偏移/增益校准在 `convert()` 之后由本对象应用（不进驱动） |

## 2. 职责与不变量

1. **模式互斥不变量**：硬件任一时刻只处于"测量模式"或"断线检测模式"；`read_sample/convert`
   仅在测量模式合法，`check_wire_break` 仅在检测模式合法（接口契约）。
2. **配置所有权**：`cfg` 是生效配置的唯一副本；变更只能经 `CONFIG_SET_SIG` 进入，
   经 `pending` + `APPLY_SIG` 隔离重入（改配不变量见 §6）。
3. **校准语义**：`value = raw × gain + offset`，`raw` 为 `convert()` 输出的工程量
   （TC 已含冷端补偿；线性型已按 `linear_min/max` 映射）。
4. **测量值有效性**：TC 通道在冷端无效期间不输出换算值（状态 5、值冻结）；
   断线检测期间值冻结、状态维持上一结果（无"检测中"过渡码，断线在检查完成时揭示）。
5. 所有等待（稳定拍、窗口、采样周期）都是状态 + 时间事件，无忙等无阻塞。
6. **改配隔离不变量**：改配不同 dispatch 重入——`CONFIG_SET_SIG` 只缓存 `pending` 并投递
   `APPLY_SIG`、转 Unconfigured，真正 `configure()` 在下一 dispatch 才执行；定时器迟到
   事件经 FIFO 必落 Unconfigured 被默认忽略（零守卫定理与纪律见 §6）。

## 3. 事件契约（本 AO 相关行）

| 信号 | 参数 | 来源 → 本 AO | 路由/存储 | 处理与过期 |
| --- | --- | --- | --- | --- |
| `CONFIG_SET_SIG` | `{ch, cfg}`（`ch` 0 基，与镜像/KV 同基；构造入参 `ch` 亦为 0 基索引） | 发布（AoModbus：启动加载 / 写帧固化成功） | 池B 动态 | `ch != me` 显式忽略；命中→改配隔离（`pending=cfg` + post `APPLY_SIG` + 转 Unconfigured，§6） |
| `APPLY_SIG` | 无 | `applyEvt` 静态自投（改配善后） | 静态 | 仅 Unconfigured 响应：`configure(pending)`→重入先检后测；该态唯一出边 |
| `CJC_SIG` | `{temp_c, valid}` | 发布（ColdJunction 1 Hz） | 池A 动态 | 复制进 `cjc` 缓存；不保存事件指针 |
| `SAMPLE_SIG` | 无 | `te_sample` 自投 | 静态 | 仅 Measuring 响应；采样一步 |
| `SETTLE_SIG` | 无 | `te_settle` 自投 | 静态 | Detecting=检查就绪；Settling=转换就绪；迟到事件结构上不可达（§6） |
| `BREAK_WINDOW_SIG` | 无 | `te_break` 自投 | 静态 | 仅 Measuring 响应→Detecting；他态默认忽略 |
| 输出 `MEASURE_SIG` | `{ch, value, status}`（`ch` 0 基） | 本 AO 发布 | 池A 动态 | 每次采样一条（含冻结分支）；断线检查发现断线即时揭示一条 |

通道状态码（`status`，u16，与寄存器映射一致）：

| 码 | 含义 | 触发 |
| --- | --- | --- |
| 0 | 正常 | 有效测量 |
| 1 | 未配置（无生效配置） | AoModbus 镜像初值表达（含 flash 加载失败），等待上位机下发；重配隔离途经不发布（状态维持，无闪烁） |
| 2 | 热电偶断线 | TC 通道断线检查发现回路开路（线位不可分辨） |
| 3 | PT100 激励线断 | PT100 通道 `ANALOG_INPUT_WIRE_BROKEN_LEAD` |
| 4 | PT100 回流线断 | PT100 通道 `ANALOG_INPUT_WIRE_BROKEN_RETURN` |
| 5 | 冷端无效（TC） | `!cjc.valid` 时的采样拍 |
| 6 | 硬件/IO 错误 | `configure/read_sample` 返回 ERR_IO/ERR_STATE |
| 7 | 转换超量程 | `convert` 返回 ERR_CONVERSION |

断线码按**通道类型**解释：TC 通道任一断线结果一律报 2（热电偶回路开路，两根线位不可
分辨）；PT100 按线位报 3/4；PT100 收到不可分辨的 `ANALOG_INPUT_WIRE_BROKEN`
（本板 SIG24131 前端不产生）视为前端契约违约，报 6。断线在检查完成时揭示、采样拍
持续维持；**无"断线检测中"过渡码**，检测期间状态维持上一结果。

## 4. 状态机

### 状态层次

```text
AoChannel (top, 初始 → Unconfigured)
├── Unconfigured
└── Working
    ├── Detecting    （断线检测硬件模式）
    ├── Settling     （测量硬件模式，转换稳定）
    └── Measuring    （测量硬件模式，周期采样）
```

### 状态定义

| 状态 | 定性含义/合法事件 | entry | exit | initial/超状态不变量 |
| --- | --- | --- | --- | --- |
| `Unconfigured` | 无生效配置/改配隔离中，不碰硬件测量路径；唯一出边 `APPLY_SIG` | disarm 全部定时器（无条件、返回值忽略）；**不发布状态**（镜像维持——改配隔离无 1 码闪烁） | — | Working 不变量：`cfg` 已被 `analog_input_configure()` 接受 |
| `Working` | 配置生效；`CJC_SIG`/`CONFIG_SET_SIG` 公共处理 | — | — | initial 目标由配置类型决定（`APPLY_SIG` 重入路径） |
| `Detecting` | 硬件在断线检测模式，值冻结、状态维持 | `enable_break_detection(&d)`→arm `te_settle(d)`（不发布） | disarm `te_settle` | `check_wire_break` 只在此态合法 |
| `Settling` | 硬件在测量模式，等首个有效转换 | arm `te_settle(settle_ms)` | disarm `te_settle` | `settle_ms` 由来源转换写入 |
| `Measuring` | 周期采样循环 | arm `te_sample`（周期 tick 数，表定）；可检测型 arm `te_break(3000)` | disarm `te_sample`、`te_break` | `read_sample/convert` 只在此态合法 |

### 事件—配置—行为矩阵

| 当前叶状态 | 事件 | 守卫 | 类型 | 动作 | 目标 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| 任意 | `CONFIG_SET_SIG` | `ch!=me` | 显式忽略 | — | 不变 | 过滤订阅噪声 |
| 任意（Working 叶） | `CONFIG_SET_SIG` | `ch==me` | 转换 | `pending=cfg`；post `APPLY_SIG` | Unconfigured | **改配隔离**（§6）；exit 叶=撤防+退出硬件模式，entry 不发布（状态维持） |
| Unconfigured | `CONFIG_SET_SIG` | `ch==me` | 内部 | `pending=cfg`；post `APPLY_SIG` | 不变 | 连续改配合并到最新 pending |
| Unconfigured | `APPLY_SIG` | configure 成功 | 转换 | `configure(pending,&d)`→`cfg=pending`、`settle_ms=d` | 可检测型→Detecting；线性型→Settling | **先检后测**重启；此刻 FIFO 已无迟到事件 |
| Unconfigured | `APPLY_SIG` | configure 失败 | 内部 | 发布状态 6 | 留 Unconfigured | 等下一次写配置 |
| 任意 | `CJC_SIG` | — | 内部 | `cjc = {temp_c, valid}` | 不变 | 缓存复制（Unconfigured 也收） |
| Detecting | `SETTLE_SIG` | — | 转换 | `check_wire_break→wire`；断线→发布状态(2/3/4 按类型)；完好→不发布（0 随首条测量值）；`disable_break_detection(&d)`→`settle_ms=d` | Settling | 断线检查完成即揭示 |
| Settling | `SETTLE_SIG` | — | 转换 | `do_sample()`（首拍有效值） | Measuring | 首拍恰在稳定点；`te_sample`/`te_break` 在 Measuring entry arm |
| Measuring | `SAMPLE_SIG` | — | 内部 | `do_sample()`（周期定时器，免再 arm） | 不变 | 采样不换态 |
| Measuring | `BREAK_WINDOW_SIG` | — | 转换 | —（模式切换在 Detecting entry） | Detecting | 每 3 s 一次 |
| Detecting/Settling | `SAMPLE_SIG`/`BREAK_WINDOW_SIG` | — | 不处理 | —（QHsm 默认忽略） | 不变 | 撤防失败漏达的迟到事件，天然无害 |
| Measuring | `SETTLE_SIG` | — | 不处理 | —（默认忽略） | 不变 | 定理保证不可达（§6），防御性忽略 |

### 复杂转换动作轨迹（外部转换，QP/C 8.1.3 QHsm 语义）

| 场景 | 实际活动叶 | 轨迹（处理函数内动作 → 框架 exit/entry） |
| --- | --- | --- |
| 改配（任何状态） | 任意叶 | 动作：`pending=cfg`、post `APPLY_SIG` → `Q_TRAN(Unconfigured)` → exit 旧叶（撤防本叶定时器+退出硬件模式）→ entry Unconfigured（仅撤防，不发布——状态维持） |
| 改配善后（下一 dispatch） | Unconfigured | 动作：`configure(pending)` 失败→发布状态 6 留态；成功→写 `settle_ms` → `Q_TRAN(Detecting/Settling)` → entry（模式切换+arm）——此刻 FIFO 已无迟到事件 |
| 启动首配 | Unconfigured | 状态 1 由 AoModbus 镜像初值表达（含 flash 加载失败通道）；首条 `CONFIG_SET_SIG` 即改配，轨迹同"改配善后" |
| 检查完成 | Detecting | 动作：check/发布/disable/写 `settle_ms` → `Q_TRAN(Settling)` → exit Detecting（disarm）→ entry Settling（arm） |
| 稳定完成 | Settling | 动作：`do_sample()` → `Q_TRAN(Measuring)` → exit Settling → entry Measuring（arm `te_sample` 周期 + `te_break`） |
| 3 s 窗口到期 | Measuring | `Q_TRAN(Detecting)` → exit Measuring（disarm `te_sample/te_break`）→ entry Detecting（模式切换+arm） |

模型检查：初始转换目标 Unconfigured 无守卫；Detecting/Settling entry 不读触发事件参数
（延迟值经扩展变量 `settle_ms` 传递）；所有副作用在处理函数 switch 分支内；守卫互斥
（`ch==me` / configure 成败互补）。

## 5. `do_sample()` 动作（辅助函数，不含转换结构）

```text
wire != OK                            → 发布状态 2/3/4（值冻结），返回
read_sample(&code)                    失败 → 发布状态 6（值冻结），返回
TC 且 !cjc.valid                      → 发布状态 5（值冻结），返回
convert(code, ctx, &raw)              失败 → 发布状态 7（值冻结），返回
value = raw * gain + offset           （raw 为局部量，不上报）
last_* 更新；发布 MEASURE_SIG(ch, value, 0)
```

`值冻结` = 值字段填 `last_value`（尚无首值时 0），有效性以状态码为准；断线码取自最近
一次检查的 `wire`，按通道类型解释（§3）。

`ctx` 组装：TC → `cold_junction = cjc.temp_c`；线性型 → `linear_min/max = cfg.*`；
PT100 → 空上下文。

## 6. 定时设计

### 采样节拍（固定周期，向上取整）

采样周期 = 速率码表定 tick 数 `ceil(1e6/R)`（R 单位 mHz），`te_sample` 为周期定时器
（`armX(ticks, ticks)`，Measuring entry arm 一次）。13 档中 9 档周期恰为整毫秒，
仅 15/30/60/80 Hz 四档取整，代价是平均速率略低于标称：

| 档（Hz） | 2.5 | 5 | 10 | 12.5 | 15 | 20 | 25 | 30 | 40 | 50 | 60 | 80 | 100 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 周期 ms | 400 | 200 | 100 | 80 | 67 | 50 | 40 | 34 | 25 | 20 | 17 | 13 | 10 |
| 速率误差 | 0 | 0 | 0 | 0 | -0.5% | 0 | 0 | -2.0% | 0 | 0 | -2.0% | -3.8% | 0 |

固定周期零抖动、无累积量、运行期零算术（tick 数查表）；向上取整保证速率恒 ≤ 标称
（不超采、不超 CPU 预算、读速不超过 ADC 转换）。速率精度对本设备无协议后果：
Modbus 轮询最新值，无人计数采样个数。若将来需要精确速率（外部时间基准同步/按
采样数处理），再引入分数节拍（Bresenham 已验证可行：查表换累加器即可）。

### 稳定延迟

`configure/enable/disable` 返回的延迟已是整毫秒（接口向上取整），直接 arm；
返回值 0 时至少 arm 1 拍、≥1 tick 原样 arm（实现取 `armX(delay ? delay : 1, 0)`，
首拍容忍一次空读，待实测定稿）。

### 定时器竞态的结构规避（方案 B，零守卫）

竞态：改配事件与 `te_settle` 到期同队竞速时，迟到到期会落进重入后的状态被误当"就绪"
（提前 `check_wire_break` = 假断线报警，提前 `do_sample` = 违先检后测）。QP 不自动丢弃
迟到事件（`QTimeEvt_disarm` 契约："should be expected in the state machine"）。

规避机制：**改配经 Unconfigured 隔离 + `APPLY_SIG` 跨派发延迟重入**。改配转换**先撤防
三个定时器**，再"缓存 `pending` + post `APPLY_SIG` + 转 Unconfigured"（exit 撤防保留，
无锁存故无毒）。顺序不可颠倒：撤防先于 post 保证任何迟到到期都入队在 `APPLY_SIG` 之前
（撤防前已到期 = 先入队；撤防后定时器停摆 = 不可能再投）。由 FIFO 全序：迟到事件必先于
`APPLY_SIG` 派发 ⇒ 落点恒为 Unconfigured ⇒ 被默认忽略；`APPLY_SIG` 派发时队列已无
同类定时器事件，新 arm 代永不与旧事件混淆。`SAMPLE_SIG`/`BREAK_WINDOW_SIG` 的迟到事件
同理落 Unconfigured 或不处理它们的叶子。**零守卫、零旗标、零计数**，无挂死失败模式。

结构纪律（评审可机械检查，违反即定理失效）：

1. Unconfigured 不 arm 任何定时器，唯一出边（转换）是 `APPLY_SIG`；
2. 定时器 arm 只发生在 Detecting/Settling/Measuring 的 entry；
3. 叶 exit 无条件撤防自己的定时器（返回值忽略——无锁存，误调无毒）；
4. 取消转换内**先撤防、后 post** `APPLY_SIG`（FIFO 前提，顺序不可颠倒）。

不采用 PSiCC2 §7.7.3 `QTimeEvt_wasDisarmed` 守卫：该锁存有双改配覆盖、双迟到单消费、
boot 首拍与 configure 失败落点毒化等缺陷，补全需计数器，违反纪律时失败模式是静默挂死。
拆信号/乒乓叶类纯信号方案被证明不可行（迟到事件与真事件代际不可辨识，同态重入不可堵，
有限状态无法给无界代际命名）。

## 7. 容量（本 AO）

| 项目 | 计算 | 数值 |
| --- | --- | --- |
| 队列深度 | 启动 8×CONFIG_SET + 1×CJC + ≤3 tick + 1×APPLY + 1 裕量 | 13（×8 实例 = 104 槽） |
| 事件产生 | `MEASURE_SIG` 100 Hz 上限 + 状态变化 1 条 | 发布共享实体，池A 统一核算（见总体 §7） |
| QTimeEvt | 3/通道 | 24 个 |
| 静态 QEvt const（`APPLY_SIG`） | 8 通道共用 | 1 个（ROM） |
| RTC | SPI ≈0.3 ms + 浮点换算 ≈0.5 ms（-O1 估算） | ≤1 ms（**实测收口**） |

## 8. 故障路径

| 故障 | 分类 | 处理 |
| --- | --- | --- |
| 断线（wire 断线结果） | 合法异常 | TC→2、PT100→3/4（该通道状态寄存器），检查完成即揭示、采样拍维持，不自愈假设 |
| SIG24131 IO 错误 | 合法异常 | 状态 6，下拍重试；连续失败不升级（日志限频） |
| 冷端失效 | 合法异常 | TC 状态 5 值冻结，CJC 恢复后自动续测 |
| `convert` 超量程 | 合法异常 | 状态 7，值冻结，下一拍继续 |
| configure 失败 | 合法异常 | 状态 6 + 降级 Unconfigured 等待重配 |
| 队列/池耗尽 | Bug/容量缺陷 | `QACTIVE_POST_X` 带裕量断言暴露，不静默丢 |

## 9. 验证矩阵（本 AO）

| 测试 | 激励 | 预期轨迹/断言 | 证据 |
| --- | --- | --- | --- |
| 初始转换与先检后测 | 上电 TC 配置 | Unconfigured→Detecting→Settling→Measuring(状态0)，首值无假高温 | RTT 轨迹 |
| 线性通道直测 | 上电 4-20 mA 配置 | Unconfigured→Settling→Measuring | RTT 轨迹 |
| 3 s 循环 | PT100 运行 10 min | 每 3 s Detecting 往返，检测期间状态维持、完成揭示结果码，空窗 4 拍 | 波形+日志 |
| 断线注入 | 热电偶开路 / PT100 激励线、回流线分别开路 | 状态 2 / 3、4 分别揭示，≤ 3 s+4 拍 | 拔线实验 |
| 冷端失效 | TMP75 断开 | TC 状态 5、值冻结；恢复自愈 | 拔线实验 |
| 速率档位 | 逐档写速率 | 实测频率 = 表定周期倒数（恒 ≤ 标称，误差见 §6 表）；无抖动 | 脉冲计数 |
| 改配竞态 | Detecting 中写新配置（含同一窗口连写两次、背压 30 ms） | 迟到事件全落 Unconfigured 被忽略；无提前检查、无挂死 | 轨迹 + 断言 |
| 竞态穷举 | host 事件注入穷举 CONFIG/到期/背压交错（真实 dispatch + tick 注入） | 迟到事件被旧态消耗或在 Unconfigured 被忽略；新代际截止时间得到遵守；连续改配合并为一次 configure | `tests/app/test_channel_ao.c`（`tests/run_host_tests.ps1`） |
| 样本率压测 | 8×100 Hz 30 min | `do_sample` RTC 低水位、队列 `getQueueMin` ≥ 5 | 日志 |
