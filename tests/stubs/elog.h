#ifndef ELOG_H_
#define ELOG_H_

/**
 * @file elog.h
 * @brief Host no-op stub for EasyLogger, found before the middleware copy.
 *
 * The host tests link the real active objects, not the logging chain; the
 * stub keeps their elog_debug() diagnostics compiling without any output
 * backend.
 */

#define elog_raw(...)   ((void)0)
#define elog_debug(...) ((void)0)
#define elog_info(...)  ((void)0)
#define elog_warn(...)  ((void)0)
#define elog_error(...) ((void)0)

#endif
