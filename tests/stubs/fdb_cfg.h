#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

/* Host stand-in for the FlashDB configuration; empty on purpose. The host
 * build compiles the real vendor headers against this guard-only file, so
 * FDB_USING_FAL_MODE stays undefined and flashdb.h never pulls fal.h. The
 * board's fdb_cfg.h is deliberately not on the host include path. */

#endif
