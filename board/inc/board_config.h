#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/**
 * @file board_config.h
 * @brief 板级拓扑与总线事实；编译期开关的唯一归属地。
 *
 * 自包含：不包含其他头文件、不含代码，任何上下文都可安全使用。
 * board.h 重新导出本文件；板级设备头文件与应用层通道模型各自从
 * BOARD_CHANNEL_COUNT 派生语义名称。
 */

/**
 * @brief 唯一的板位列表（8 槽背板布局）。
 *
 * 各层都从该展开式派生自己的按位行：寄存器映射区间表与 KV 默认表
 * （后者即使 SKU 装配的通道更少也保留 8 位键集）。不得存在第二份
 * 手工维护的板位列表。
 */
#define BOARD_POSITIONS(X) X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7)

/**
 * @brief 板位数量，由 BOARD_POSITIONS 自身派生。
 *
 * BOARD_POSITION_ONE 只统计行数，因此数量不会与列表脱节。它必须保
 * 持定义：BOARD_POSITION_COUNT 在每个使用点（包括 #if 表达式）都会
 * 展开它。
 */
#define BOARD_POSITION_ONE(pos) +1U
#define BOARD_POSITION_COUNT    (0U BOARD_POSITIONS(BOARD_POSITION_ONE))

/**
 * @brief 存放参数的 FAL 分区。
 *
 * 由 FAL 分区表（fal_cfg.h）命名，并由 modbus_ao.c 传给
 * fdb_kvdb_init()，两处调用点因此不会相互脱节。
 */
#define BOARD_PARAM_PARTITION "fdb_kvdb"

/**
 * @brief 已装配的通道位置（1..BOARD_POSITION_COUNT）。
 *
 * SKU 开关是预处理宏 SIG24131_BOARD_COUNT=N；未定义时每个位置都已
 * 装配。
 */
#ifndef BOARD_CHANNEL_COUNT
#ifdef SIG24131_BOARD_COUNT
#define BOARD_CHANNEL_COUNT SIG24131_BOARD_COUNT
#else
#define BOARD_CHANNEL_COUNT BOARD_POSITION_COUNT
#endif
#endif

#if ( BOARD_CHANNEL_COUNT < 1U ) \
    || (BOARD_CHANNEL_COUNT > BOARD_POSITION_COUNT)
#error "BOARD_CHANNEL_COUNT must be between 1 and 8"
#endif

/**
 * @brief Modbus RTU 总线波特率，按核心时序计算所假定的值。
 *
 * modbus_slave_board.c 将其提供给从站核心（T3.5 与发送看门狗均由其
 * 派生）；wk_usart.c 中向导生成的 USART1 初始化必须与该值保持同步。
 */
#define BOARD_MODBUS_BAUDRATE 9600U

#endif
