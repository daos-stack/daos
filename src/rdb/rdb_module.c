/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * rdb: Server Module Interface
 */

#define D_LOGFAC	DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include <daos_srv/daos_engine.h>
#include "rdb_internal.h"

static int
rdb_module_init(void)
{
	return rdb_hash_init();
}

static int
rdb_module_fini(void)
{
	rdb_hash_fini();
	return 0;
}
/* Define for cont_rpcs[] array population below.
 * See RDB_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
}

static struct daos_rpc_handler rdb_handlers[] = {
	RDB_PROTO_SRV_RPC_LIST,
};

#undef X

struct dss_module rdb_module = {
	.sm_name	= "rdb",
	.sm_mod_id	= DAOS_RDB_MODULE,
	.sm_ver		= DAOS_RDB_VERSION,
	.sm_init	= rdb_module_init,
	.sm_fini	= rdb_module_fini,
	.sm_proto_fmt	= &rdb_proto_fmt,
	.sm_cli_count	= 0,
	.sm_handlers	= rdb_handlers,
	.sm_key		= NULL
};
