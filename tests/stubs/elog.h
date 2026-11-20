#ifndef ELOG_H_
#define ELOG_H_

/**
 * @file elog.h
 * @brief EasyLogger 的宿主空操作测试桩，排在中间件副本之前被找到。
 *
 * 宿主测试链接真实的活动对象，而不是日志链路；测试桩让它们的 elog_debug()
 * 诊断仍能编译，却没有任何输出后端。
 */

#define elog_raw(...)   ((void)0)
#define elog_debug(...) ((void)0)
#define elog_info(...)  ((void)0)
#define elog_warn(...)  ((void)0)
#define elog_error(...) ((void)0)

#endif
