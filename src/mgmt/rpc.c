/**
 * (C) Copyright 2016-2018 Intel Corporation.
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

struct crt_msg_field *mgmt_pool_create_in_fields[] = {
	&CMF_UUID,		/* pc_pool_uuid */
	&CMF_STRING,		/* pc_grp */
	&CMF_STRING,		/* pc_tgt_dev */
	&CMF_RANK_LIST,		/* pc_tgts */
	&DMF_DAOS_SIZE,		/* pc_scm_size */
	&DMF_DAOS_SIZE,		/* pc_nvme_size */
	&CMF_UINT32,		/* pc_svc_nr */
	&CMF_UINT32,		/* pc_mode */
	&CMF_UINT32,		/* pc_uid */
	&CMF_UINT32,		/* pc_gid */
};

struct crt_msg_field *mgmt_pool_create_out_fields[] = {
	&CMF_RANK_LIST,		/* pc_svc */
	&CMF_INT,		/* pc_rc */
};

struct crt_msg_field *mgmt_pool_destroy_in_fields[] = {
	&CMF_UUID,		/* pd_pool_uuid */
	&CMF_STRING,		/* pd_grp */
	&CMF_INT		/* pd_force */
};

struct crt_msg_field *mgmt_pool_destroy_out_fields[] = {
	&CMF_INT		/* pd_rc */
};

struct crt_msg_field *mgmt_tgt_create_in_fields[] = {
	&CMF_UUID,		/* tc_pool_uuid */
	&CMF_STRING,		/* tc_tgt_dev */
	&DMF_DAOS_SIZE,		/* tc_scm_size */
	&DMF_DAOS_SIZE		/* tc_nvme_size */
};

struct crt_msg_field *mgmt_tgt_create_out_fields[] = {
	&DMF_UUID_ARRAY,	/* tc_tgt_uuid */
	&DMF_UINT32_ARRAY,	/* tc_ranks */
	&CMF_INT,		/* tc_rc */
};

struct crt_msg_field *mgmt_tgt_destroy_in_fields[] = {
	&CMF_UUID		/* td_pool_uuid */
};

struct crt_msg_field *mgmt_tgt_destroy_out_fields[] = {
	&CMF_INT		/* td_rc */
};

struct crt_msg_field *mgmt_svc_rip_in_fields[] = {
	&CMF_UINT32,		/* rip_flags */
};

struct crt_msg_field *mgmt_params_set_in_fields[] = {
	&CMF_UINT32,		/* ps_rank */
	&CMF_UINT32,		/* ps_key_id */
	&CMF_UINT64,		/* ps_value */
	&CMF_UINT64,		/* ps_value_extra */
};

struct crt_msg_field *mgmt_tgt_params_set_in_fields[] = {
	&CMF_UINT64,		/* tps_value */
	&CMF_UINT64,		/* tps_value_extra */
	&CMF_UINT32,		/* tps_key_id */
};

struct crt_msg_field *mgmt_out_fields[] = {
	&CMF_INT,		/* ssp_rc */
};

struct crt_req_format DQF_MGMT_POOL_CREATE =
	DEFINE_CRT_REQ_FMT(mgmt_pool_create_in_fields,
			   mgmt_pool_create_out_fields);

struct crt_req_format DQF_MGMT_POOL_DESTROY =
	DEFINE_CRT_REQ_FMT(mgmt_pool_destroy_in_fields,
			   mgmt_pool_destroy_out_fields);

struct crt_req_format DQF_MGMT_TGT_CREATE =
	DEFINE_CRT_REQ_FMT(mgmt_tgt_create_in_fields,
			   mgmt_tgt_create_out_fields);

struct crt_req_format DQF_MGMT_TGT_DESTROY =
	DEFINE_CRT_REQ_FMT(mgmt_tgt_destroy_in_fields,
			   mgmt_tgt_destroy_out_fields);

struct crt_req_format DQF_MGMT_SVC_RIP =
	DEFINE_CRT_REQ_FMT(mgmt_svc_rip_in_fields, NULL);

struct crt_req_format DQF_MGMT_PARAMS_SET =
	DEFINE_CRT_REQ_FMT(mgmt_params_set_in_fields, mgmt_out_fields);

struct crt_req_format DQF_MGMT_TGT_PARAMS_SET =
	DEFINE_CRT_REQ_FMT(mgmt_tgt_params_set_in_fields, mgmt_out_fields);

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
