/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * ds_rsvc: RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(rsvc)

#include <daos/rpc.h>
#include "rpc.h"

CRT_RPC_DEFINE(rsvc_start, DAOS_ISEQ_RSVC_START, DAOS_OSEQ_RSVC_START)
CRT_RPC_DEFINE(rsvc_stop, DAOS_ISEQ_RSVC_STOP, DAOS_OSEQ_RSVC_STOP)

/* Define for cont_rpcs[] array population below.
 * See RSVC_PROTO_SRV_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format rsvc_proto_rpc_fmt[] = {
	RSVC_PROTO_SRV_RPC_LIST,
};

#undef X

struct crt_proto_format rsvc_proto_fmt = {
	.cpf_name  = "rsvc-proto",
	.cpf_ver   = DAOS_RSVC_VERSION,
	.cpf_count = ARRAY_SIZE(rsvc_proto_rpc_fmt),
	.cpf_prf   = rsvc_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_RSVC_MODULE, 0)
};
