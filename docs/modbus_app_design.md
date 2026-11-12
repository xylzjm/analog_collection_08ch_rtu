# Modbus 应用层设计

本文说明应用层如何把 Modbus RTU 从站、用户参数、出厂校准和 Flash 保存组合在一起。寄存器地址和协议细节以源码常量和用户 README 为准；工厂隐藏块见 `docs/factory_calibration.md`。

## 模块边界

| 模块 | 责任 | 关键约束 |
|---|---|---|
| `modbus_app` | 从站 AO、输入映像、用户寄存器回调、FC11、隐藏块解锁和命令入口 | 是用户参数值的唯一写者 |
| `param_app` | 用户参数镜像、默认值、revision、脏标志 | 访问器使用 `board_critical_enter/exit` |
| `cal_data` | 每通道 `calN` 工厂校准记录、有效位、应用到板级 | 与用户参数完全隔离 |
| `modbus_app_regs` | 用户寄存器常量、类型/速率映射、int32 编解码 | 纯 C，不依赖 QP，可主机测试 |
| `modbus_cal_regs` | 隐藏块常量、STATUS/ERROR 枚举、范围判断 | 纯 C，不依赖 QP |
| `storage_app` | 启动同步加载后的运行期异步保存 AO | Deferred Event + Reminder |
| `storage_flash` | FlashDB/FAL 初始化、KV 读写、记录校验 | `sys`、`ch0..7`、`cal0..7` |
| `calibrate_app` | 校准 QHsm 组件：暂停通道、配置 ADC、采样、计算、保存 | 跑在 `modbus_app` 线程内 |
| `channel_app` / `cold_junction_app` | 本阶段契约桩 | `channel_app_get()` 返回 NULL |

`calibrate_app` 是 orthogonal component：没有自己的队列、优先级或任务。容器在 `operating` 超级状态转发校准信号、三个组件定时器和保存结果；`CAL_TIMEOUT` 由容器自己处理。

## 事件与内存池

共享信号只来自 QM 生成的 `common_app.h`。带载荷的事件定义在 `app_evt.h`：

- `cal_select_evt_t`：栈上构造并同步 `QHSM_DISPATCH()`，不进事件池。
- `meas_evt_t`：`CH_DATA` / `CJ_TEMP`，池 2，64 块。
- `cfg_evt_t`：`CH_CONFIG`，池 3，16 块。
- `cal_save_evt_t`：`CAL_SAVE`，池 1，8 块。

测量事件比校准保存事件多 4 字节填充，保证 `Q_NEW_X()` 的首个足够大块选择落到正确的池；否则两个 16 字节事件会共用池 1。

握手和结果事件使用模块内静态 `QEvt` 和 `QACTIVE_POST()`；只有测量/配置事件允许 margin 0 丢弃。

## 输入映像

`modbus_app` 维护 8 个通道和冷端的输入映像：

- 上电初值：通道 `value = INT32_MIN`、`status = 5`；冷端 `value = 0`、`status = 1`。
- `CH_DATA_SIG` 更新通道；`status != 0` 时强制 `INT32_MIN`。
- `CJ_TEMP_SIG` 更新冷端。
- 线性下限 ≥ 上限时直接置通道状态 6 和哨兵，修正后恢复状态 0。

## 用户寄存器回调

FC03/FC04 读取任意合法范围；保留区返回 0。FC06/FC10 写入采用“先完整校验、后整体提交”：

1. 类型、采样率、增益、偏移、地址逐项校验。
2. 任一寄存器非法时整条请求返回异常 02/03，参数不变。
3. 跨多个通道的 FC10 全部通过后才提交，每个受影响通道投递一次 `CH_CONFIG`（通道 AO 为 NULL 时跳过）。
4. 写地址成功后同步 `slave->identity.slave_id`，当前应答仍使用请求中的地址。

用户保持寄存器布局：

| 地址 | 内容 |
|---|---|
| `0x0000` | 从站地址 1..247 |
| `0x0001` | 波特率 9600，只读 |
| `0x0100 + c*0x20 + 0x00` | 传感器类型 |
| `+0x01` | 采样率索引 |
| `+0x02..0x03` | 线性下限 int32 |
| `+0x04..0x05` | 线性上限 int32 |
| `+0x06..0x07` | 用户偏移 int32 |
| `+0x08..0x09` | 用户增益 int32 |
| 其余 | 保留/只读规则 |

## 保存策略

`storage_app` 是 QV 下的普通 AO，不能抢占 Flash 操作；它提供状态机、重试和模块边界，不消除 Flash 写入停顿。

- `PARAM_SAVE`：`modbus_app` 在参数写入后重新武装 1 s 去抖；到期时若 500 ms 内收到过帧且未到 10 s 上限，则顺延 500 ms，否则投递保存请求。
- `CAL_SAVE`：校准提交后立即投递，不走去抖。
- `storage` 超级状态对 `PARAM_SAVE` / `CAL_SAVE` 调用 `QActive_defer()`；`idle` entry 用 `QActive_recall()` 循环回收。
- `writing_params` / `writing_cal` 的 entry 只写一个 key，然后投递 `STORAGE_MORE` / `STORAGE_DONE` / `STORAGE_FAIL`；转换只按完成事件走。
- 参数保存失败：置设备故障 bit1，进入 `fault`，5 s 后按脏标志重试；成功清除 bit1。
- 校准保存失败：投递 `CAL_SAVE_FAILED`，丢弃待写记录，回 `idle`；不重试、不置设备故障位。

`param_app` 的 revision 用于清脏竞态判断：只有 revision 未变才清脏，保存期间的新写入不会被误清。

## 启动顺序

```c
log_init();
QF_init();
QActive_psInit(subscr, APP_SIG_MAX);
QF_poolInit(cal_pool,  sizeof(cal_pool),  sizeof(cal_save_evt_t));
QF_poolInit(meas_pool, sizeof(meas_pool), sizeof(meas_evt_t));
QF_poolInit(cfg_pool,  sizeof(cfg_pool),  sizeof(cfg_evt_t));
board_init();

param_app_init();
cal_data_init();
load_fault = storage_load();
cal_data_apply_all();

modbus_app_ctor(&modbus_obj, load_fault ? MODBUS_APP_DEV_FAULT_LOAD : 0U);
storage_app_ctor(&storage_obj);

QActive_start(&modbus_obj.super, Q_PRIO(20, 0), modbus_queue, 32, NULL, 0, NULL);
QActive_start(&storage_obj.super, Q_PRIO(9, 0), storage_queue, 8, NULL, 0, NULL);
QF_run();
```

`modbus_app` 的初始转换显式调用 `QHSM_INIT(&me->cal.super, NULL, 0U)`；`SysTick_Handler()` 在计数后调用 `QTIMEEVT_TICK_X(0U, NULL)`，tick 频率 1 kHz。

## 已知边界

- `channel_app` 尚未实现：校准组件用自己合成的 `CH_CAL_READY` 走通配置/采样；退出时不恢复用户 ADC 配置。
- 冷端模块尚未实现：`CJ_TEMP` 事件路径已就绪，输入映像在收到事件前保持故障初值。
- QV 非抢占：Flash 擦除/编程期间整机停顿，保存瞬间可能丢一帧。
