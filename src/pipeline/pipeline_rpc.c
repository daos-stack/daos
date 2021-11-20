/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/rpc.h>
#include "pipeline_rpc.h"


CRT_RPC_DEFINE(pipeline_run, DAOS_ISEQ_PIPELINE_RUN, DAOS_OSEQ_PIPELINE_RUN)


#define X(a, b, c, d, e, f)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
},
static struct crt_proto_rpc_format pipeline_proto_rpc_fmt[] = {
	PIPELINE_PROTO_CLI_RPC_LIST
};
#undef X

struct crt_proto_format pipeline_proto_fmt = {
	.cpf_name  = "daos-pipeline",
	.cpf_ver   = DAOS_PIPELINE_VERSION,
	.cpf_count = ARRAY_SIZE(pipeline_proto_rpc_fmt),
	.cpf_prf   = pipeline_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_PIPELINE_MODULE, 0)
};
