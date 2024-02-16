/**
 * (C) Copyright 2015-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_DEBUG_H__
#define __DAOS_DEBUG_H__

#include <stdio.h>
#include <daos_errno.h>

#ifndef DD_FAC
#define DD_FAC(name)	daos_##name##_logfac
#endif /* !DD_FAC */
#ifndef D_LOGFAC
#define D_LOGFAC DD_FAC(daos)
#endif /* !D_LOGFAC */

#include <gurt/debug_setup.h>

/**
 * predefined debug facilities (subsystems/modules), they have to be declared
 * before including any libgurt headers
 */
#define DAOS_FOREACH_LOG_FAC(ACTION, arg)	\
	ACTION(daos,      daos,      arg)	\
	ACTION(array,     array,     arg)	\
	ACTION(kv,        kv,        arg)	\
	ACTION(common,    common,    arg)	\
	ACTION(tree,      tree,      arg)	\
	ACTION(vos,       vos,       arg)	\
	ACTION(client,    client,    arg)	\
	ACTION(server,    server,    arg)	\
	ACTION(rdb,       rdb,       arg)	\
	ACTION(rsvc,      rsvc,      arg)	\
	ACTION(pool,      pool,      arg)	\
	ACTION(container, container, arg)	\
	ACTION(object,    object,    arg)	\
	ACTION(placement, placement, arg)	\
	ACTION(rebuild,   rebuild,   arg)	\
	ACTION(mgmt,      mgmt,      arg)	\
	ACTION(bio,       bio,       arg)	\
	ACTION(tests,     tests,     arg)	\
	ACTION(dfs,       dfs,       arg)	\
	ACTION(duns,      duns,      arg)	\
	ACTION(drpc,      drpc,      arg)	\
	ACTION(security,  security,  arg)	\
	ACTION(dtx,       dtx,       arg)	\
	ACTION(dfuse,     dfuse,     arg)	\
	ACTION(il,        il,        arg)	\
	ACTION(csum,      csum,      arg)	\
	ACTION(pipeline,  pipeline,  arg)	\
	ACTION(stack,     stack,     arg)


#define DAOS_FOREACH_DB(ACTION, arg)				\
	/** metadata operation */				\
	ACTION(DB_MD,	   md,	    metadata,       0, arg)	\
	/** placement operation */				\
	ACTION(DB_PL,	   pl,	    placement,      0, arg)	\
	/** pool operation */					\
	ACTION(DB_MGMT,	   mgmt,    management,	    0, arg)	\
	/** epoch operation */					\
	ACTION(DB_EPC,	   epc,	    epoch,          0, arg)	\
	/** durable format operation */				\
	ACTION(DB_DF,	   df,	    durable_format, 0, arg)	\
	/** rebuild operation */				\
	ACTION(DB_REBUILD, rebuild, rebuild,	    0, arg)	\
	/** security check */					\
	ACTION(DB_SEC,	   sec,	    security,       0, arg)	\
	/** checksum */						\
	ACTION(DB_CSUM,	   csum,    checksum,	    0, arg)

DAOS_FOREACH_DB(D_LOG_DECLARE_DB, D_NOOP);
DAOS_FOREACH_LOG_FAC(D_LOG_DECLARE_FAC, DAOS_FOREACH_DB);

#include <gurt/debug.h>
#include <gurt/common.h>

#define DB_DEFAULT	DLOG_DBG
#define DB_NULL		0
/** XXX Temporary things, should be replaced by debug bits above */
#define DF_MISC		DB_ANY

/** initialize the debug system */
int  daos_debug_init(char *logfile);
/**
 * DAOS-10412
 * need this unnecessary internal API since Go can't see log masks due to
 * no C pre-processor macro support
 */
int  daos_debug_init_ex(char *logfile, d_dbug_t logmask);
void daos_debug_set_id_cb(d_log_id_cb_t id_cb);
/** finalize the debug system */
void daos_debug_fini(void);

/** I/O bypass tunables for performance debugging */
enum {
	IOBP_OFF		= 0,
	/** client RPC is not sent */
	IOBP_CLI_RPC		= (1 << 0),
	/** server ignores bulk transfer (garbage data is stored) */
	IOBP_SRV_BULK		= (1 << 1),
	/** bypass target I/O, no VOS and BIO at all */
	IOBP_TARGET		= (1 << 2),
	/** server does not store bulk data in NVMe (drop it) */
	IOBP_NVME		= (1 << 3),
	/** bypass bulk handle cache */
	IOBP_SRV_BULK_CACHE	= (1 << 4),
	/** bypass WAL commit */
	IOBP_WAL_COMMIT		= (1 << 5),
};

/**
 * This environment is mostly for performance debugging, it can be set to
 * combination of strings below, invalid combination will be ignored.
 */
#define DENV_IO_BYPASS		"DAOS_IO_BYPASS"

#define IOBP_ENV_CLI_RPC	"cli_rpc"
#define IOBP_ENV_SRV_BULK	"srv_bulk"
#define IOBP_ENV_TARGET		"target"
#define IOBP_ENV_NVME		"nvme"
#define IOBP_ENV_SRV_BULK_CACHE	"srv_bulk_cache"
#define IOBP_ENV_WAL_COMMIT	"wal_commit"

extern unsigned int daos_io_bypass;

#endif /* __DAOS_DEBUG_H__ */
