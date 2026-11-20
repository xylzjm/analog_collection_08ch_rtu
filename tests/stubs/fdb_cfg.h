#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

/* FlashDB 配置的宿主替身；有意留空。宿主构建针对这个仅含守卫的文件编译真实
 * 厂商头文件，因此 FDB_USING_FAL_MODE 保持未定义，flashdb.h 也不会引入 fal.h。
 * 板级的 fdb_cfg.h 有意不放入宿主包含路径。 */

#endif
