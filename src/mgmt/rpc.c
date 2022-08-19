/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * MGMT RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <daos/rpc.h>
#include "rpc.h"

CRT_GEN_PROC_FUNC(server_entry, DAOS_SEQ_SERVER_ENTRY);

static int
crt_proc_struct_server_entry(crt_proc_t proc, crt_proc_op_t proc_op,
			     struct server_entry *data)
{
	return crt_proc_server_entry(proc, data);
}

CRT_RPC_DEFINE(mgmt_svc_rip, DAOS_ISEQ_MGMT_SVR_RIP, DAOS_OSEQ_MGMT_SVR_RIP)
CRT_RPC_DEFINE(mgmt_params_set, DAOS_ISEQ_MGMT_PARAMS_SET,
		DAOS_OSEQ_MGMT_PARAMS_SET)
CRT_RPC_DEFINE(mgmt_profile, DAOS_ISEQ_MGMT_PROFILE,
	       DAOS_OSEQ_MGMT_PROFILE)
CRT_RPC_DEFINE(mgmt_pool_get_svcranks, DAOS_ISEQ_MGMT_POOL_GET_SVCRANKS,
	       DAOS_OSEQ_MGMT_POOL_GET_SVCRANKS)
CRT_RPC_DEFINE(mgmt_pool_find, DAOS_ISEQ_MGMT_POOL_FIND,
	       DAOS_OSEQ_MGMT_POOL_FIND)
CRT_RPC_DEFINE(mgmt_mark, DAOS_ISEQ_MGMT_MARK,
	       DAOS_OSEQ_MGMT_MARK)

CRT_RPC_DEFINE(mgmt_tgt_create, DAOS_ISEQ_MGMT_TGT_CREATE,
		DAOS_OSEQ_MGMT_TGT_CREATE)
CRT_RPC_DEFINE(mgmt_tgt_destroy, DAOS_ISEQ_MGMT_TGT_DESTROY,
		DAOS_OSEQ_MGMT_TGT_DESTROY)
CRT_RPC_DEFINE(mgmt_tgt_params_set, DAOS_ISEQ_MGMT_TGT_PARAMS_SET,
		DAOS_OSEQ_MGMT_TGT_PARAMS_SET)
CRT_RPC_DEFINE(mgmt_tgt_map_update, DAOS_ISEQ_MGMT_TGT_MAP_UPDATE,
		DAOS_OSEQ_MGMT_TGT_MAP_UPDATE)

CRT_RPC_DEFINE(mgmt_get_bs_state, DAOS_ISEQ_MGMT_GET_BS_STATE,
	       DAOS_OSEQ_MGMT_GET_BS_STATE)

CRT_RPC_DEFINE(mgmt_tgt_shard_destroy, DAOS_ISEQ_MGMT_TGT_SHARD_DESTROY,
	       DAOS_OSEQ_MGMT_TGT_SHARD_DESTROY)

/* Define for cont_rpcs[] array population below.
 * See MGMT_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format mgmt_proto_rpc_fmt[] = {
	MGMT_PROTO_CLI_RPC_LIST,
	MGMT_PROTO_SRV_RPC_LIST,
};

#undef X

struct crt_proto_format mgmt_proto_fmt = {
	.cpf_name  = "management",
	.cpf_ver   = DAOS_MGMT_VERSION,
	.cpf_count = ARRAY_SIZE(mgmt_proto_rpc_fmt),
	.cpf_prf   = mgmt_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_MGMT_MODULE, 0)
};
