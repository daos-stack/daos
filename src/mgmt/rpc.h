/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#define DAOS_MGMT_VERSION 2
/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define MGMT_PROTO_CLI_RPC_LIST						\
	X(MGMT_SVC_RIP,							\
		DAOS_RPC_NO_REPLY, &CQF_mgmt_svc_rip,			\
		ds_mgmt_hdlr_svc_rip, NULL),				\
	X(MGMT_PARAMS_SET,						\
		0, &CQF_mgmt_params_set,				\
		ds_mgmt_params_set_hdlr, NULL),				\
	X(MGMT_PROFILE,							\
		0, &CQF_mgmt_profile,					\
		ds_mgmt_profile_hdlr, NULL),				\
	X(MGMT_POOL_GET_SVCRANKS,					\
		0, &CQF_mgmt_pool_get_svcranks,				\
		ds_mgmt_pool_get_svcranks_hdlr, NULL),			\
	X(MGMT_POOL_FIND,						\
		0, &CQF_mgmt_pool_find,					\
		ds_mgmt_pool_find_hdlr, NULL),				\
	X(MGMT_MARK,							\
		0, &CQF_mgmt_mark,					\
		ds_mgmt_mark_hdlr, NULL),				\
	X(MGMT_GET_BS_STATE,						\
		0, &CQF_mgmt_get_bs_state,				\
		ds_mgmt_hdlr_get_bs_state, NULL)
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
		ds_mgmt_tgt_profile_hdlr, NULL),			\
	X(MGMT_TGT_MAP_UPDATE,						\
		0, &CQF_mgmt_tgt_map_update,				\
		ds_mgmt_hdlr_tgt_map_update,				\
		&ds_mgmt_hdlr_tgt_map_update_co_ops),			\
	X(MGMT_TGT_MARK,						\
		0, &CQF_mgmt_mark,					\
		ds_mgmt_tgt_mark_hdlr, NULL)



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
	((d_string_t)		(p_path)		CRT_VAR) \
	((int32_t)		(p_avg)			CRT_VAR) \
	((int32_t)		(p_op)			CRT_VAR)

#define DAOS_OSEQ_MGMT_PROFILE /* output fields */	 \
	((int32_t)		(p_rc)			CRT_VAR)

CRT_RPC_DECLARE(mgmt_profile, DAOS_ISEQ_MGMT_PROFILE,
		DAOS_OSEQ_MGMT_PROFILE)

#define DAOS_ISEQ_MGMT_POOL_GET_SVCRANKS /* input fields */	 \
	((uuid_t)		(gsr_puuid)		CRT_VAR)

#define DAOS_OSEQ_MGMT_POOL_GET_SVCRANKS /* output fields */	 \
	((d_rank_list_t)	(gsr_ranks)		CRT_PTR) \
	((int32_t)		(gsr_rc)		CRT_VAR)

CRT_RPC_DECLARE(mgmt_pool_get_svcranks, DAOS_ISEQ_MGMT_POOL_GET_SVCRANKS,
		DAOS_OSEQ_MGMT_POOL_GET_SVCRANKS)

#define DAOS_ISEQ_MGMT_POOL_FIND /* input fields */		 \
	((int32_t)		(pfi_bylabel)		CRT_VAR) \
	((int32_t)		(pfi_pad32)		CRT_VAR) \
	((d_const_string_t)	(pfi_label)		CRT_VAR) \
	((uuid_t)		(pfi_puuid)		CRT_VAR)

#define DAOS_OSEQ_MGMT_POOL_FIND /* output fields */		 \
	((uuid_t)		(pfo_puuid)		CRT_VAR) \
	((d_rank_list_t)	(pfo_ranks)		CRT_PTR) \
	((int32_t)		(pfo_rc)		CRT_VAR)

CRT_RPC_DECLARE(mgmt_pool_find, DAOS_ISEQ_MGMT_POOL_FIND,
		DAOS_OSEQ_MGMT_POOL_FIND)

#define MGMT_POOL_FIND_DUMMY_LABEL "NO LABEL, FINDING BY UUID"

#define DAOS_ISEQ_MGMT_TGT_CREATE /* input fields */		 \
	((uuid_t)		(tc_pool_uuid)		CRT_VAR) \
	((d_string_t)		(tc_tgt_dev)		CRT_VAR) \
	((daos_size_t)		(tc_scm_size)		CRT_VAR) \
	((daos_size_t)		(tc_nvme_size)		CRT_VAR)

#define DAOS_OSEQ_MGMT_TGT_CREATE /* output fields */		   \
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

#define DAOS_SEQ_SERVER_ENTRY \
	((d_rank_t)		(se_rank)		CRT_VAR) \
	((uint16_t)		(se_flags)		CRT_VAR) \
	((uint16_t)		(se_nctxs)		CRT_VAR) \
	((d_string_t)		(se_uri)		CRT_VAR)

CRT_GEN_STRUCT(server_entry, DAOS_SEQ_SERVER_ENTRY);

#define DAOS_ISEQ_MGMT_TGT_MAP_UPDATE /* input fields */	   \
	((struct server_entry)	(tm_servers)		CRT_ARRAY) \
	((uint32_t)		(tm_map_version)	CRT_VAR)

#define DAOS_OSEQ_MGMT_TGT_MAP_UPDATE /* output fields */	 \
	((int32_t)		(tm_rc)			CRT_VAR)

CRT_RPC_DECLARE(mgmt_tgt_map_update, DAOS_ISEQ_MGMT_TGT_MAP_UPDATE,
		DAOS_OSEQ_MGMT_TGT_MAP_UPDATE)

#define DAOS_ISEQ_MGMT_MARK /* input fields */	\
	((d_string_t)		(m_mark)		CRT_VAR)

#define DAOS_OSEQ_MGMT_MARK /* output fields */	\
	((int32_t)		(m_rc)			CRT_VAR)

CRT_RPC_DECLARE(mgmt_mark, DAOS_ISEQ_MGMT_MARK, DAOS_OSEQ_MGMT_MARK)

/* Get Blobstore State */
#define DAOS_ISEQ_MGMT_GET_BS_STATE /* input fields */		 \
	((uuid_t)		(bs_uuid)		CRT_VAR)

#define DAOS_OSEQ_MGMT_GET_BS_STATE /* output fields */		 \
	((int32_t)		(bs_state)		CRT_VAR) \
	((uuid_t)		(bs_uuid)		CRT_VAR) \
	((int32_t)		(bs_rc)			CRT_VAR)

CRT_RPC_DECLARE(mgmt_get_bs_state, DAOS_ISEQ_MGMT_GET_BS_STATE,
		DAOS_OSEQ_MGMT_GET_BS_STATE)

#endif /* __MGMT_RPC_H__ */
