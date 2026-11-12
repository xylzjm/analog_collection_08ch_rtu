# 出厂校准协议

本文只面向生产夹具和工厂固件维护，不属于用户 README。校准通过同一路 RS485 的隐藏保持寄存器块完成，不额外接线。

## 1. 访问规则

隐藏块基地址 `0x0F00`，范围 `0x0F00..0x0F1F`。

- 未解锁时，除 `MAGIC1`/`MAGIC2` 外的块内读写返回异常 02；magic 寄存器读返回 0。
- 两步 magic 必须在 1 s 内完成；写错只清窗口，不改变锁状态。
- 解锁成功后 60 s 无访问自动退出；每次访问隐藏块都会重置计时。
- 任何 FC10 范围只要跨出隐藏块边界，整条请求返回异常 02。
- `CMD` 必须单寄存器写；FC10 覆盖 `0x0F02` 且 count ≠ 1 返回异常 03。
- `CMD` 值不在 1..6 返回异常 03。
- 系数和原始码只读，由固件计算。
- 锁定条件：`CMD=6`、60 s 超时、从站进入 fault。

## 2. 寄存器

| 地址 | 内容 | 属性 |
|---|---|---|
| `0x0F00` | MAGIC1：写 `0xCA11` | 写，读 0 |
| `0x0F01` | MAGIC2：写 `0xB8A7`（1 s 内） | 写，读 0 |
| `0x0F02` | CMD | 写 |
| `0x0F03` | CHANNEL 0..7 | 读写 |
| `0x0F04` | CAL_TYPE 0..3 | 读写 |
| `0x0F05` | STATUS | 读 |
| `0x0F06` | ERROR | 读 |
| `0x0F07..0x0F08` | RAW_LOW int32 | 读 |
| `0x0F09..0x0F0A` | RAW_HIGH int32 | 读 |
| `0x0F0B..0x0F0C` | GAIN int32（×1e6） | 读 |
| `0x0F0D..0x0F0E` | OFFSET int32（原始码） | 读 |
| `0x0F0F` | 当前通道有效位图 | 读 |
| `0x0F10` | 校准模式剩余秒数 | 读 |
| `0x0F11..0x0F1F` | 保留 | 读 0，写异常 02 |

int32 均为高字在前。

## 3. 命令

| CMD | 操作 | 说明 |
|---|---|---|
| 1 | SELECT | 用当前 CHANNEL/CAL_TYPE 选择；未先写两者时判为非法 |
| 2 | LOW | 采低参考点 |
| 3 | HIGH | 采高参考点 |
| 4 | COMMIT | 计算、校验并保存 |
| 5 | ABORT | 放弃当前采点，释放通道，保持解锁 |
| 6 | EXIT | 锁定隐藏块并释放通道 |

CMD=5 会重置 60 s 计时，夹具可直接选择下一个通道/类型。CMD=6 后必须重新发两步 magic。

## 4. 校准类型与参考点

| CAL_TYPE | 含义 | 低点 | 高点 |
|---|---|---|---|
| 0 | 热电偶 mV 通道 | 0 mV | 50 mV |
| 1 | PT100 | 100 Ω | 300 Ω |
| 2 | 4-20 mA | 4 mA | 20 mA |
| 3 | 0-10 V | 0 V | 10 V |

参考点是固件固定常数，夹具输出必须与之一致。热电偶不区分分度号，校的是模拟 mV 通道。

## 5. STATUS

| 值 | 名称 |
|---|---|
| 0 | IDLE |
| 1 | WAITING_CHANNEL |
| 2 | SETTLING |
| 3 | SETTLED |
| 4 | CAPTURING_LOW |
| 5 | LOW_CAPTURED |
| 6 | CAPTURING_HIGH |
| 7 | HIGH_CAPTURED |
| 8 | SAVING |
| 9 | DONE |
| 10 | ERROR |

## 6. ERROR

| 值 | 名称 | 含义 |
|---|---|---|
| 0 | NONE | 无错误 |
| 1 | INVALID_CHANNEL_TYPE | 通道或类型越界 |
| 2 | CHANNEL_NOT_READY | 1 s 内未收到通道就绪 |
| 3 | CAPTURE_FAILED | 采样读取失败或原始码饱和 |
| 4 | SPAN_TOO_SMALL | 两点原始码跨度不足理想跨度的 10% |
| 5 | GAIN_OUT_OF_RANGE | 增益不在 0.5..2.0 |
| 6 | OFFSET_OUT_OF_RANGE | 偏移超出 ±16777216 |
| 7 | SAVE_FAILED | `calN` 写 Flash 失败 |
| 8 | BUSY | 当前状态不允许该命令 |
| 9 | NOT_SELECTED | 未选择通道/类型就采点或提交 |
| 10 | CONFIG_FAILED | `analog_input_configure()` 失败 |
| 11 | REFERENCE_FAILED | 板级参考码反算失败 |

## 7. 采样与系数

- 固定 10 Hz 配置 ADC，与通道用户采样率无关。
- 稳定延迟由 `analog_input_configure()` 返回，再额外等待 50 ms。
- 每点采集 16 个样本，丢弃前 2 个，剩余 14 个取平均。
- 低点完成后才能采高点；两点完成后才能提交。

固件先由板级反算函数得到两个参考点的理想原始码 `ideal_low`、`ideal_high`，再解：

```text
gain_micro  = (ideal_high - ideal_low) * 1000000 / (raw_high - raw_low)
offset_code = ideal_low - gain_micro * raw_low / 1000000
```

校验：

- `abs(raw_high - raw_low)` 不小于 `abs(ideal_high - ideal_low)` 的 10%；
- `gain_micro` 在 500000..2000000；
- `|offset_code| <= 16777216`；
- 原始码在 ±8388608 内。

任一失败：拒绝提交，保留旧系数，只更新 ERROR/STATUS，可重新采点。

## 8. 保存顺序

1. COMMIT 计算并通过校验后，新系数放入组件 `pending`。
2. 组件投递 `CAL_SAVE_SIG`，事件携带通道和 `pending` 指针。
3. `storage_app` 在自己的成员中复制记录，写 `calN`。
4. 保存成功：组件才把 `pending` 提交到 `cal_data`，应用到板级校准表，STATUS=DONE。
5. 保存失败：丢弃新系数，保留旧值，ERROR=SAVE_FAILED；不重试、不置设备故障位。

Flash 擦除期间整机停顿，保存过程中可能丢一帧；夹具应轮询状态并允许超时重试。

## 9. 夹具流程

```text
写 0x0F00 = 0xCA11
写 0x0F01 = 0xB8A7            -> 解锁
写 0x0F03 = channel
写 0x0F04 = cal_type
写 0x0F02 = 1                 -> SELECT，等待 STATUS=SETTLED
输出低参考点，等稳定
写 0x0F02 = 2                 -> 采样低点，等待 LOW_CAPTURED
输出高参考点，等稳定
写 0x0F02 = 3                 -> 采样高点，等待 HIGH_CAPTURED
写 0x0F02 = 4                 -> COMMIT，等待 DONE 或 ERROR
读 0x0F05/0x0F06/0x0F0B..0x0F0E 验证
写 0x0F02 = 5                 -> 可继续下一个通道，保持解锁
写 0x0F02 = 6                 -> 退出并重新锁定
```

断电重启后 `calN` 保持；测量使用新系数。缺失或无效校准记录不占用户故障位，只按标称路径运行。
