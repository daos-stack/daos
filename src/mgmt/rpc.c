/**
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/*
 * MGMT RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <daos/rpc.h>
#include "rpc.h"

CRT_GEN_PROC_FUNC(server_entry, DAOS_SEQ_SERVER_ENTRY);

CRT_RPC_DEFINE(mgmt_svc_rip, DAOS_ISEQ_MGMT_SVR_RIP, DAOS_OSEQ_MGMT_SVR_RIP)
CRT_RPC_DEFINE(mgmt_params_set, DAOS_ISEQ_MGMT_PARAMS_SET,
		DAOS_OSEQ_MGMT_PARAMS_SET)
CRT_RPC_DEFINE(mgmt_profile, DAOS_ISEQ_MGMT_PROFILE,
	       DAOS_OSEQ_MGMT_PROFILE)
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
	.cpf_name  = "mgmt-proto",
	.cpf_ver   = DAOS_MGMT_VERSION,
	.cpf_count = ARRAY_SIZE(mgmt_proto_rpc_fmt),
	.cpf_prf   = mgmt_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_MGMT_MODULE, 0)
};
