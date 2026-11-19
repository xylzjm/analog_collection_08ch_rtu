# AoChannel — 通道采集活动对象（×8 实例）

单类 8 实例（ch1..ch8），每实例独占一路 SIG24131（SPI1 共享、独立片选）与该路前端
开关；一个 AO 一个文件（`app/channel_ao.h/.c`）。总体设计见
`docs/qp-application-design.md`。

## 1. 基线与范围

| 项目 | 内容 |
| --- | --- |
| 需求 | 按配置（TC/PT100/4-20 mA/0-10 V）周期采样输出校准工程量；TC/PT100 断线检测与测量互斥、3 s 切换；**先检后测**（上电/改配后先断线检查再出首值）；上报通道状态 |
| 状态机 | QHsm 手写（`Q_TRAN`/`Q_SUPER`），一状态一处理函数 |
| 独占资源 | `analog_input_t`（构造注入：`channel_ao_ctor(me, ch, input)`，组合根经 `analog_input_board_get(ch)` 接线）：SIG24131 配置/读数、CV/COM 前端开关、IDAC |
| 依赖接口 | `analog_input.h` 六操作：`configure / read_sample / convert / enable_break_detection / disable_break_detection / check_wire_break`（全部非阻塞，延迟经出参返回） |
| 扩展变量 | `cfg`（生效配置）、`pending`（待应用）、`cjc` 缓存、`wire`（断线结果）、`last_value`、`settle_ms` |

## 2. 职责与不变量

1. **模式互斥不变量**：硬件任一时刻只处于"测量模式"或"断线检测模式"；`read_sample/convert`
   仅测量模式合法，`check_wire_break` 仅检测模式合法（接口契约）。
2. **配置所有权**：`cfg` 是生效配置的唯一副本，变更只能经 `CONFIG_SET_SIG` 进入，
   经 `pending` + `APPLY_SIG` 隔离重入（§6）。
3. **校准语义**：`convert()` 直接输出校准后工程量（板级换算模块内完成
   `raw × gain + offset`；TC 已含冷端补偿，线性型已按 `linear_min/max` 映射）。
4. **测量值有效性**：TC 通道在冷端无效期间不输出换算值（状态 5、值冻结）；断线检测
   期间值冻结、状态维持上一结果（**无"检测中"过渡码**，断线在检查完成时揭示）。
5. 所有等待（稳定拍、窗口、采样周期）都是状态 + 时间事件，无忙等无阻塞。
6. **改配隔离不变量**：改配不同 dispatch 重入——`CONFIG_SET_SIG` 只缓存 `pending`
   并投递 `APPLY_SIG`、转 Unconfigured，真正 `configure()` 在下一 dispatch 才执行；
   定时器迟到事件经 FIFO 必落 Unconfigured 被默认忽略（零守卫定理与纪律见 §6）。

## 3. 事件契约与通道状态码

| 信号 | 参数 | 来源 → 本 AO | 存储 | 处理与过期 |
| --- | --- | --- | --- | --- |
| `CONFIG_SET_SIG` | `{ch, cfg}`（`ch` 0 基，与镜像/KV 同基；构造入参 `ch` 亦 0 基） | 发布（AoModbus：启动加载 / 写帧提交成功） | 池B 动态 | `ch != me` 显式忽略；命中→改配隔离（`pending=cfg` + post `APPLY_SIG` + 转 Unconfigured，§6） |
| `APPLY_SIG` | 无 | `applyEvt` 静态自投 | 静态 | 仅 Unconfigured 响应：`configure(pending)`→重入先检后测；该态唯一出边 |
| `CJC_SIG` | `{temp_c, valid}` | 发布（ColdJunction 1 Hz） | 池A 动态 | 复制进 `cjc` 缓存，不保存事件指针 |
| `SAMPLE_SIG` | 无 | `te_sample` 自投 | 静态 | 仅 Measuring 响应；采样一步 |
| `SETTLE_SIG` | 无 | `te_settle` 自投 | 静态 | Detecting=检查就绪；Settling=转换就绪；迟到事件结构上不可达（§6） |
| `BREAK_WINDOW_SIG` | 无 | `te_break` 自投 | 静态 | 仅 Measuring 响应→Detecting；他态默认忽略 |
| 输出 `MEASURE_SIG` | `{ch, value, status}`（`ch` 0 基） | 本 AO 发布 | 池A 动态 | 每次采样一条（含冻结分支）；断线检查发现断线即时揭示一条 |
| 输出 `CFG_APPLIED_SIG` | `{ch}` | 本 AO 发布 | 池A 动态 | apply 成功才发布（无 ok 字段：失败只发状态 6，不改变 AoModbus 记账） |

通道状态码（`status`，u16，与寄存器映射一致，wire 值冻结）：

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

断线码按**通道类型**解释：TC 通道任一断线结果一律报 2（回路开路，两根线位不可
分辨）；PT100 按线位报 3/4；PT100 收到不可分辨的 `ANALOG_INPUT_WIRE_BROKEN`
（本板前端不产生）视为前端契约违约，报 6。断线在检查完成时揭示、采样拍持续维持。

## 4. 状态机

```text
AoChannel (top, 初始 → Unconfigured)
├── Unconfigured
└── Working
    ├── Detecting    （断线检测硬件模式）
    ├── Settling     （测量硬件模式，转换稳定）
    └── Measuring    （测量硬件模式，周期采样）
```

| 状态 | 定性含义/合法事件 | entry | initial/超状态不变量 |
| --- | --- | --- | --- |
| `Unconfigured` | 无生效配置/改配隔离中，不碰硬件测量路径；唯一出边 `APPLY_SIG` | disarm 全部定时器（无条件）；**不发布状态**（镜像维持，无 1 码闪烁） | Working 不变量：`cfg` 已被 `analog_input_configure()` 接受 |
| `Working` | 配置生效；`CJC_SIG`/`CONFIG_SET_SIG` 公共处理 | — | initial 目标由配置类型决定（APPLY_SIG 重入路径） |
| `Detecting` | 硬件在断线检测模式，值冻结、状态维持 | `enable_break_detection(&d)`→arm `te_settle(d)`（不发布） | `check_wire_break` 只在此态合法 |
| `Settling` | 硬件在测量模式，等首个有效转换 | arm `te_settle(settle_ms)` | `settle_ms` 由来源转换写入 |
| `Measuring` | 周期采样循环 | arm `te_sample(ticks)`；可检测型 arm `te_break(3000)` | `read_sample/convert` 只在此态合法 |

事件—配置—行为要点：

- `CONFIG_SET_SIG`（`ch==me`，Working 叶）：**改配隔离**（§6）——先撤防三个定时器，
  再 `pending=cfg` + post `APPLY_SIG` + 转 Unconfigured；Unconfigured 内命中则只合并
  到最新 `pending`（连续改配合并为一次 configure）。
- `APPLY_SIG`（仅 Unconfigured）：`configure(pending)` 成功 → `cfg=pending`、
  `settle_ms=d`、发布 `CFG_APPLIED_SIG {ch}` → 可检测型进 Detecting、线性型进
  Settling（**先检后测**重入，此刻 FIFO 已无迟到事件）；失败 → 发布状态 6 留
  Unconfigured，等下一次写配置（无 ok 回调——失败不改变 AoModbus 记账，
  `applied[ch]` 保持 0，同值重写即重试）。
- Detecting `SETTLE_SIG`：`check_wire_break` → 断线即发布状态 2/3/4（按类型），
  完好不发布（0 随首条测量值）→ Settling。Settling `SETTLE_SIG`：`do_sample()` →
  Measuring。Measuring `SAMPLE_SIG`：`do_sample()`；`BREAK_WINDOW_SIG` → Detecting。
- 迟到定时器事件（撤防失败漏达）落 Unconfigured 或不处理它们的叶子，QHsm 默认
  忽略——防御性无害（§6 保证其结构上不可达）。

模型检查：初始转换目标 Unconfigured 无守卫；Detecting/Settling entry 不读触发事件
参数（延迟经扩展变量 `settle_ms` 传递）；所有副作用在处理函数 switch 分支内。

## 5. `do_sample()` 动作

```text
wire != OK                            → 发布状态 2/3/4（值冻结），返回
read_sample(&code)                    失败 → 发布状态 6（值冻结），返回
TC 且 !cjc.valid                      → 发布状态 5（值冻结），返回
convert(code, ctx, &value)            失败 → 发布状态 7（值冻结），返回
last_value 更新；发布 MEASURE_SIG(ch, value, 0)
```

`值冻结` = 值字段填 `last_value`（尚无首值时 0），有效性以状态码为准。`ctx` 组装：
TC → `cold_junction = cjc.temp_c`；线性型 → `linear_min/max = cfg.*`；PT100 → 空上下文。

## 6. 定时设计与改配隔离

采样周期 = `ceil(1e6/R)` tick（R 单位 mHz，现算而非查表——见总体设计 §1 理由 6），
`te_sample` 为周期定时器（Measuring entry arm 一次），速率恒 ≤ 标称。稳定延迟
（`configure/enable/disable` 返回，整毫秒）直接 arm，0 至少 arm 1 拍。

### 定时器竞态的结构规避（零守卫）

竞态：改配事件与 `te_settle` 到期同队竞速时，迟到到期会落进重入后的状态被误当
"就绪"（提前 `check_wire_break` = 假断线报警，提前 `do_sample` = 违先检后测）。
QP 不自动丢弃迟到事件（`QTimeEvt_disarm` 契约明言由状态机处理）。

规避机制：**改配经 Unconfigured 隔离 + `APPLY_SIG` 跨派发延迟重入**。改配转换
**先撤防三个定时器**，再"缓存 `pending` + post `APPLY_SIG` + 转 Unconfigured"
（exit 撤防保留，无锁存故无毒）。顺序不可颠倒：撤防先于 post 保证任何迟到到期都
入队在 `APPLY_SIG` 之前（撤防前已到期 = 先入队；撤防后定时器停摆 = 不可能再投）。
由 FIFO 全序：迟到事件必先于 `APPLY_SIG` 派发 ⇒ 落点恒为 Unconfigured ⇒ 被默认
忽略；`APPLY_SIG` 派发时队列已无同类定时器事件，新 arm 代永不与旧事件混淆。
**零守卫、零旗标、零计数**，无挂死失败模式。

结构纪律（评审可机械检查，违反即定理失效）：

1. Unconfigured 不 arm 任何定时器，唯一出边（转换）是 `APPLY_SIG`；
2. 定时器 arm 只发生在 Detecting/Settling/Measuring 的 entry；
3. 叶 exit 无条件撤防自己的定时器（返回值忽略——无锁存，误调无毒）；
4. 取消转换内**先撤防、后 post** `APPLY_SIG`（FIFO 前提，顺序不可颠倒）。

不采用 `QTimeEvt_wasDisarmed` 守卫（锁存有双改配覆盖、boot 毒化等缺陷，失败模式
是静默挂死）；纯信号拆分方案因迟到事件与真事件代际不可辨识而不可行。

## 7. 容量与故障路径

| 项目 | 数值 |
| --- | --- |
| 队列 | 13（启动 8×CONFIG_SET + 1×CJC + ≤3 tick + 1×APPLY + 裕量；×8 = 104 槽） |
| QTimeEvt | 3/通道（24 个）；静态 `APPLY_SIG` 8 通道共用 1 个 |
| RTC | SPI ≈0.3 ms + 浮点换算 ≈0.5 ms（-O1 估算，**实测收口**）≤ 1 ms |

| 故障 | 分类 | 处理 |
| --- | --- | --- |
| 断线 | 合法异常 | TC→2、PT100→3/4（该通道状态寄存器），检查完成即揭示、采样拍维持 |
| SIG24131 IO 错误 | 合法异常 | 状态 6，下拍重试；连续失败不升级（日志限频） |
| 冷端失效 | 合法异常 | TC 状态 5 值冻结，CJC 恢复后自动续测 |
| `convert` 超量程 | 合法异常 | 状态 7，值冻结，下一拍继续 |
| configure 失败 | 合法异常 | 状态 6 + 降级 Unconfigured 等待重配 |
| 队列/池耗尽 | Bug/容量缺陷 | `QACTIVE_POST_X` 带裕量断言暴露，不静默丢 |

## 8. 验证要点

宿主测试 `tests/test_channel_ao.c`（真实 QP dispatch + 事件/tick 注入）覆盖：先检后测
轨迹、3 s 循环、状态码映射（含 `wire_status_code` 用例）、改配竞态穷举（迟到事件全落
Unconfigured、连续改配合并为一次 configure）、速率档位（fake 调用日志）。上板验证：
断线注入（2 / 3、4）、冷端失效（5）、8×100 Hz 压测（队列 `getQueueMin` ≥ 5）。
