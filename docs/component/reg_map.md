# reg_map — Modbus 数据空间映射（纯函数无状态）

被动模块（非活动对象）：Modbus 四个数据空间的寻址判定与寄存器编解码
（`app/reg_map.h/.c`），由 `AoModbus` 的数据空间回调内联调用。无状态、无定时器、
不使用 QP 工具宏。寄存器**数据定义**（地址/布局/写校验/故障位域位义）见
`docs/ao/modbus_ao.md` §4–§5；类型化镜像、写事务与持久化归 `AoModbus`
（`modbus_ao.md` §6），本文只覆盖纯映射本身。

> 不建独立 AO：纯函数无自主行为、无独立时限，寄存器访问是 AoModbus RTC 步的一
> 部分。独立成模块的理由：互操作规格的函数化——读回调与写事务共用，可独立宿主
> 测试（`tests/test_reg_map.c`）。

## 1. 数据空间与跨度表（地图唯一出处）

`reg_space_t`（coil/discrete/holding/input）、`reg_span_kind_t`（CH_CFG/DEV/
CH_IN/SYS）、`reg_span_t`（space/base/n/kind/channel）。一张按 `space` 分段的
静态 const 表即寄存器地图的唯一出处：holding 段 9 项（8×`CH_CFG` @ 16n +
`DEV` @ 128）、input 段 9 项（8×`CH_IN` @ 8n + `SYS` @ 64..71）、coil/discrete
段**为空**。"空间有无映射"由表回答，产品裁剪零成本。

- **8 位置单源**：通道行由 `board_config.h` 的 `BOARD_POSITIONS` X 宏展开（与 KV
  默认表同源），行数编译期守卫——不存在第二份手维护的位置清单。
- **SKU 装配数过滤**：表描述底板 8 位置全布局，`reg_map_find`（文件内 static）
  把未装配位置（`channel >= CH_CONFIG_CHANNEL_COUNT`）过滤为 EX 02——既有通道地址
  不随装配数变化。
- 字段一律枚举类型直用（类型安全、自文档），表在 ROM。几何常量
  （`REG_MAP_CH_CFG_LEN`、`REG_MAP_DEV_BASE`、`REG_MAP_CH_IN_LEN/_STRIDE`、
  `REG_MAP_SYS_BASE`、`REG_MAP_DEFAULT_DEV_ADDR`）定义在 `reg_map.h`。

## 2. 寻址与异常口径

`reg_map_find(space, start, count)`（文件内 static）：[start, start+count) 整体落在
某跨度内 → 该跨度；否则 NULL。`reg_map_check(...)`：唯一的寻址入口，异常码 + 命中
跨度输出。

| 情形 | 码 | 归属 |
| --- | --- | --- |
| 空间无任何跨度（本产品 coil/discrete） | EX 01（ILLEGAL_FUNCTION） | `reg_map_check` |
| 有跨度但区间不整体落在单跨度内（越界/空档/跨块） | EX 02（ILLEGAL_DATA_ADDRESS） | `reg_map_check` |
| 命中但写值语义非法 | EX 03 | 写事务（`modbus_ao.c` `config_validate`/`addr_valid`） |
| 固化/读回失败 | EX 04 | 写事务（`modbus_ao.c` `save_cfg`/`save_addr`） |

EX 01 口径：空间整体不存在 = 函数不支持（比 EX 02 诚实——不是地址错，是本产品
没有线圈/离散量）。

## 3. 编解码

- **字节序**只在本模块出现一次：u16 经 `modbus_slave_get_u16`/`put_u16`；float32 =
  2 寄存器大端字序（高字在低地址）。
- **通道块 codec**：`reg_map_ch_encode`（类型化 → 10 字）/`reg_map_ch_decode`
  （10 字 → 类型化）。只管布局：类型码 >10 解码为 0xFF 交给语义校验拒绝；子块写
  的读-改-写（encode(当前) → 帧覆盖 → decode(全块)）由写事务编排，任意单寄存器
  写天然正确。往返无损：`decode(encode(x)) == normalize(x)`。
- **设备寄存器 codec**：`reg_map_dev_encode/decode`（u16 直通；范围校验归写事务）。
- **输入区编码**：`reg_map_input_encode(value, status, cjc_temp, fault_word,
  span, start, count, buf)`（值/状态/CJC/故障字逐段拼装，保留槽读 0；无写路径）。
  按 `span->kind` 分派（与保持寄存器路径同式）：`CH_IN` 用 `span->channel` 与
  `addr - span->base` 偏移，`SYS` 用 CJC/故障字；`span` 由 `reg_map_check` 命中
  后传入。

## 4. 故障字合成

`reg_map_fault_word(cjc_valid, latched)`：b0 实时反映冷端有效性，b1..b3 携带调用方
传入的锁存位（`REG_MAP_FAULT_*` 位定义在此），b4..15 恒 0。锁存位的**推导**（b2/b3
从加载终局状态现算，含 `store.db_ok` 守卫与 SKU 掩码）归 `modbus_ao.c`，见
`modbus_ao.md` §5。

已迁出（勿在此找）：`reg_map_status_t` → `app.h` 的 `ch_status_t`；线位→状态码
映射 → `channel_ao.c` 的 `wire_status_code()`；从站地址合法性 → `modbus_ao.c` 的
`addr_valid()`。

验证：`tests/test_reg_map.c`（跨度穷举、编解码往返、输入区布局、故障字）。
