# analog_collection_08ch_rtu

8 通道模拟量采集 RTU 固件，目标 MCU 为 AT32F421C8T7（Cortex-M4，120 MHz，64 KB Flash，16 KB SRAM）。

本文档保存代码中不再保留的设计说明、初始化顺序、资源分配和移植注意事项。源码注释只保留简洁的 Doxygen 函数/类型/宏说明，以及实现中的关键约束提醒。

## 目录与分层

```text
app/                    应用入口、日志服务
board/inc, board/src    板级硬件与板级设备适配
board/ports/            中间件移植层
board/vendor_wk/        WorkBench 生成的外设初始化代码
```

- `app/`：组合根（composition root）、日志初始化，后续放参数模块和应用对象。
- `board/`：时钟、外设、SysTick/NVIC、板级设备对象；不初始化日志、不拥有 FlashDB。
- `board/ports/`：SEGGER RTT、EasyLogger、FlashDB/FAL、QP 的移植代码。
- `board/vendor_wk/`：WorkBench 生成代码，保留其原始注释。

## 启动顺序

```c
log_init();     /* SEGGER RTT + EasyLogger */
board_init();   /* 时钟、外设、板级设备 */
```

`board_init()` 只负责板级硬件：

1. 120 MHz 时钟树、Flash 等待周期、外设时钟门控
2. SysTick 1 ms、NVIC 优先级
3. TMR6 1 MHz 时基
4. GPIO、USART1、SPI1、TMR14
5. SIG24131 设备对象构造与上电复位
6. 软 I2C 总线构造与初始化、TMP75 构造、0x48 探测
7. analog_input 板级对象构造

日志必须先于 `board_init()` 初始化，`board_init()` 内的 `elog_error()` 诊断信息才可见。

参数存储初始化不属于 board 层。后续由参数/存储模块（例如 `app/param` 或 `library/storage`）拥有 `fdb_kvdb`，并调用 `fal_init()` / `fdb_kvdb_init()`；日志必须在 `fdb_kvdb_init()` 之前启动。

## 日志链路

```text
elog_* / FDB_PRINT / printf
        -> EasyLogger
        -> SEGGER_RTT_Write(0, ...)
        -> RTT Viewer
```

- RTT up channel 0，1 KB 缓冲区，`SEGGER_RTT_MODE_NO_BLOCK_SKIP`。
- 缓冲区不足时整行丢弃；没有 RTT 主机读取时日志不能作为唯一证据。
- `printf` 通过 `fputc()` 逐字节重定向到 RTT，仅作兜底；高频日志使用 EasyLogger。
- EasyLogger 行缓冲 256 B，输出级别 `ELOG_LVL_DEBUG`，格式 `LVL | TAG`，不开异步、缓冲和颜色。
- `elog_cfg.h` 必须位于 EasyLogger 中间件 include 目录之前，否则会使用模板配置。
- 当前约定不在中断上下文打日志；`elog_port_output_lock/unlock` 为空，多任务或 ISR 日志需要补临界区。
- FlashDB 的 `FDB_PRINT` 直接转到 `elog_raw()`，因此 FlashDB 一条日志可能分成多次 RTT 写入。

## FlashDB / FAL

- 片内 Flash：64 KB，1 KB 扇区，支持字节编程。
- 参数分区：`fdb_kvdb`，`0x0800F000..0x0800FFFF`，偏移 60 KB，长度 4 KB。
- 固件 ROM 区域为 `0x08000000..0x0800EFFF`（60 KB）。
- FAL 设备：`at32f421_onchip`，`blk_size = 1024`，`write_gran = 8`。
- FlashDB：`FDB_USING_KVDB`、`FDB_USING_FAL_MODE`、`FDB_WRITE_GRAN = 8`。
- `FDB_WRITE_GRAN` 必须与 `fal_flash_dev.write_gran` 一致；错误值会静默破坏 KV 格式。
- `FAL_PART_HAS_TABLE_CFG` 使用静态分区表和缓存，不依赖 malloc。
- FAL 端口对读写擦做溢出安全的范围检查；擦除要求 offset 和 size 都按 1 KB 对齐。
- 写操作逐字节调用 `flash_byte_program()` 并回读校验。
- `FDB_DEBUG_ENABLE` 当前打开，发布构建可关闭以节省 ROM 和 RTT 流量。
- FAL/FDB 日志走 `printf` / `elog_raw`，FAL 自带 ANSI 转义，RTT Viewer 中可能可见。

## 板级资源

### 时钟与中断

- 系统时钟 120 MHz。
- SysTick：1 ms，优先级 15。
- USART1、TMR14：抢占优先级 5。
- TMR6：1 MHz 时基，供 `wk_delay_us/ms` 使用。
- TMR14：10 us/tick，16 位周期覆盖 T3.5 和 9600 baud 下的 TX 看门狗。

### SIG24131

- 多片 SIG24131 共用 SPI1，每片独立 GPIO 片选。
- 构造时释放片选；上电后统一复位，共享一次 43 ms 等待。
- SPI 传输带超时，超时返回 `SIG24131_ERR_IO`。
- 板级通道数由 `SIG24131_BOARD_INSTANCE_COUNT` 决定，最大 8。

### I2C 软总线

- PA1 = SDA，PA2 = SCL，开漏输出、初始置高、`GPIO_PULL_NONE`，依赖外部上拉；不使用硬件 I2C。
- 半周期按实例配置：`i2c_soft_config_t.half_period_us`，本板填 `0`，解析为默认 5 µs（名义 100 kHz 以内，标准模式最稳）。库不校验下限，低于 5 µs 即离开标准模式。
- 位延时用 `wk_delay_us()`，基于 TMR6 轮询计数，可在关中断下工作。
- 临界区用 `board_critical_enter/exit`，与 QP 共享同一个嵌套计数；库只做逐边沿 5–20 µs 的屏蔽，不包整事务。
- 仅任务上下文，禁止 ISR；单实例非线程安全，调用方负责串行。
- `i2c_soft_board_init()` 失败只记录日志、不阻塞启动；`i2c_soft_init()` 成功后才能发起事务。

### TMP75

- 单片 TMP75，地址 0x48（A2–A0 接地），ALERT 未接，不处理。
- 复位默认 9-bit 分辨率、连续转换、比较器模式，实际步进 0.5 °C；需要 0.0625 °C 时由应用调用 `tmp75_write_config()` 切 12-bit。
- 启动时只对 0x48 做一次 `i2c_soft_probe()`：NACK 只说明地址/接线问题，不表示温度可读。
- 读取方式：`tmp75_board_get_device()` 取设备对象，再调用 `tmp75_read_temperature()`。
- 板级 `transfer` 把所有 I2C 失败统一映射为 `TMP75_ERR_IO`；启动 probe 直接调用 `i2c_soft_probe()` 以保留原始状态码。

### 模拟输入

- 4-20 mA 采样电阻 200 Ω。
- 电压/电流前端衰减比 5:1。
- 内部参考 2.5 V；PT100 采用 1.65 kΩ 参考电阻和 500 uA 激励电流。
- 热电偶增益 32，4-20 mA 增益 2，0-10 V 增益 1，PT100 增益 8。
- 热电偶与 PT100 共用差分对 AIN5/AIN4；4-20 mA 与 0-10 V 使用 AIN0/AVSS。
- CV/COM 电平按输入类型切换：
  - PT100：COM=0，CV=0
  - 0-10 V：COM=1，CV=0
  - 4-20 mA：COM=1，CV=1
  - 热电偶：COM=1，CV=0
- 默认采样率 10 Hz；配置后需要 2 个转换周期稳定。
- 支持全部 SIG24131 数据率，单位 mHz。
- 热电偶断线检测使用 burnout 电流；PT100 通过参考电阻电压判断断线/断激励/断回流。

### Modbus RTU

- USART1：TX PA9，RX PA10，115200 8N1。
- RS485 DE：PA11，高电平驱动总线。
- T3.5 / TX 看门狗：TMR14。
- RX/TX 缓冲区各 256 B。
- USART1 与 TMR14 共用优先级 5，不能互相抢占。
- 板级只拥有一个从站实例；`modbus_slave_board_ctor()` 需要应用传入 callbacks/identity。
- 应用必须把 `on_frame_received` 事件接到 `modbus_slave_advance()`，并在自己的上下文中调用。
- 从站地址由 `callbacks.address` 提供，核心不保存地址。
- 临界区使用 board 的可嵌套中断屏蔽，与 QP 的 QF 临界区共享同一个嵌套计数。

## QP 移植

- `board.c` 提供强定义的 `QF_crit_entry_()` / `QF_crit_exit_()`，覆盖 `qv_port.c` 的弱实现。
- 临界区使用 `board_critical_enter()` / `board_critical_exit()`，通过 `board_crit_nest` 支持嵌套。
- 最外层进入要求中断已开启，嵌套进入要求中断已屏蔽；违规调用 `board_crit_error_()`，错误码 110/111。
- 同一嵌套计数同时服务于 QP、Modbus 和板级模块。

## 断言

- 项目统一使用 QP 的 `Q_ASSERT()` / `Q_onError()`。
- `Q_onError()` 直接通过 `SEGGER_RTT_Write()` 输出，不经过 stdio 或 EasyLogger，可用于中断上下文。
- FlashDB 的 `FDB_ASSERT()` 桥接到 `Q_onError("FlashDB", __LINE__)`；`Q_UNSAFE` 时编译为空操作。
- EasyLogger 的 `ELOG_ASSERT` 通过 `elog_assert_set_hook()` 桥接到 `Q_onError("EasyLogger", line)`。
- FAL 内部 `assert` 当前由 `fal_def.h` 定义为空操作（`FAL_DEBUG = 0`）。

## 第三方与生成代码

以下目录保留其原始版权/许可证和生成注释，不做注释清理：

- `board/ports/qp/`：QP 移植与配置模板
- `board/vendor_wk/`：WorkBench 生成代码
- `.pack/`：MCU 器件包

## 构建

- IDE：EIDE
- 编译器：ARMCLANG 6，C99，`-O0`
- 运行库：microlib
- 调试构建定义：`AT32F421C8T7`
- include 顺序要求：`board/ports/easylogger` 必须早于 `../middleware/EasyLogger/easylogger/inc`
- 参数分区使用 Flash 尾部 4 KB，下载时不要使用全片擦除
