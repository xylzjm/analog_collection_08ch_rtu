# modbus_image — 类型化镜像与写事务组件

Modbus 数据空间的类型化镜像（保持/输入寄存器背后的真实数据）与保持寄存器写事务。
被动组件：无自主行为、无独立时限、**零 QP 依赖**（不使用任何 QP 工具宏），全部
执行发生在 `AoModbus` 上下文内——modbus 核心以镜像对象为 `ctx` 直调四个数据空间
回调与地址回调。

## 1. 职责与不变量

| 职责 | 说明 |
| --- | --- |
| 类型化镜像 | `cfg[]`（已固化配置）、`applied[]`（通道是否已接受该配置）、`value[]/status[]`（遥测最近状态）、`cjc_temp/cjc_valid`、`addr_image` |
| 故障锁存 | b1（参数写失败）、b2（参数加载失败，含 dev/addr）、b3（FlashDB 初始化失败）；输入寄存器 68 的合成见 §3 |
| 写事务 | 保持寄存器 FC06/FC16 的原子性纪律：校验 → 经 `param` 同步固化 → 才更新镜像 → 才通知；设备寄存器 128 同路径 |
| 提交通知 | 通道配置提交成功后经**普通函数指针通知器**回调（`modbus_image_cfg_commit_fn`）；本组件不认识 QP，发布由 `AoModbus` 折算成 `CONFIG_SET_SIG` |
| 地址回答 | `callbacks.address` 返回镜像中的生效地址 |

地址/编解码/异常口径由 `reg_map` 组件承载（`docs/component/reg_map.md`）；
固化细节由 `param` 组件承载（`docs/component/param.md`）。本组件编排三者。

## 2. 接口

| 函数 | 语义 |
| --- | --- |
| `modbus_image_init` | 镜像复位到出厂默认：通道状态 1（未配置）、地址取下限、b0 置位前的 cjc_valid=0 |
| `modbus_image_boot` | 采纳 `param_boot_load()` 结果：有效项成为镜像配置、`applied=0`，失败项成为锁存位 |
| `modbus_image_callbacks` | 返回绑定本镜像的数据空间回调表（逐实例存储，ctx=镜像），供 `modbus_slave_board_ctor()`/`modbus_slave_ctor()` 使用 |
| `modbus_image_set_cfg_notifier` | 注册通道配置提交通知器（可 NULL 关闭） |
| `modbus_image_set_applied` | 通道 apply 成功/失败回写；失败后同值重写会再次通知 |
| `modbus_image_set_measure` | MEASURE_SIG 到达时写入遥测镜像（ch 0 基，事件线与镜像同基） |
| `modbus_image_set_cjc` | CJC_SIG 到达时写入冷端镜像（b0 源） |

## 3. 写事务（原子性全部所在）

与 `docs/ao/modbus_ao.md` §6、`docs/component/reg_map.md` §5 同源：

```text
holding 写回调（本组件内）：
    reg_map_check                       缺席 → EX 02
    CH_CFG：encode(cfg[ch]) → 帧覆盖 → decode → ch_config_validate（含归一化）
    DEV：  校验 1..247（非法 → EX 03）
    param_save_*（同步固化 + 写后读回）
        读回=下发值 → 更新 cfg/applied=0（通道块触发通知器、清 b1/加载失败位）→ EX 00
        读回≠下发值 → 什么都不动、b1 置位 → EX 04
        无变化且 applied=1 → EX 00（不写 flash、不通知）
        无变化且 applied=0 → EX 00（重新通知，让通道重试 apply）
```

`EX 00` 表示**已固化**，不承诺通道已接受；通道 apply 结果由 `applied[]` 和
通道状态 6 表达。这样写回调不需要跨 AO 同步等待，重写同值也能重试。

- 设备寄存器写不触发通知器（地址变更不发布通道配置）。
- 故障字（输入寄存器 68）：`reg_map_fault_word(cjc_valid, b1|b2|b3)`，b0 实时、
  b1..b3 锁存（`docs/ao/modbus_ao.md` §5 的位布局）。

## 4. 与 AoModbus 的边界

| 事项 | 归属 |
| --- | --- |
| 数据空间回调、镜像内容、故障锁存、EX 决策 | 本组件 |
| QP 事件订阅、`FRAME_SIG→advance()` 路由、TX 计数 | `AoModbus` |
| `param_boot_load()` 调用、启动 `CONFIG_SET_SIG` 发布 | `AoModbus`（初始转换） |
| 写事务提交 → `CONFIG_SET_SIG` 发布 | 通知器回流 `AoModbus` 折算 |
| Report-Slave-ID identity | `AoModbus`（无逐实例状态） |
| `modbus_ao_callbacks()` | 对 `modbus_image_callbacks()` 的一行委托 |

## 5. 验证矩阵（本组件）

| 测试 | 激励 | 预期 | 证据 |
| --- | --- | --- | --- |
| 写事务全路径 | 直调 `holding_registers`（EX 00/03/04、单寄存器 RMW、同值免写） | 镜像/通知器/锁存与 EX 一致；非法值镜像不动 | `app/component/test/test_modbus_image.c` |
| apply 重试 | 提交后 `applied=0`，再次写同值 | 重新通知；`applied=1` 后同值静默 | 同上 |
| 提交失败 | `kv_store_mem.corrupt_read` 制造读回不一致 | EX 04 + b1；故障字经输入寄存器可见 | 同上 |
| 地址寄存器 | 写 247/0/248、读回 | 生效地址移动、EX 03 拒绝、`callbacks.address` 同步 | 同上 |
| 启动采纳 | 构造 boot 结果 | 有效项入镜像、失败项入 b2/b3 | 同上 |
| AO 级全链路 | 真实 modbus 核心收帧 → advance | 写帧应答字节、发布、静默路径 | `tests/app/test_modbus_ao.c` |

板上行为（时序、固化、故障字在真实线路上）仍按 `docs/ao/modbus_ao.md` §9 验证。
