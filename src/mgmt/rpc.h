/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
/**
 * Management RPC Protocol Definitions
 */

#ifndef __MGMT_RPC_H__
#define __MGMT_RPC_H__

#include <daos/rpc.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See daos_rpc.h.
 */
#define DAOS_MGMT_VERSION 1
/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define MGMT_PROTO_CLI_RPC_LIST						\
	X(MGMT_POOL_CREATE,						\
		0, &CQF_mgmt_pool_create,				\
		ds_mgmt_hdlr_pool_create, NULL),			\
	X(MGMT_POOL_DESTROY,						\
		0, &CQF_mgmt_pool_destroy,				\
		ds_mgmt_hdlr_pool_destroy, NULL),			\
	X(MGMT_SVC_RIP,							\
		DAOS_RPC_NO_REPLY, &CQF_mgmt_svc_rip,			\
		ds_mgmt_hdlr_svc_rip, NULL),				\
	X(MGMT_PARAMS_SET,						\
		0, &CQF_mgmt_params_set,				\
		ds_mgmt_params_set_hdlr, NULL),				\
	X(MGMT_PROFILE,							\
		0, &CQF_mgmt_profile,					\
		ds_mgmt_profile_hdlr, NULL)

#define MGMT_PROTO_SRV_RPC_LIST						\
	X(MGMT_TGT_CREATE,						\
		0, &CQF_mgmt_tgt_create,				\
		ds_mgmt_hdlr_tgt_create,				\
		&ds_mgmt_hdlr_tgt_create_co_ops),			\
	X(MGMT_TGT_DESTROY,						\
		0, &CQF_mgmt_tgt_destroy,				\
		ds_mgmt_hdlr_tgt_destroy, NULL),			\
	X(MGMT_TGT_PARAMS_SET,						\
		0, &CQF_mgmt_tgt_params_set,				\
		ds_mgmt_tgt_params_set_hdlr, NULL),			\
	X(MGMT_TGT_PROFILE,						\
		0, &CQF_mgmt_profile,					\
		ds_mgmt_tgt_profile_hdlr, NULL)


/* Define for RPC enum population below */
#define X(a, b, c, d, e) a

enum mgmt_operation {
	MGMT_PROTO_CLI_RPC_LIST,
	MGMT_PROTO_CLI_COUNT,
	MGMT_PROTO_CLI_LAST = MGMT_PROTO_CLI_COUNT - 1,
	MGMT_PROTO_SRV_RPC_LIST,
};

#undef X

enum mgmt_profile_op {
	MGMT_PROFILE_START = 1,
	MGMT_PROFILE_STOP
};

extern struct crt_proto_format mgmt_proto_fmt;

#define DAOS_ISEQ_MGMT_POOL_CREATE /* input fields */		 \
	((uuid_t)		(pc_pool_uuid)		CRT_VAR) \
	((d_string_t)		(pc_grp)		CRT_VAR) \
	((d_string_t)		(pc_tgt_dev)		CRT_VAR) \
	((d_rank_list_t)	(pc_tgts)		CRT_PTR) \
	((daos_size_t)		(pc_scm_size)		CRT_VAR) \
	((daos_size_t)		(pc_nvme_size)		CRT_VAR) \
	((daos_prop_t)		(pc_prop)		CRT_PTR) \
	((uint32_t)		(pc_svc_nr)		CRT_VAR) \
	((uint32_t)		(pc_mode)		CRT_VAR) \
	((uint32_t)		(pc_uid)		CRT_VAR) \
	((uint32_t)		(pc_gid)		CRT_VAR)

#define DAOS_OSEQ_MGMT_POOL_CREATE /* output fields */		 \
	((d_rank_list_t)	(pc_svc)		CRT_PTR) \
	((int32_t)		(pc_rc)			CRT_VAR)

CRT_RPC_DECLARE(mgmt_pool_create, DAOS_ISEQ_MGMT_POOL_CREATE,
		DAOS_OSEQ_MGMT_POOL_CREATE)

#define DAOS_ISEQ_MGMT_POOL_DESTROY /* input fields */		 \
	((uuid_t)		(pd_pool_uuid)		CRT_VAR) \
	((d_string_t)		(pd_grp)		CRT_VAR) \
	((uint32_t)		(pd_force)		CRT_VAR)

#define DAOS_OSEQ_MGMT_POOL_DESTROY /* output fields */		 \
	((int32_t)		(pd_rc)			CRT_VAR)

CRT_RPC_DECLARE(mgmt_pool_destroy, DAOS_ISEQ_MGMT_POOL_DESTROY,
		DAOS_OSEQ_MGMT_POOL_DESTROY)

#define DAOS_ISEQ_MGMT_SVR_RIP	/* input fields */		 \
	((uint32_t)		(rip_flags)		CRT_VAR)

#define DAOS_OSEQ_MGMT_SVR_RIP	/* output fields */

CRT_RPC_DECLARE(mgmt_svc_rip, DAOS_ISEQ_MGMT_SVR_RIP, DAOS_OSEQ_MGMT_SVR_RIP)

#define DAOS_ISEQ_MGMT_PARAMS_SET /* input fields */		 \
	((uint32_t)		(ps_rank)		CRT_VAR) \
	((uint32_t)		(ps_key_id)		CRT_VAR) \
	((uint64_t)		(ps_value)		CRT_VAR) \
	((uint64_t)		(ps_value_extra)	CRT_VAR)

#define DAOS_OSEQ_MGMT_PARAMS_SET /* output fields */		 \
	((int32_t)		(srv_rc)		CRT_VAR)

CRT_RPC_DECLARE(mgmt_params_set, DAOS_ISEQ_MGMT_PARAMS_SET,
		DAOS_OSEQ_MGMT_PARAMS_SET)

#define DAOS_ISEQ_MGMT_PROFILE /* input fields */		 \
	((uint64_t)		(p_module)		CRT_VAR) \
	((d_string_t)		(p_path)		CRT_VAR) \
	((int32_t)		(p_op)			CRT_VAR)

#define DAOS_OSEQ_MGMT_PROFILE /* output fields */	 \
	((int32_t)		(p_rc)			CRT_VAR)

CRT_RPC_DECLARE(mgmt_profile, DAOS_ISEQ_MGMT_PROFILE,
		DAOS_OSEQ_MGMT_PROFILE)

#define DAOS_ISEQ_MGMT_TGT_CREATE /* input fields */		 \
	((uuid_t)		(tc_pool_uuid)		CRT_VAR) \
	((d_string_t)		(tc_tgt_dev)		CRT_VAR) \
	((daos_size_t)		(tc_scm_size)		CRT_VAR) \
	((daos_size_t)		(tc_nvme_size)		CRT_VAR)

#define DAOS_OSEQ_MGMT_TGT_CREATE /* output fields */		   \
	((uuid_t)		(tc_tgt_uuids)		CRT_ARRAY) \
	((d_rank_t)		(tc_ranks)		CRT_ARRAY) \
	((int32_t)		(tc_rc)			CRT_VAR)

CRT_RPC_DECLARE(mgmt_tgt_create, DAOS_ISEQ_MGMT_TGT_CREATE,
		DAOS_OSEQ_MGMT_TGT_CREATE)

#define DAOS_ISEQ_MGMT_TGT_DESTROY /* input fields */		 \
	((uuid_t)		(td_pool_uuid)		CRT_VAR)

#define DAOS_OSEQ_MGMT_TGT_DESTROY /* output fields */		 \
	((int32_t)		(td_rc)			CRT_VAR)

CRT_RPC_DECLARE(mgmt_tgt_destroy, DAOS_ISEQ_MGMT_TGT_DESTROY,
		DAOS_OSEQ_MGMT_TGT_DESTROY)

#define DAOS_ISEQ_MGMT_TGT_PARAMS_SET /* input fields */	 \
	((uint64_t)		(tps_value)		CRT_VAR) \
	((uint64_t)		(tps_value_extra)	CRT_VAR) \
	((uint32_t)		(tps_key_id)		CRT_VAR)

#define DAOS_OSEQ_MGMT_TGT_PARAMS_SET /* output fields */	 \
	((int32_t)		(srv_rc)		CRT_VAR)

CRT_RPC_DECLARE(mgmt_tgt_params_set, DAOS_ISEQ_MGMT_TGT_PARAMS_SET,
		DAOS_OSEQ_MGMT_TGT_PARAMS_SET)

#endif /* __MGMT_RPC_H__ */
