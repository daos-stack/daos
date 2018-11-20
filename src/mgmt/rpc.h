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
		0, &DQF_MGMT_POOL_CREATE,				\
		ds_mgmt_hdlr_pool_create, NULL),			\
	X(MGMT_POOL_DESTROY,						\
		0, &DQF_MGMT_POOL_DESTROY,				\
		ds_mgmt_hdlr_pool_destroy, NULL),			\
	X(MGMT_SVC_RIP,							\
		DAOS_RPC_NO_REPLY, &DQF_MGMT_SVC_RIP,			\
		ds_mgmt_hdlr_svc_rip, NULL),				\
	X(MGMT_PARAMS_SET,						\
		0, &DQF_MGMT_PARAMS_SET,				\
		ds_mgmt_params_set_hdlr, NULL)

#define MGMT_PROTO_SRV_RPC_LIST						\
	X(MGMT_TGT_CREATE,						\
		0, &DQF_MGMT_TGT_CREATE,				\
		ds_mgmt_hdlr_tgt_create,				\
		&ds_mgmt_hdlr_tgt_create_co_ops),			\
	X(MGMT_TGT_DESTROY,						\
		0, &DQF_MGMT_TGT_DESTROY,				\
		ds_mgmt_hdlr_tgt_destroy, NULL),			\
	X(MGMT_TGT_PARAMS_SET,						\
		0, &DQF_MGMT_TGT_PARAMS_SET,				\
		ds_mgmt_tgt_params_set_hdlr, NULL)

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a

enum mgmt_operation {
	MGMT_PROTO_CLI_RPC_LIST,
	MGMT_PROTO_CLI_COUNT,
	MGMT_PROTO_CLI_LAST = MGMT_PROTO_CLI_COUNT - 1,
	MGMT_PROTO_SRV_RPC_LIST,
};

#undef X

extern struct crt_proto_format mgmt_proto_fmt;

struct mgmt_svc_rip_in {
	uint32_t	rip_flags;
};

struct mgmt_pool_create_in {
	uuid_t			 pc_pool_uuid;
	d_string_t		 pc_grp;
	d_string_t		 pc_tgt_dev;
	d_rank_list_t		*pc_tgts;
	daos_size_t		 pc_scm_size;
	daos_size_t		 pc_nvme_size;
	uint32_t		 pc_svc_nr;
	uint32_t		 pc_mode;
	uint32_t		 pc_uid;
	uint32_t		 pc_gid;
};

struct mgmt_pool_create_out {
	d_rank_list_t	*pc_svc;
	int			 pc_rc;
};

struct mgmt_pool_destroy_in {
	uuid_t			pd_pool_uuid;
	d_string_t		pd_grp;
	int			pd_force;
};

struct mgmt_pool_destroy_out {
	int			pd_rc;
};

struct mgmt_tgt_create_in {
	uuid_t			tc_pool_uuid;
	d_string_t		tc_tgt_dev;
	daos_size_t		tc_scm_size;
	daos_size_t		tc_nvme_size;
};

struct mgmt_tgt_create_out {
	struct crt_array	tc_tgt_uuids;
	struct crt_array	tc_ranks;
	int			tc_rc;
};

struct mgmt_tgt_destroy_in {
	uuid_t			td_pool_uuid;
};

struct mgmt_tgt_destroy_out {
	int			td_rc;
};

struct mgmt_params_set_in {
	uint32_t	ps_rank;
	uint32_t	ps_key_id;
	uint64_t	ps_value;
};

struct mgmt_tgt_params_set_in {
	uint64_t	tps_value;
	uint32_t	tps_key_id;
};

struct mgmt_srv_out {
	int	srv_rc;
};

#endif /* __MGMT_RPC_H__ */
