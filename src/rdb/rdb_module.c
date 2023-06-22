/**
 * (C) Copyright 2017-2022 Intel Corporation.
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

static int
rdb_get_req_attr(crt_rpc_t *rpc, struct sched_req_attr *attr)
{
	attr->sra_type = SCHED_REQ_ANONYM;

	if (opc_get(rpc->cr_opc) == RDB_APPENDENTRIES) {
		struct rdb_appendentries_in	*in = crt_req_get(rpc);

		/*
		 * AE request with 0 entries is a heartbeat request, we'd inform the
		 * scheduler that the request is periodic, so that scheduler will be
		 * able to ignore it when trying to enter relaxing mode.
		 */
		if (in->aei_msg.n_entries == 0)
			attr->sra_flags |= SCHED_REQ_FL_PERIODIC;
	}

	return 0;
}

static struct dss_module_ops rdb_mod_ops = {
	.dms_get_req_attr = rdb_get_req_attr,
};

struct dss_module rdb_module = {.sm_name        = "rdb",
				.sm_mod_id      = DAOS_RDB_MODULE,
				.sm_ver         = DAOS_RDB_VERSION,
				.sm_proto_count = 1,
				.sm_init        = rdb_module_init,
				.sm_fini        = rdb_module_fini,
				.sm_proto_fmt   = {&rdb_proto_fmt},
				.sm_cli_count   = {0},
				.sm_handlers    = {rdb_handlers},
				.sm_key         = NULL,
				.sm_mod_ops     = &rdb_mod_ops};
