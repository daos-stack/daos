/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * rebuild: RPC
 *
 * This file contains the RPC format for rebuild.
 */
#define D_LOGFAC       DD_FAC(rebuild)

#include <daos/object.h>
#include <daos/rpc.h>
#include "rpc.h"

static int
crt_proc_daos_unit_oid_t(crt_proc_t proc, crt_proc_op_t proc_op,
			 daos_unit_oid_t *p)
{
	return crt_proc_memcpy(proc, proc_op, p, sizeof(*p));
}

CRT_RPC_DEFINE(rebuild_scan, DAOS_ISEQ_REBUILD_SCAN, DAOS_OSEQ_REBUILD_SCAN)
CRT_RPC_DEFINE(rebuild, DAOS_ISEQ_REBUILD, DAOS_OSEQ_REBUILD)

/* Define for cont_rpcs[] array population below.
 * See REBUILD_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format rebuild_proto_rpc_fmt[] = {
	REBUILD_PROTO_SRV_RPC_LIST,
};

#undef X

struct crt_proto_format rebuild_proto_fmt = {
	.cpf_name  = "rebuild-proto",
	.cpf_ver   = DAOS_REBUILD_VERSION,
	.cpf_count = ARRAY_SIZE(rebuild_proto_rpc_fmt),
	.cpf_prf   = rebuild_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_REBUILD_MODULE, 0)
};
