# AGENTS.md

面向 AT32F421C8T7 的 8 通道模拟量输入 RTU 固件（4-20 mA、0-10 V、PT100、热电偶）：
Cortex-M4，120 MHz，可用 Flash 60 KB，SRAM 16 KB。C99、armclang（AC6）、microlib、
QP/C 8.1.3（QV 内核）。EIDE 是唯一的构建系统——不存在 Makefile、CMake、CI、lint
或类型检查步骤。

## 先读这些

- `README.md` 是板级设计参考：启动顺序、时钟与中断优先级、引脚复用、
  SIG24131/TMP75/I2C/Modbus 资源、日志链路、Flash 分区、QP 移植层与断言策略。
- 应用设计位于 `docs/`：`qp-application-design.md`（活动对象划分、事件契约、
  容量、设计理由）、`docs/ao/*.md`（每个活动对象一篇）以及
  `docs/component/reg_map.md`（纯寄存器映射）。
- 设计行为发生变化时，同步更新文档；源码注释只保留简短的 Doxygen `@brief` 行。
- 文档与源码均为 UTF-8 无 BOM。文档使用 CRLF；源码保留各文件原有换行（多数为
  CRLF，少数为 LF，不要跨文件统一）。这个 Windows shell 会破坏非 ASCII 输出，
  所以判断编码要看字节，而不是看打印出来的文本。

## 依赖与工作区

构建会读取既不随仓库提供、也不是子模块的兄弟目录：`../library`（sig24131、
tmp75、analog_input、i2c_soft、modbus_slave）与 `../middleware`（qp、SEGGER RTT、
EasyLogger、flashDB/FAL）。多根工作区文件把这两个目录都加了进来；缺少它们什么
都编译不了。它们只是普通目录，不是 git 仓库，因此那里的改动不会被本仓库跟踪。

## 构建与烧录

- 只能通过 EIDE 构建：VS Code 任务 `build`、`rebuild`、`clean`、`flash`、
  `build and flash`，或使用项目 uid `2b2c107904df4cdb1e97e57d6947db29` 的 EIDE MCP
  工具（`rebuild` 是可靠的全量检查）。
- 一次干净构建是 64 个 C 文件 + 1 个汇编文件，ROM 51.38/60 KB（85.6 %），RAM
  9.09/16 KB。`-O1` 是有意为之：在 `-O0` 下完整 QP 运行时加上 FlashDB KV 层大约
  需要 70 KB，装不下。把 ROM 余量当作稀缺资源。
- 烧录时绝不要整片擦除：最后 4 KB（`0x0800F000..0x0800FFFF`）存放 `fdb_kvdb`
  参数分区，而 ROM 只链接到 60 KB（`0xF000`）。
- SKU 变体在编译期固定通道数：在 EIDE 预处理器宏（`.eide/eide.yml` 的
  `defineList`）中定义 `SIG24131_BOARD_COUNT=N`（1..8）。该开关在
  `board/inc/board_config.h` 解析为 `BOARD_CHANNEL_COUNT`，它是“已装配通道数”的
  唯一名字，板级与应用代码共用。当前的 define 列表只设置了 `AT32F421C8T7`，所以
  默认是全部 8 个位置。未焊接的位置返回 EX 02（ILLEGAL_DATA_ADDRESS）；KV 键保持
  8 位置布局。主机测试始终按完整的 8 位置板建模；验证变体要编译固件并在目标板上检
  查未焊接地址，而不是重跑主机测试。
- `build/` 已被 gitignore 且经常是过期的。绝不要把 `build/Debug/*.lnp`、`*.map`
  或 `.obj` 当作当前文件列表——先重新构建。
  `build/Debug/compile_commands.json` 每次构建都会重新生成，供 clangd 使用。

## 添加源文件

- `.eide/eide.yml` 显式列出每个文件（`srcDirs: []`）。新增的 `.c` 如果没有加入
  目标的虚拟文件夹/文件列表，就会被静默地排除在构建之外。在 EIDE UI（或 YAML）中
  注册它，重新加载，然后重新构建。
- `incList` 中的包含顺序是有实际作用的：`board/ports/easylogger` 必须排在
  `../middleware/EasyLogger/easylogger/inc` 之前，否则模板 `elog_cfg.h` 会遮蔽
  `board/ports/easylogger/elog_cfg.h`。
- `.eide/eide.yml` 把 `board/ports/flashdb` 写成小写，而实际目录是
  `board/ports/flashDB`。在 Windows 上无害，在区分大小写的主机上会出错。

## 生成代码与厂商代码

- `board/vendor_wk/project/` 是由 `board/vendor_wk/vendor_wk.ATWP` 驱动的 AT32
  WorkBench 输出；`.pack/` 是 AT32F421 器件包。保留它们原有的英文版权声明与生成
  注释，以及 MS 风格格式。
- `board/vendor_wk/project/` 下没有任何文件带有手工修改：DE 引脚、USART1、TMR14
  以及 TMR6 延时基准都保存在 `.ATWP` 中，因此从它重新生成在结构上就是安全的。无论
  生成的初始化状态如何，板级移植层都拥有自己的外设前置条件（所有权注释见
  `board/src/modbus_slave_board.c`）。
- 厂商的 `main.c` 与 `at32f421_int.c` 不属于构建的一部分。入口点是 `app/main.c`；
  中断处理函数位于板级模块中（`SysTick_Handler` 在 `board/src/board.c`，
  `USART1_IRQHandler` / `TMR14_GLOBAL_IRQHandler` 在
  `board/src/modbus_slave_board.c`）。

## 应用层

- 满载板：10 个活动对象，每个已焊接位置一个 AoChannel，优先级 2..9，
  AoColdJunction 10，AoModbus 11，外加纯模块 `reg_map`。每个 AO 一个文件，QP/C
  示例风格：`app/channel_ao.*`、`app/cold_junction_ao.*`、`app/modbus_ao.*`；
  `app/app.h` 存放共享词汇（`ch_config_t`、信号、事件负载）。QV 是协作式的：
  活动对象之间永不抢占；ISR 只向各自的队列投递事件。SysTick（1 ms）是 QP 时间事件
  和 `board_tick_get()` 的唯一节拍源。
- AoModbus 拥有带类型的 Modbus 镜像、保持寄存器写事务（校验 -> 带回读验证的同步
  提交 -> 镜像 -> 发布）以及持久化。镜像本身就是变更检查快照（没有独立的快照）。
  故障字：b1 是锁存的写失败；b2/b3 每次读取时由加载结果推导（`store.db_ok` 守卫、
  SKU 掩码）。
- 持久化直接在 `modbus_ao.c` 中使用 FlashDB
  （`fdb_kvdb_init`/`fdb_kv_get_blob`/`fdb_kv_set_blob`/`fdb_blob_make`；
  `modbus_ao.c` 是唯一包含 `flashdb.h` 的生产编译单元）。KV schema（键
  `cfg/ch0..7`/`dev/addr`、`fdb_default_kv_node` 行）就在同一个文件里。主机测试在
  链接期用假实现替换这四个入口点（`tests/stubs/fdb_fake.c`，用空的
  `tests/stubs/fdb_cfg.h`
  针对真实厂商头文件编译）。
- 应用结果以 `CFG_APPLIED_SIG` 传递（AoChannel 发布，AoModbus 记录
  `config.applied[]`），提交成功的写入直接发布 `CONFIG_SET_SIG`：任何被动模块都
  不会回调活动对象。
- 活动对象通过构造函数注入获取各自的设备对象（`app/` 内没有板级单例）：`main.c`
  把板级单例传给通道和冷端构造函数，并把板级的从站存储与平台配置交给
  `modbus_ao_ctor()`（`modbus_slave_board_get()` / `modbus_slave_board_config()`）。
  板级只做提供者；AO 负责组装完整配置、构建核心并打开参数数据库。
- 启动顺序：通道、AoModbus、AoColdJunction。每个需要订阅的 AO 都在自己的初始迁移
  中完成订阅（QP/C 示例风格；`QActive_start()` 在运行 AO 之前完成注册），而启动
  加载（load_all + 发布）在 `QF_run()` 之前、于 AoModbus 的初始迁移内运行。
- 通道索引在应用层和板级 API 中都从 0 开始（事件、`cfg/ch0`..`cfg/ch7` KV 键）：
  `analog_input_board_get()` 接受 `i`，与 `main.c` 的做法一致。
- `on_frame_received` 通过 `FRAME_SIG` 路由到 `modbus_slave_advance()`。目标板上
  的行为（时序、Flash 提交、断线码）尚未在硬件上验证。

## 硬性不变量

- `log_init()` 必须在 `board_init()` 之前运行，否则该函数里的 `elog_error()`
  诊断信息会丢失。中断上下文中不得打日志：`elog_port_output_lock` 是空的。
- 参数数据库由 `modbus_ao.c` 拥有。它在单步的 `modbus_ao_ctor()` 中用
  `fdb_kvdb_init()` 打开（在 `QActive_start()`/`QF_run()` 之前）；`main.c` 只移交
  板级的从站存储和平台半配置。打开失败会让 `store.db_ok = 0`（故障字 b3），之后
  每次提交都以 EX 04 安全失败。
- 临界区只能通过 `board_critical_enter()` / `board_critical_exit()`
  （`board/inc/board.h`）。嵌套计数器与 QP 共享，QP 通过 `board/src/board.c` 中的
  强符号 `QF_crit_entry_`/`QF_crit_exit_` 覆盖进入它。不要添加 `__disable_irq()`、
  第二个 QP 移植层或本地嵌套计数器；违反契约会在 `Q_onError` 中以 110/111 陷入。
  该临界区通过 PRIMASK 屏蔽所有中断（`QF_USE_BASEPRI` 未启用），包括软 I2C 的位
  延时。
- 断言使用 `Q_ASSERT`/`Q_onError`；`Q_onError` 写原始 RTT，并且在 ISR 中也是安全
  的。FlashDB 和 EasyLogger 的断言已经桥接到它。
- `FDB_WRITE_GRAN`（8）是唯一来源：FAL 移植层由它构建
  `fal_flash_dev.write_gran`；手工改成不一致的值会静默破坏 KV 布局。
- USART1 运行在 9600 8N1。`modbus_slave_board.c` 用 `BOARD_MODBUS_BAUDRATE`
  （`board_config.h`，核心波特率的唯一归属地）给核心提供波特率；生成的
  `wk_usart.c` 初始化字面量必须与之保持一致。
- USART1 与 TMR14 共享抢占优先级 5，彼此不得互相抢占。Modbus 收/发缓冲区各
  256 B；TMR14 以 10 us 计数。

## 验证改动

1. EIDE rebuild 必须成功。
2. 在仓库根目录运行主机测试：`pwsh tests/run_host_tests.ps1`（`-KeepExe` 保留可执行
   文件）。gcc 必须在 PATH 上；脚本会回退到 `C:\Users\HP\mingw64\bin\gcc.exe`，
   否则抛错。脚本逐个编译并运行每个测试；新增测试必须加入它的 `$tests` 表。
   `tests/stubs` 必须保持在包含顺序的首位，这样主机的 `qp_port.h`、`elog.h` 和
   `fdb_cfg.h` 影子才能优先于固件头文件。
   - 七个可执行文件，全部位于 `tests/`（扁平）：`test_reg_map`（纯映射）、
     `test_modbus_data`（直接通过数据空间回调、在 `fdb_fake` 介质上驱动的写事务与
     持久化，不经帧；同时在 `test_fake_medium()` 中固定假实现自身的契约）、
     `test_channel_ao` 与 `test_modbus_ao`（事件/节拍注入的 AO 测试）、
     `test_cold_junction_ao`（基于 `tmp75_port_fake` 的冷端心跳、故障冻结与恢复）、
     `test_boot`（启动集成；接受场景参数 1 或 2，因为 QP 注册表不会在同一进程内
     重建两次）以及 `test_analog_input_convert`（`board/inc/analog_input_convert.h`
     中仅头文件的转换公式，对照手工推导的电气锚点，不链接 QP，不含板级编译单元）。
   - AO 测试把真实的 QP/C 源文件（`../middleware/qp/qpc/src/qf/*` + `qv.c`）链接
     到主机移植层 `tests/stubs/qp_port.h` 上，后者共用固件的 `qp_config.h`；调度
     循环复刻 QV 三元组（`QActive_get_` -> `QASM_DISPATCH` -> `QF_gc`）。三个设备
     假实现位于设备边界：`analog_input_fake`、`tmp75_port_fake`（在真实 `tmp75.c`
     之后）和 `modbus_port_fake`（在真实从站核心之后）；`fdb_fake` 在
     `modbus_ao.c` 中真实持久化代码之后的链接缝上替换那四个 FlashDB 入口点（真实
     厂商头文件，链接期假实现）。
3. 脚本对每个测试打印 `N checks, 0 failures`。`tests/` 在 EIDE 构建之外，因此主机
   测试说明不了 ROM 或目标板时序。
4. `../library` 中的纯逻辑代码在 `<module>/test/*.c` 下有主机测试——自包含的
   `main()` 加一个 `CHECK` 宏，不参与固件构建。目前有四个（`analog_input_math`、
   `i2c_soft`、`modbus_slave` 以及 `tmp75_test.c` 中的 `tmp75`），没有测试集自动
   运行它们。用主机 gcc 编译，例如：

   ```powershell
   gcc -std=c99 -I ../library/protocol/modbus_slave/inc `
     ../library/protocol/modbus_slave/test/test_modbus_slave.c `
     ../library/protocol/modbus_slave/src/*.c -o build/host/test_modbus_slave.exe
   ```

## 风格

- `.clang-format`：基于 Microsoft，`ColumnLimit: 0`（不自动换行），4 空格缩进，
  `if ( x )` / `while ( x )` 括号内留空格，`int* p`，函数左花括号独占一行。
  clang-format 不在 PATH 上；它随 VS Code C/C++ 扩展安装（当前为
  `ms-vscode.cpptools-1.35.6-win32-x64` 下的 `LLVM/bin/clang-format.exe`）。
- 由于 `ColumnLimit: 0` 不会自动折行，执行格式化之前必须手工把过长的行断开
  （按显示宽度约 100 列）。
- 格式化范围是 `app/`、`board/inc`、`board/src`、`board/ports` 与 `tests/`；
  `board/vendor_wk/`、`RTE_Components.h` 等生成文件保持原样。
- 源码注释与文档一律使用简体中文；许可证/版权声明、厂商生成的代码与 QP 配置
  向导标记保持原有英文。源码文件为 UTF-8 无 BOM，并按文件保留 CRLF/LF。公开函数
  写 Doxygen `@brief`/`@param`/`@return`；结构体成员用 `/**< */`；不使用 `//`。
- 提交标题使用 `type(scope): summary`（如 `feat(board):`、`fix(ports):`）；检出在
  `develop` 分支上。绝不提交 `build/`。
