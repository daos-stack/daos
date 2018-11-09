/* Copyright (C) 2016-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * This file is part of CaRT. It gives out the data types internally used by
 * CaRT and not in other specific header files.
 */

#ifndef __CRT_RPC_H__
#define __CRT_RPC_H__

#include <gurt/heap.h>
#include "gurt/common.h"

/* default RPC timeout 60 seconds */
#define CRT_DEFAULT_TIMEOUT_S	(60) /* second */
#define CRT_DEFAULT_TIMEOUT_US	(CRT_DEFAULT_TIMEOUT_S * 1e6) /* micro-second */

/* uri lookup max retry times */
#define CRT_URI_LOOKUP_RETRY_MAX	(8)

extern struct d_binheap_ops crt_timeout_bh_ops;
void crt_hdlr_rank_evict(crt_rpc_t *rpc_req);
extern struct crt_corpc_ops crt_rank_evict_co_ops;
extern void crt_hdlr_memb_sample(crt_rpc_t *rpc_req);

/* RPC flags, these are sent over the wire as part of the protocol so can
 * be set at the origin and read by the target
 */
enum crt_rpc_flags_internal {
	/* flag of collective RPC (bcast) */
	CRT_RPC_FLAG_COLL		= (1U << 16),
	/* flag of targeting primary group */
	CRT_RPC_FLAG_PRIMARY_GRP	= (1U << 17),
};

struct crt_corpc_hdr {
	/* internal group ID */
	uint64_t		 coh_int_grpid;
	/* collective bulk handle */
	crt_bulk_t		 coh_bulk_hdl;
	/* optional excluded ranks */
	d_rank_list_t		*coh_excluded_ranks;
	/* optional inline ranks, for example piggyback the group members */
	d_rank_list_t		*coh_inline_ranks;
	/* group membership version */
	uint32_t		 coh_grp_ver;
	uint32_t		 coh_tree_topo;
	/* root rank of the tree, it is the logical rank within the group */
	uint32_t		 coh_root;
	uint32_t		 coh_padding;
};

/* CaRT layer common header */
struct crt_common_hdr {
	uint32_t	cch_opc;
	/* RPC request flag, see enum crt_rpc_flags_internal */
	uint32_t	cch_flags;
	/* gid and rank identify the rpc request sender */
	d_rank_t	cch_rank;
	/* used in crp_reply_hdr to propagate rpc failure back to sender */
	uint32_t	cch_rc;
};

typedef enum {
	RPC_STATE_INITED = 0x36,
	RPC_STATE_QUEUED, /* queued for flow controlling */
	RPC_STATE_REQ_SENT,
	RPC_STATE_REPLY_RECVED,
	RPC_STATE_COMPLETED,
	RPC_STATE_CANCELED,
	RPC_STATE_TIMEOUT,
	RPC_STATE_ADDR_LOOKUP,
	RPC_STATE_URI_LOOKUP,
	RPC_STATE_FWD_UNREACH,
} crt_rpc_state_t;

/* corpc info to track the tree topo and child RPCs info */
struct crt_corpc_info {
	struct crt_grp_priv	*co_grp_priv;
	d_rank_list_t		*co_excluded_ranks;
	uint32_t		 co_grp_ver;
	uint32_t		 co_tree_topo;
	d_rank_t		 co_root;
	/* the priv passed in crt_corpc_req_create */
	void			*co_priv;
	/* child RPCs list */
	d_list_t		 co_child_rpcs;
	/*
	 * replied child RPC list, when a child RPC being replied and parent
	 * RPC has not been locally handled, we can not aggregate the reply
	 * as it possibly be over-written by local RPC handler. So when child
	 * RPC being replied and parent RPC not finished, the child RPC is
	 * queued at co_replied_rpcs.
	 */
	d_list_t		 co_replied_rpcs;
	uint32_t		 co_child_num;
	uint32_t		 co_child_ack_num;
	uint32_t		 co_child_failed_num;
	/*
	 * co_local_done is the flag of local RPC finish handling
	 * (local reply ready).
	 */
	uint32_t		 co_local_done:1,
	/* co_root_excluded is the flag of root in excluded rank list */
				 co_root_excluded:1,
	/* flag of if refcount taken for co_grp_priv */
				 co_grp_ref_taken:1;
	int			 co_rc;
};

struct crt_rpc_priv {
	/* link to crt_ep_inflight::epi_req_q/::epi_req_waitq */
	d_list_t			crp_epi_link;
	/* tmp_link used in crt_context_req_untrack */
	d_list_t			crp_tmp_link;
	/* link to parent RPC crp_opc_info->co_child_rpcs/co_replied_rpcs */
	d_list_t			crp_parent_link;
	/* binheap node for timeout management, in crt_context::cc_bh_timeout */
	struct d_binheap_node	crp_timeout_bp_node;
	/* the timeout in seconds set by user */
	uint32_t		crp_timeout_sec;
	/* time stamp to be timeout, the key of timeout binheap */
	uint64_t		crp_timeout_ts;
	crt_cb_t		crp_complete_cb;
	void			*crp_arg; /* argument for crp_complete_cb */
	struct crt_ep_inflight	*crp_epi; /* point back to inflight ep */

	crt_rpc_t		crp_pub; /* public part */
	crt_rpc_state_t		crp_state; /* RPC state */
	hg_handle_t		crp_hg_hdl; /* HG request handle */
	hg_addr_t		crp_hg_addr; /* target na address */
	struct crt_hg_hdl	*crp_hdl_reuse; /* reused hg_hdl */
	crt_phy_addr_t		crp_tgt_uri; /* target uri address */
	crt_rpc_t		*crp_ul_req; /* uri lookup request */
	uint32_t		crp_ul_retry; /* uri lookup retry counter */

	/*
	 * RPC request flag, see enum crt_rpc_flags/crt_rpc_flags_internal,
	 * match with crp_req_hdr.cch_flags.
	 */
	uint32_t		crp_flags;
	uint32_t		crp_srv:1, /* flag of server received request */
				crp_output_got:1,
				crp_input_got:1,
				/* flag of collective RPC request */
				crp_coll:1,
				/* flag of crp_tgt_uri need to be freed */
				crp_uri_free:1,
				/* flag of forwarded rpc for corpc */
				crp_forward:1,
				/* flag of in timeout binheap */
				crp_in_binheap:1,
				/* set if a call to crt_req_reply pending */
				crp_reply_pending:1,
				/* set to 1 if target ep is set */
				crp_have_ep:1,
				/* 1 if RPC is succesfully put on the wire */
				crp_on_wire:1;
	uint32_t		crp_refcount;
	struct crt_opc_info	*crp_opc_info;
	/* corpc info, only valid when (crp_coll == 1) */
	struct crt_corpc_info	*crp_corpc_info;
	pthread_spinlock_t	crp_lock;
	struct crt_common_hdr	crp_reply_hdr; /* common header for reply */
	struct crt_common_hdr	crp_req_hdr; /* common header for request */
	struct crt_corpc_hdr	crp_coreq_hdr; /* collective request header */
};

/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define CRT_INTERNAL_RPCS_LIST						\
	X(CRT_OPC_GRP_CREATE,						\
		0, &CQF_crt_grp_create,					\
		crt_hdlr_grp_create, &crt_grp_create_co_ops),		\
	X(CRT_OPC_GRP_DESTROY,						\
		0,  &CQF_crt_grp_destroy,				\
		crt_hdlr_grp_destroy, &crt_grp_destroy_co_ops),		\
	X(CRT_OPC_URI_LOOKUP,						\
		0, &CQF_crt_uri_lookup,					\
		crt_hdlr_uri_lookup, NULL),				\
	X(CRT_OPC_SELF_TEST_BOTH_EMPTY,					\
		0, NULL, crt_self_test_msg_handler, NULL),		\
	X(CRT_OPC_SELF_TEST_SEND_ID_REPLY_IOV,				\
		0,  &CQF_crt_st_send_id_reply_iov,			\
		crt_self_test_msg_handler, NULL),			\
	X(CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY,			\
		0, &CQF_crt_st_send_iov_reply_empty,			\
		crt_self_test_msg_handler, NULL),			\
	X(CRT_OPC_SELF_TEST_BOTH_IOV,					\
		0, &CQF_crt_st_both_iov,				\
		crt_self_test_msg_handler, NULL),			\
	X(CRT_OPC_SELF_TEST_SEND_BULK_REPLY_IOV,			\
		0, &CQF_crt_st_send_bulk_reply_iov,			\
		crt_self_test_msg_handler, NULL),			\
	X(CRT_OPC_SELF_TEST_SEND_IOV_REPLY_BULK,			\
		0, &CQF_crt_st_send_iov_reply_bulk,			\
		crt_self_test_msg_handler, NULL),			\
	X(CRT_OPC_SELF_TEST_BOTH_BULK,					\
		0, &CQF_crt_st_both_bulk,				\
		crt_self_test_msg_handler, NULL),			\
	X(CRT_OPC_SELF_TEST_OPEN_SESSION,				\
		0, &CQF_crt_st_open_session,				\
		crt_self_test_open_session_handler, NULL),		\
	X(CRT_OPC_SELF_TEST_CLOSE_SESSION,				\
		0, &CQF_crt_st_close_session,				\
		crt_self_test_close_session_handler, NULL),		\
	X(CRT_OPC_SELF_TEST_START,					\
		0, &CQF_crt_st_start,					\
		crt_self_test_start_handler, NULL),			\
	X(CRT_OPC_SELF_TEST_STATUS_REQ,					\
		0, &CQF_crt_st_status_req,				\
		crt_self_test_status_req_handler, NULL),		\
	X(CRT_OPC_IV_FETCH,						\
		0, &CQF_crt_iv_fetch, crt_hdlr_iv_fetch, NULL),		\
	X(CRT_OPC_IV_UPDATE,						\
		0, &CQF_crt_iv_update, crt_hdlr_iv_update, NULL),	\
	X(CRT_OPC_IV_SYNC,						\
		0, &CQF_crt_iv_sync,					\
		crt_hdlr_iv_sync, &crt_iv_sync_co_ops),			\
	X(CRT_OPC_BARRIER_ENTER,					\
		0, &CQF_crt_barrier,					\
		crt_hdlr_barrier_enter, &crt_barrier_corpc_ops),	\
	X(CRT_OPC_BARRIER_EXIT,						\
		0, &CQF_crt_barrier,					\
		crt_hdlr_barrier_exit, &crt_barrier_corpc_ops),		\
	X(CRT_OPC_RANK_EVICT,						\
		0, &CQF_crt_lm_evict,					\
		crt_hdlr_rank_evict, &crt_rank_evict_co_ops),		\
	X(CRT_OPC_MEMB_SAMPLE,						\
		0, &CQF_crt_lm_memb_sample,				\
		crt_hdlr_memb_sample, NULL),				\
	X(CRT_OPC_CTL_LS,						\
		0, &CQF_crt_ctl_ep_ls, crt_hdlr_ctl_ls, NULL),		\
	X(CRT_OPC_CTL_GET_HOSTNAME,					\
		0, &CQF_crt_ctl_get_host,				\
		crt_hdlr_ctl_get_hostname, NULL),			\
	X(CRT_OPC_CTL_GET_PID,						\
		0, &CQF_crt_ctl_get_pid, crt_hdlr_ctl_get_pid, NULL),	\
	X(CRT_OPC_PROTO_QUERY,						\
		0, &CQF_crt_proto_query, crt_hdlr_proto_query, NULL)

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a

/* CRT internal opcode definitions, must be 0xFF00xxxx.*/
enum {
	__FIRST  = CRT_PROTO_OPC(CRT_OPC_INTERNAL_BASE, 0, 0) - 1,
	CRT_INTERNAL_RPCS_LIST,
};

#undef X

/* CRT internal RPC definitions */
#define CRT_ISEQ_GRP_CREATE	/* input fields */		 \
	/* user visible grp id (group name) */			 \
	((crt_group_id_t)	(gc_grp_id)		CRT_VAR) \
	/* internal subgrp id */				 \
	((uint64_t)		(gc_int_grpid)		CRT_VAR) \
	((d_rank_list_t)	(gc_membs)		CRT_PTR) \
	/* the rank initiated the group create */		 \
	((d_rank_t)		(gc_initiate_rank)	CRT_VAR)

#define CRT_OSEQ_GRP_CREATE	/* output fields */		 \
	/* failed rank list, can be used to aggregate the reply from child */ \
	((d_rank_list_t)	(gc_failed_ranks)	CRT_PTR) \
	/* the rank sent out the reply */			 \
	((d_rank_t)		(gc_rank)		CRT_VAR) \
	/* return code, if failed the gc_rank should be in gc_failed_ranks */ \
	((int32_t)		(gc_rc)			CRT_VAR)

CRT_RPC_DECLARE(crt_grp_create, CRT_ISEQ_GRP_CREATE, CRT_OSEQ_GRP_CREATE)

#define CRT_ISEQ_GRP_DESTROY	/* input fields */		 \
	((crt_group_id_t)	(gd_grp_id)		CRT_VAR) \
	/* the rank initiated the group destroy */		 \
	((d_rank_t)		(gd_initiate_rank)	CRT_VAR)

#define CRT_OSEQ_GRP_DESTROY	/* output fields */		 \
	/* failed rank list, can be used to aggregate the reply from child */ \
	((d_rank_list_t)	(gd_failed_ranks)	CRT_PTR) \
	/* the rank sent out the reply */			 \
	((d_rank_t)		(gd_rank)		CRT_VAR) \
	/* return code, if failed the gc_rank should be in gc_failed_ranks */ \
	((int32_t)		(gd_rc)			CRT_VAR)

CRT_RPC_DECLARE(crt_grp_destroy, CRT_ISEQ_GRP_DESTROY, CRT_OSEQ_GRP_DESTROY)

#define CRT_ISEQ_URI_LOOKUP	/* input fields */		 \
	((crt_group_id_t)	(ul_grp_id)		CRT_VAR) \
	((d_rank_t)		(ul_rank)		CRT_VAR) \
	((uint32_t)		(ul_tag)		CRT_VAR)

#define CRT_OSEQ_URI_LOOKUP	/* output fields */		 \
	((crt_phy_addr_t)	(ul_uri)		CRT_VAR) \
	((int32_t)		(ul_rc)			CRT_VAR)

CRT_RPC_DECLARE(crt_uri_lookup, CRT_ISEQ_URI_LOOKUP, CRT_OSEQ_URI_LOOKUP)

#define CRT_ISEQ_ST_SEND_ID	/* input fields */		 \
	((uint64_t)		(unused1)		CRT_VAR)

#define CRT_ISEQ_ST_SEND_ID_IOV	/* input fields */		 \
	((uint64_t)		(unused1)		CRT_VAR) \
	((d_iov_t)		(unused2)		CRT_VAR)

#define CRT_ISEQ_ST_SEND_ID_IOV_BULK /* input fields */		 \
	((uint64_t)		(unused1)		CRT_VAR) \
	((d_iov_t)		(unused2)		CRT_VAR) \
	((crt_bulk_t)		(unused3)		CRT_VAR)

#define CRT_ISEQ_ST_SEND_ID_BULK /* input fields */		 \
	((uint64_t)		(unused1)		CRT_VAR) \
	((crt_bulk_t)		(unused2)		CRT_VAR)

#define CRT_OSEQ_ST_REPLY_EMPTY	/* output fields */

#define CRT_OSEQ_ST_REPLY_IOV	/* output fields */		 \
	((d_iov_t)		(unused1)		CRT_VAR)

CRT_RPC_DECLARE(crt_st_send_id_reply_iov,
		CRT_ISEQ_ST_SEND_ID, CRT_OSEQ_ST_REPLY_IOV)

CRT_RPC_DECLARE(crt_st_send_iov_reply_empty,
		CRT_ISEQ_ST_SEND_ID_IOV, CRT_OSEQ_ST_REPLY_EMPTY)

CRT_RPC_DECLARE(crt_st_both_iov,
		CRT_ISEQ_ST_SEND_ID_IOV, CRT_OSEQ_ST_REPLY_IOV)

CRT_RPC_DECLARE(crt_st_send_iov_reply_bulk,
		CRT_ISEQ_ST_SEND_ID_IOV_BULK, CRT_OSEQ_ST_REPLY_EMPTY)

CRT_RPC_DECLARE(crt_st_send_bulk_reply_iov,
		CRT_ISEQ_ST_SEND_ID_BULK, CRT_OSEQ_ST_REPLY_IOV)

CRT_RPC_DECLARE(crt_st_both_bulk,
		CRT_ISEQ_ST_SEND_ID_BULK, CRT_OSEQ_ST_REPLY_EMPTY)

#define CRT_ISEQ_ST_SEND_SESSION /* input fields */		 \
	((uint32_t)		(unused1)		CRT_VAR) \
	((uint32_t)		(unused2)		CRT_VAR) \
	((uint32_t)		(unused3)		CRT_VAR) \
	((uint32_t)		(unused4)		CRT_VAR)

#define CRT_OSEQ_ST_REPLY_ID	/* output fields */		 \
	((uint64_t)		(unused1)		CRT_VAR)

CRT_RPC_DECLARE(crt_st_open_session,
		CRT_ISEQ_ST_SEND_SESSION, CRT_OSEQ_ST_REPLY_ID)

CRT_RPC_DECLARE(crt_st_close_session,
		CRT_ISEQ_ST_SEND_ID, CRT_OSEQ_ST_REPLY_EMPTY)

#define CRT_ISEQ_ST_START	/* input fields */		 \
	((crt_group_id_t)	(unused1)		CRT_VAR) \
	((d_iov_t)		(unused2)		CRT_VAR) \
	((uint32_t)		(unused3)		CRT_VAR) \
	((uint32_t)		(unused4)		CRT_VAR) \
	((uint32_t)		(unused5)		CRT_VAR) \
	((uint32_t)		(unused6)		CRT_VAR) \
	((uint32_t)		(unused7)		CRT_VAR)

#define CRT_OSEQ_ST_START	/* output fields */		 \
	((int32_t)		(unused1)		CRT_VAR)

CRT_RPC_DECLARE(crt_st_start, CRT_ISEQ_ST_START, CRT_OSEQ_ST_START)

#define CRT_ISEQ_ST_STATUS_REQ	/* input fields */		 \
	((crt_bulk_t)		(unused1)		CRT_VAR)

#define CRT_OSEQ_ST_STATUS_REQ	/* output fields */		 \
	((uint64_t)		(test_duration_ns)	CRT_VAR) \
	((uint32_t)		(num_remaining)		CRT_VAR) \
	((int32_t)		(status)		CRT_VAR)

CRT_RPC_DECLARE(crt_st_status_req,
		CRT_ISEQ_ST_STATUS_REQ, CRT_OSEQ_ST_STATUS_REQ)

#define CRT_ISEQ_IV_FETCH	/* input fields */		 \
	/* Namespace ID */					 \
	((d_iov_t)		(ifi_nsid)		CRT_VAR) \
	/* IV Key */						 \
	((d_iov_t)		(ifi_key)		CRT_VAR) \
	/* Bulk handle for iv value */				 \
	((crt_bulk_t)		(ifi_value_bulk)	CRT_VAR) \
	/* Class id */						 \
	((int32_t)		(ifi_class_id)		CRT_VAR) \
	/* Root node for current fetch operation */		 \
	((d_rank_t)		(ifi_root_node)		CRT_VAR)

#define CRT_OSEQ_IV_FETCH	/* output fields */		 \
	((int32_t)		(ifo_rc)		CRT_VAR)

CRT_RPC_DECLARE(crt_iv_fetch, CRT_ISEQ_IV_FETCH, CRT_OSEQ_IV_FETCH)

#define CRT_ISEQ_IV_UPDATE	/* input fields */		 \
	/* IV namespace ID */					 \
	((d_iov_t)		(ivu_nsid)		CRT_VAR) \
	/* IOV for key */					 \
	((d_iov_t)		(ivu_key)		CRT_VAR) \
	/* IOV for sync */					 \
	((d_iov_t)		(ivu_sync_type)		CRT_VAR) \
	/* Bulk handle for iv value */				 \
	((crt_bulk_t)		(ivu_iv_value_bulk)	CRT_VAR) \
	/* Root node for IV UPDATE */				 \
	((d_rank_t)		(ivu_root_node)		CRT_VAR) \
	/* Original node that issued crt_iv_update call */	 \
	((d_rank_t)		(ivu_caller_node)	CRT_VAR) \
	/* Class ID */						 \
	((uint32_t)		(ivu_class_id)		CRT_VAR) \
	((uint32_t)		(padding)		CRT_VAR)

#define CRT_OSEQ_IV_UPDATE	/* output fields */		 \
	((uint64_t)		(rc)			CRT_VAR)

CRT_RPC_DECLARE(crt_iv_update, CRT_ISEQ_IV_UPDATE, CRT_OSEQ_IV_UPDATE)

#define CRT_ISEQ_IV_SYNC	/* input fields */		 \
	/* IV Namespace ID */					 \
	((d_iov_t)		(ivs_nsid)		CRT_VAR) \
	/* IOV for key */					 \
	((d_iov_t)		(ivs_key)		CRT_VAR) \
	/* IOV for sync type */					 \
	((d_iov_t)		(ivs_sync_type)		CRT_VAR) \
	/* IV Class ID */					 \
	((uint32_t)		(ivs_class_id)		CRT_VAR)

#define CRT_OSEQ_IV_SYNC	/* output fields */		 \
	((int32_t)		(rc)			CRT_VAR)

CRT_RPC_DECLARE(crt_iv_sync, CRT_ISEQ_IV_SYNC, CRT_OSEQ_IV_SYNC)

#define CRT_ISEQ_BARRIER	/* input fields */		 \
	((int32_t)		(b_num)			CRT_VAR)

#define CRT_OSEQ_BARRIER	/* output fields */		 \
	((int32_t)		(b_rc)			CRT_VAR)

CRT_RPC_DECLARE(crt_barrier, CRT_ISEQ_BARRIER, CRT_OSEQ_BARRIER)

#define CRT_ISEQ_LM_EVICT	/* input fields */		 \
	((d_rank_t)		(clei_rank)		CRT_VAR) \
	((uint32_t)		(clei_ver)		CRT_VAR)

#define CRT_OSEQ_LM_EVICT	/* output fields */		 \
	((int32_t)		(cleo_succeeded)	CRT_VAR) \
	((int32_t)		(cleo_rc)		CRT_VAR)

CRT_RPC_DECLARE(crt_lm_evict, CRT_ISEQ_LM_EVICT, CRT_OSEQ_LM_EVICT)

#define CRT_ISEQ_LM_MEMB_SAMPLE	/* input fields */		 \
	((uint32_t)		(msi_ver)		CRT_VAR)

#define CRT_OSEQ_LM_MEMB_SAMPLE	/* output fields */		 \
	((d_iov_t)		(mso_delta)		CRT_VAR) \
	((uint32_t)		(mso_ver)		CRT_VAR) \
	((int32_t)		(mso_rc)		CRT_VAR)

CRT_RPC_DECLARE(crt_lm_memb_sample,
		CRT_ISEQ_LM_MEMB_SAMPLE, CRT_OSEQ_LM_MEMB_SAMPLE)

#define CRT_ISEQ_CTL		/* input fields */		 \
	((crt_group_id_t)	(cel_grp_id)		CRT_VAR) \
	((d_rank_t)		(cel_rank)		CRT_VAR)

#define CRT_OSEQ_CTL_EP_LS	/* output fields */		 \
	((d_iov_t)		(cel_addr_str)		CRT_VAR) \
	((int32_t)		(cel_ctx_num)		CRT_VAR) \
	((int32_t)		(cel_rc)		CRT_VAR)

CRT_RPC_DECLARE(crt_ctl_ep_ls, CRT_ISEQ_CTL, CRT_OSEQ_CTL_EP_LS)

#define CRT_OSEQ_CTL_GET_HOST	/* output fields */		 \
	((d_iov_t)		(cgh_hostname)		CRT_VAR) \
	((int32_t)		(cgh_rc)		CRT_VAR)

CRT_RPC_DECLARE(crt_ctl_get_host, CRT_ISEQ_CTL, CRT_OSEQ_CTL_GET_HOST)

#define CRT_OSEQ_CTL_GET_PID	/* output fields */		 \
	((int32_t)		(cgp_pid)		CRT_VAR) \
	((int32_t)		(cgp_rc)		CRT_VAR)

CRT_RPC_DECLARE(crt_ctl_get_pid, CRT_ISEQ_CTL, CRT_OSEQ_CTL_GET_PID)

#define CRT_ISEQ_PROTO_QUERY	/* input fields */		 \
	((d_iov_t)		(pq_ver)		CRT_VAR) \
	((int32_t)		(pq_ver_count)		CRT_VAR) \
	((uint32_t)		(pq_base_opc)		CRT_VAR)

#define CRT_OSEQ_PROTO_QUERY	/* output fields */		 \
	((uint32_t)		(pq_ver)		CRT_VAR) \
	((int32_t)		(pq_rc)			CRT_VAR)

CRT_RPC_DECLARE(crt_proto_query, CRT_ISEQ_PROTO_QUERY, CRT_OSEQ_PROTO_QUERY)

/* CRT internal RPC format definitions */
struct crt_internal_rpc {
	/* Name of the RPC */
	const char		*ir_name;
	/* Operation code associated with the RPC */
	crt_opcode_t		 ir_opc;
	/* RPC version */
	int			 ir_ver;
	/* Operation flags, TBD */
	int			 ir_flags;
	/* RPC request format */
	struct crt_req_format	*ir_req_fmt;
	/* RPC handler */
	crt_rpc_cb_t		 ir_hdlr;
	/* collective ops */
	struct crt_corpc_ops	*ir_co_ops;
};

/* Internal macros for crt_req_(add|dec)ref from within cart.  These take
 * a crt_internal_rpc pointer and provide better logging than the public
 * functions however only work when a private pointer is held.
 */
#define RPC_ADDREF(RPC) do {						\
		int __ref;						\
		D_SPIN_LOCK(&(RPC)->crp_lock);				\
		D_ASSERTF((RPC)->crp_refcount != 0,			\
			  "%p addref from zero\n", (RPC));		\
		__ref = ++(RPC)->crp_refcount;				\
		D_SPIN_UNLOCK(&(RPC)->crp_lock);			\
		RPC_TRACE(DB_NET, RPC, "addref to %d.\n", __ref);	\
	} while (0)

#define RPC_DECREF(RPC) do {						\
		int __ref;						\
		D_SPIN_LOCK(&(RPC)->crp_lock);				\
		D_ASSERTF((RPC)->crp_refcount != 0,			\
			  "%p decref from zero\n", (RPC));		\
		__ref = --(RPC)->crp_refcount;				\
		RPC_TRACE(DB_NET, RPC, "decref to %d.\n", __ref);	\
		D_SPIN_UNLOCK(&(RPC)->crp_lock);			\
		if (__ref == 0)						\
			crt_req_destroy(RPC);				\
	} while (0)

#define RPC_PUB_ADDREF(RPC) do {					\
		struct crt_rpc_priv *_rpc_priv;				\
		D_ASSERT((RPC) != NULL);				\
		_rpc_priv = container_of((RPC), struct crt_rpc_priv, crp_pub); \
		RPC_ADDREF(_rpc_priv);					\
	} while (0)

#define RPC_PUB_DECREF(RPC) do {					\
		struct crt_rpc_priv *_rpc_priv;				\
		D_ASSERT((RPC) != NULL);				\
		_rpc_priv = container_of((RPC), struct crt_rpc_priv, crp_pub); \
		RPC_DECREF(_rpc_priv);					\
	} while (0)

void crt_req_destroy(struct crt_rpc_priv *rpc_priv);

static inline void
crt_common_hdr_init(struct crt_common_hdr *hdr, crt_opcode_t opc)
{
	int rc;

	D_ASSERT(hdr != NULL);
	hdr->cch_opc = opc;
	rc = crt_group_rank(0, &hdr->cch_rank);
	D_ASSERT(rc == 0);
}

static inline bool
crt_rpc_cb_customized(struct crt_context *crt_ctx,
		      crt_rpc_t *rpc_pub)
{
	return crt_ctx->cc_rpc_cb != NULL &&
	       !crt_opcode_reserved_legacy(rpc_pub->cr_opc);
}

/* crt_rpc.c */
int crt_rpc_priv_alloc(crt_opcode_t opc, struct crt_rpc_priv **priv_allocated,
		       bool forward);
void crt_rpc_priv_free(struct crt_rpc_priv *rpc_priv);
int crt_rpc_priv_init(struct crt_rpc_priv *rpc_priv, crt_context_t crt_ctx,
		      bool srv_flag);
void crt_rpc_priv_fini(struct crt_rpc_priv *rpc_priv);
int crt_req_create_internal(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep,
			    crt_opcode_t opc, bool forward, crt_rpc_t **req);
int crt_internal_rpc_register(void);
int crt_rpc_common_hdlr(struct crt_rpc_priv *rpc_priv);
int crt_req_send_internal(struct crt_rpc_priv *rpc_priv);

static inline bool
crt_req_timedout(struct crt_rpc_priv *rpc_priv)
{
	return (rpc_priv->crp_state == RPC_STATE_REQ_SENT ||
		rpc_priv->crp_state == RPC_STATE_URI_LOOKUP ||
		rpc_priv->crp_state == RPC_STATE_ADDR_LOOKUP ||
		rpc_priv->crp_state == RPC_STATE_TIMEOUT ||
		rpc_priv->crp_state == RPC_STATE_FWD_UNREACH) &&
	       !rpc_priv->crp_in_binheap;
}

static inline uint64_t
crt_get_timeout(struct crt_rpc_priv *rpc_priv)
{
	uint32_t	timeout_sec;

	timeout_sec = rpc_priv->crp_timeout_sec > 0 ?
		      rpc_priv->crp_timeout_sec : crt_gdata.cg_timeout;

	return d_timeus_secdiff(timeout_sec);
}

/* crt_corpc.c */
int crt_corpc_req_hdlr(struct crt_rpc_priv *rpc_priv);
void crt_corpc_reply_hdlr(const struct crt_cb_info *cb_info);
int crt_corpc_common_hdlr(struct crt_rpc_priv *rpc_priv);
void crt_corpc_info_fini(struct crt_rpc_priv *rpc_priv);

/* crt_iv.c */
void crt_hdlr_iv_fetch(crt_rpc_t *rpc_req);
void crt_hdlr_iv_update(crt_rpc_t *rpc_req);
void crt_hdlr_iv_sync(crt_rpc_t *rpc_req);
int crt_iv_sync_corpc_aggregate(crt_rpc_t *source, crt_rpc_t *result,
				void *arg);

/* crt_register.c */
int
crt_proto_register_internal(struct crt_proto_format *crf);

#endif /* __CRT_RPC_H__ */
