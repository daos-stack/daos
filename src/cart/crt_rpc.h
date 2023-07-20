/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
void crt_hdlr_memb_sample(crt_rpc_t *rpc_req);

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
	/* internal group ID name */
	d_string_t		 coh_grpid;
	/* collective bulk handle */
	crt_bulk_t		 coh_bulk_hdl;
	/* optional filter ranks (see crt_corpc_req_create) */
	d_rank_list_t		*coh_filter_ranks;
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
	/* HLC timestamp */
	uint64_t	cch_hlc;
	/* RPC id */
	uint64_t	cch_rpcid;
	/* destination rank in default primary group */
	d_rank_t	cch_dst_rank;
	/* originator rank in default primary group */
	d_rank_t	cch_src_rank;
	/* destination tag */
	uint32_t	cch_dst_tag;


	/* used in crp_reply_hdr to propagate rpc failure back to sender */
	/* TODO: workaround for DAOS-13973 */
	union {
		uint32_t	cch_src_timeout;
		uint32_t	cch_rc;
	};
};


typedef enum {
	RPC_STATE_INITED = 0x36,
	RPC_STATE_QUEUED, /* queued for flow controlling */
	RPC_STATE_REQ_SENT,
	RPC_STATE_COMPLETED,
	RPC_STATE_CANCELED,
	RPC_STATE_TIMEOUT,
	RPC_STATE_URI_LOOKUP,
	RPC_STATE_FWD_UNREACH,
} crt_rpc_state_t;

/* corpc info to track the tree topo and child RPCs info */
struct crt_corpc_info {
	struct crt_grp_priv	*co_grp_priv;
	/* filter ranks (see crt_corpc_req_create) */
	d_rank_list_t		*co_filter_ranks;
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
	crt_rpc_t		crp_pub; /* public part */
	/* link to crt_ep_inflight::epi_req_q/::epi_req_waitq */
	d_list_t		crp_epi_link;
	/* tmp_link used in crt_context_req_untrack */
	d_list_t		crp_tmp_link;
	/* link to parent RPC crp_opc_info->co_child_rpcs/co_replied_rpcs */
	d_list_t		crp_parent_link;
	/* binheap node for timeout management, in crt_context::cc_bh_timeout */
	struct d_binheap_node	crp_timeout_bp_node;
	/* the timeout in seconds set by user */
	uint32_t		crp_timeout_sec;
	/* time stamp to be timeout, the key of timeout binheap */
	uint64_t		crp_timeout_ts;
	crt_cb_t		crp_complete_cb;
	void			*crp_arg; /* argument for crp_complete_cb */
	struct crt_ep_inflight	*crp_epi; /* point back to in-flight ep */

	ATOMIC uint32_t		crp_refcount;
	crt_rpc_state_t		crp_state; /* RPC state */
	hg_handle_t		crp_hg_hdl; /* HG request handle */
	hg_addr_t		crp_hg_addr; /* target na address */
	struct crt_hg_hdl	*crp_hdl_reuse; /* reused hg_hdl */
	crt_phy_addr_t		crp_tgt_uri; /* target uri address */
	crt_rpc_t		*crp_ul_req; /* uri lookup request */

	uint32_t		crp_ul_retry; /* uri lookup retry counter */

	int			crp_ul_idx; /* index last tried */

	struct crt_grp_priv	*crp_grp_priv; /* group private pointer */
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
				/* RPC is tracked by the context */
				crp_ctx_tracked:1,
				/* 1 if RPC fails HLC epsilon check */
				crp_fail_hlc:1,
				/* RPC completed flag */
				crp_completed:1,
				/* RPC originated from a primary provider */
				crp_src_is_primary:1;

	struct crt_opc_info	*crp_opc_info;
	/* corpc info, only valid when (crp_coll == 1) */
	struct crt_corpc_info	*crp_corpc_info;
	pthread_spinlock_t	crp_lock;
	/*
	 * Prevent data races on most crt_rpc_priv fields from crt_req_send,
	 * crt_hg_req_send_cb, uri_lookup_cb, and crt_req_timeout_hdlr. The
	 * following fine-to-coarse lock order shall be followed:
	 *
	 *   crt_rpc_priv.crp_mutex
	 *   crt_ep_inflight.epi_mutex
	 *   crt_context.cc_mutex
	 *   crt_gdata.cg_rwlock
	 */
	pthread_mutex_t		crp_mutex;
	struct crt_common_hdr	crp_reply_hdr; /* common header for reply */
	struct crt_common_hdr	crp_req_hdr; /* common header for request */
	struct crt_corpc_hdr	crp_coreq_hdr; /* collective request header */
};

static inline void
crt_rpc_lock(struct crt_rpc_priv *rpc_priv)
{
	D_MUTEX_LOCK(&rpc_priv->crp_mutex);
}

static inline void
crt_rpc_unlock(struct crt_rpc_priv *rpc_priv)
{
	D_MUTEX_UNLOCK(&rpc_priv->crp_mutex);
}

#define CRT_PROTO_INTERNAL_VERSION 4
#define CRT_PROTO_FI_VERSION 3
#define CRT_PROTO_ST_VERSION 1
#define CRT_PROTO_CTL_VERSION 1
#define CRT_PROTO_IV_VERSION 1

/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 * TODO: CRT_OPC_CTL_LS should be in the ctl protocol however cart_ctl uses
 * this to ping the server waiting for start so needs to work before
 * proto_query() can be called.
 */
#define CRT_INTERNAL_RPCS_LIST						\
	X(CRT_OPC_URI_LOOKUP,						\
		0, &CQF_crt_uri_lookup,					\
		crt_hdlr_uri_lookup, NULL)				\
	X(CRT_OPC_PROTO_QUERY,						\
		0, &CQF_crt_proto_query,				\
		crt_hdlr_proto_query, NULL)				\
	X(CRT_OPC_CTL_LS,						\
		0, &CQF_crt_ctl_ep_ls,					\
		crt_hdlr_ctl_ls, NULL)					\

#define CRT_FI_RPCS_LIST						\
	X(CRT_OPC_CTL_FI_TOGGLE,					\
		0, &CQF_crt_ctl_fi_toggle,				\
		crt_hdlr_ctl_fi_toggle, NULL)				\
	X(CRT_OPC_CTL_FI_SET_ATTR,					\
		0, &CQF_crt_ctl_fi_attr_set,				\
		crt_hdlr_ctl_fi_attr_set, NULL)				\

#define CRT_ST_RPCS_LIST						\
	X(CRT_OPC_SELF_TEST_BOTH_EMPTY,					\
		0, NULL,						\
		crt_self_test_msg_handler, NULL)			\
	X(CRT_OPC_SELF_TEST_SEND_ID_REPLY_IOV,				\
		0, &CQF_crt_st_send_id_reply_iov,			\
		crt_self_test_msg_handler, NULL)			\
	X(CRT_OPC_SELF_TEST_SEND_IOV_REPLY_EMPTY,			\
		0, &CQF_crt_st_send_iov_reply_empty,			\
		crt_self_test_msg_handler, NULL)			\
	X(CRT_OPC_SELF_TEST_BOTH_IOV,					\
		0, &CQF_crt_st_both_iov,				\
		crt_self_test_msg_handler, NULL)			\
	X(CRT_OPC_SELF_TEST_SEND_BULK_REPLY_IOV,			\
		0, &CQF_crt_st_send_bulk_reply_iov,			\
		crt_self_test_msg_handler, NULL)			\
	X(CRT_OPC_SELF_TEST_SEND_IOV_REPLY_BULK,			\
		0, &CQF_crt_st_send_iov_reply_bulk,			\
		crt_self_test_msg_handler, NULL)			\
	X(CRT_OPC_SELF_TEST_BOTH_BULK,					\
		0, &CQF_crt_st_both_bulk,				\
		crt_self_test_msg_handler, NULL)			\
	X(CRT_OPC_SELF_TEST_OPEN_SESSION,				\
		0, &CQF_crt_st_open_session,				\
		crt_self_test_open_session_handler, NULL)		\
	X(CRT_OPC_SELF_TEST_CLOSE_SESSION,				\
		0, &CQF_crt_st_close_session,				\
		crt_self_test_close_session_handler, NULL)		\
	X(CRT_OPC_SELF_TEST_START,					\
		0, &CQF_crt_st_start,					\
		crt_self_test_start_handler, NULL)			\
	X(CRT_OPC_SELF_TEST_STATUS_REQ,					\
		0, &CQF_crt_st_status_req,				\
		crt_self_test_status_req_handler, NULL)			\

#define CRT_CTL_RPCS_LIST						\
	X(CRT_OPC_CTL_LOG_SET,						\
		0, &CQF_crt_ctl_log_set,				\
		crt_hdlr_ctl_log_set, NULL)				\
	X(CRT_OPC_CTL_LOG_ADD_MSG,					\
		0, &CQF_crt_ctl_log_add_msg,				\
		crt_hdlr_ctl_log_add_msg, NULL)				\
	X(CRT_OPC_CTL_GET_URI_CACHE,					\
		0, &CQF_crt_ctl_get_uri_cache,				\
		crt_hdlr_ctl_get_uri_cache, NULL)			\
	X(CRT_OPC_CTL_GET_HOSTNAME,					\
		0, &CQF_crt_ctl_get_host,				\
		crt_hdlr_ctl_get_hostname, NULL)			\
	X(CRT_OPC_CTL_GET_PID,						\
		0, &CQF_crt_ctl_get_pid,				\
		crt_hdlr_ctl_get_pid, NULL)				\

#define CRT_IV_RPCS_LIST						\
	X(CRT_OPC_IV_FETCH,						\
		0, &CQF_crt_iv_fetch,					\
		crt_hdlr_iv_fetch, NULL)				\
	X(CRT_OPC_IV_UPDATE,						\
		0, &CQF_crt_iv_update,					\
		crt_hdlr_iv_update, NULL)				\
	X(CRT_OPC_IV_SYNC,						\
		0, &CQF_crt_iv_sync,					\
		crt_hdlr_iv_sync, &crt_iv_sync_co_ops)			\

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a,

/* CRT internal opcode definitions, must be 0xFF00xxxx.*/
#define CRT_OPC_INTERNAL_BASE	0xFF000000UL
enum {
	__FIRST_INTERNAL  = CRT_PROTO_OPC(CRT_OPC_INTERNAL_BASE,
					  CRT_PROTO_INTERNAL_VERSION, 0) - 1,
	CRT_INTERNAL_RPCS_LIST
};

/* CRT fault injection opcode definitions, must be 0xFF00xxxx.*/
#define CRT_OPC_FI_BASE		0xF1000000UL
enum {
	__FIRST_FI  = CRT_PROTO_OPC(CRT_OPC_FI_BASE,
				    CRT_PROTO_FI_VERSION, 0) - 1,
	CRT_FI_RPCS_LIST
};

/* CRT self-test opcode definitions, must be 0xFF00xxxx.*/
#define CRT_OPC_ST_BASE		0xF2000000UL
enum {
	__FIRST_ST  = CRT_PROTO_OPC(CRT_OPC_ST_BASE,
				    CRT_PROTO_ST_VERSION, 0) - 1,
	CRT_ST_RPCS_LIST
};

/* CRT ctl opcode definitions, must be 0xFF00xxxx.*/
#define CRT_OPC_CTL_BASE		0xF3000000UL
enum {
	__FIRST_CTL  = CRT_PROTO_OPC(CRT_OPC_CTL_BASE,
				     CRT_PROTO_CTL_VERSION, 0) - 1,
	CRT_CTL_RPCS_LIST
};

/* CRT IV opcode definitions, must be 0xFF00xxxx.*/
#define CRT_OPC_IV_BASE		0xF4000000UL
enum {
	__FIRST_IV  = CRT_PROTO_OPC(CRT_OPC_IV_BASE,
				    CRT_PROTO_IV_VERSION, 0) - 1,
	CRT_IV_RPCS_LIST
};

#define CRT_OPC_SWIM_BASE	0xFE000000UL

#undef X

static inline bool
crt_opc_is_swim(crt_opcode_t opc)
{
	return ((opc & CRT_PROTO_BASEOPC_MASK) == CRT_OPC_SWIM_BASE);
}

#define CRT_SEQ_GRP_CACHE					 \
	((d_rank_t)		(gc_rank)		CRT_VAR) \
	((uint32_t)		(gc_tag)		CRT_VAR) \
	((d_string_t)		(gc_uri)		CRT_VAR)

CRT_GEN_STRUCT(crt_grp_cache, CRT_SEQ_GRP_CACHE)

#define CRT_ISEQ_URI_LOOKUP	/* input fields */		 \
	((crt_group_id_t)	(ul_grp_id)		CRT_VAR) \
	((d_rank_t)		(ul_rank)		CRT_VAR) \
	((uint32_t)		(ul_tag)		CRT_VAR)

#define CRT_OSEQ_URI_LOOKUP	/* output fields */		 \
	((crt_phy_addr_t)	(ul_uri)		CRT_VAR) \
	((uint32_t)		(ul_tag)		CRT_VAR) \
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
	((uint32_t)		(ifi_ivns_id)		CRT_VAR) \
	((uint32_t)		(ifi_grp_ver)		CRT_VAR) \
	((crt_group_id_t)	(ifi_ivns_group)	CRT_VAR) \
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
	((uint32_t)		(ivu_ivns_id)		CRT_VAR) \
	((uint32_t)		(ivu_grp_ver)		CRT_VAR) \
	((crt_group_id_t)	(ivu_ivns_group)	CRT_VAR) \
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
	((uint32_t)		(ivs_ivns_id)		CRT_VAR) \
	((uint32_t)		(ivs_grp_ver)		CRT_VAR) \
	((crt_group_id_t)	(ivs_ivns_group)	CRT_VAR) \
	/* IOV for key */					 \
	((d_iov_t)		(ivs_key)		CRT_VAR) \
	/* IOV for sync type */					 \
	((d_iov_t)		(ivs_sync_type)		CRT_VAR) \
	/* IV Class ID */					 \
	((uint32_t)		(ivs_class_id)		CRT_VAR)

#define CRT_OSEQ_IV_SYNC	/* output fields */		 \
	((int32_t)		(rc)			CRT_VAR)

CRT_RPC_DECLARE(crt_iv_sync, CRT_ISEQ_IV_SYNC, CRT_OSEQ_IV_SYNC)

#define CRT_ISEQ_CTL		/* input fields */		 \
	((crt_group_id_t)	(cel_grp_id)		CRT_VAR) \
	((d_rank_t)		(cel_rank)		CRT_VAR)

#define CRT_OSEQ_CTL_EP_LS	/* output fields */		 \
	((d_iov_t)		(cel_addr_str)		CRT_VAR) \
	((int32_t)		(cel_ctx_num)		CRT_VAR) \
	((int32_t)		(cel_rc)		CRT_VAR)

CRT_RPC_DECLARE(crt_ctl_ep_ls, CRT_ISEQ_CTL, CRT_OSEQ_CTL_EP_LS)

#define CRT_OSEQ_CTL_GET_URI_CACHE /* output fields */		 \
	((struct crt_grp_cache)	(cguc_grp_cache)	CRT_ARRAY) \
	((int32_t)		(cguc_rc)		CRT_VAR)

CRT_RPC_DECLARE(crt_ctl_get_uri_cache, CRT_ISEQ_CTL, CRT_OSEQ_CTL_GET_URI_CACHE)

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

#define CRT_ISEQ_CTL_FI_ATTR_SET	/* input fields */	 \
	((uint32_t)		(fa_fault_id)		CRT_VAR) \
	((uint32_t)		(fa_interval)		CRT_VAR) \
	((uint64_t)		(fa_max_faults)		CRT_VAR) \
	((uint32_t)		(fa_err_code)		CRT_VAR) \
	((uint32_t)		(fa_probability_x)	CRT_VAR) \
	((d_string_t)		(fa_argument)		CRT_VAR) \
	((uint32_t)		(fa_probability_y)	CRT_VAR)

#define CRT_OSEQ_CTL_FI_ATTR_SET	/* output fields */	 \
	((int32_t)		(fa_ret)		CRT_VAR)

CRT_RPC_DECLARE(crt_ctl_fi_attr_set, CRT_ISEQ_CTL_FI_ATTR_SET,
		CRT_OSEQ_CTL_FI_ATTR_SET)

#define CRT_ISEQ_CTL_FI_TOGGLE		/* input fields */	 \
	((bool)		(op)			CRT_VAR)

#define CRT_OSEQ_CTL_FI_TOGGLE		/* output fields */	 \
	((int32_t)		(rc)			CRT_VAR)

CRT_RPC_DECLARE(crt_ctl_fi_toggle,
		CRT_ISEQ_CTL_FI_TOGGLE, CRT_OSEQ_CTL_FI_TOGGLE)

#define CRT_ISEQ_CTL_LOG_SET		/* input fields */	\
	((d_string_t)		(log_mask)	CRT_VAR)

#define CRT_OSEQ_CTL_LOG_SET		/* output fields */	\
	((int32_t)		(rc)		CRT_VAR)

CRT_RPC_DECLARE(crt_ctl_log_set, CRT_ISEQ_CTL_LOG_SET, CRT_OSEQ_CTL_LOG_SET)

#define CRT_ISEQ_CTL_LOG_ADD_MSG	/* input fields */	\
	((d_string_t)		(log_msg)	CRT_VAR)

#define CRT_OSEQ_CTL_LOG_ADD_MSG	/* output fields */	\
	((int32_t)		(rc)		CRT_VAR)

CRT_RPC_DECLARE(crt_ctl_log_add_msg, CRT_ISEQ_CTL_LOG_ADD_MSG,
		CRT_OSEQ_CTL_LOG_ADD_MSG)

/* Internal macros for crt_req_(add|dec)ref from within cart.  These take
 * a crt_internal_rpc pointer and provide better logging than the public
 * functions however only work when a private pointer is held.
 *
 * Note that we conservatively use the default, strict memory order for both
 * RPC_ADDREF and RPC_DECREF, leaving relaxations to future work.
 */
#define RPC_ADDREF(RPC) do {						\
		int __ref;						\
		__ref = atomic_fetch_add(&(RPC)->crp_refcount, 1);	\
		D_ASSERTF(__ref != 0, "%p addref from zero\n", (RPC));	\
		RPC_TRACE(DB_NET, RPC, "addref to %u.\n", __ref + 1);	\
	} while (0)

#define RPC_DECREF(RPC) do {						\
		int __ref;						\
		__ref = atomic_fetch_sub(&(RPC)->crp_refcount, 1);	\
		D_ASSERTF(__ref != 0, "%p decref from zero\n", (RPC));	\
		RPC_TRACE(DB_NET, RPC, "decref to %u.\n", __ref - 1);	\
		if (__ref == 1)						\
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

static inline bool
crt_rpc_cb_customized(struct crt_context *crt_ctx,
		      crt_rpc_t *rpc_pub)
{
	return crt_ctx->cc_rpc_cb != NULL;
}

/* crt_rpc.c */
int crt_rpc_priv_alloc(crt_opcode_t opc, struct crt_rpc_priv **priv_allocated,
		       bool forward);
void crt_rpc_priv_free(struct crt_rpc_priv *rpc_priv);
void crt_rpc_priv_init(struct crt_rpc_priv *rpc_priv, crt_context_t crt_ctx, bool srv_flag);
void crt_rpc_priv_fini(struct crt_rpc_priv *rpc_priv);
int crt_req_create_internal(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep,
			    crt_opcode_t opc, bool forward, crt_rpc_t **req);
int crt_internal_rpc_register(bool server);
int crt_rpc_common_hdlr(struct crt_rpc_priv *rpc_priv);
int crt_req_send_internal(struct crt_rpc_priv *rpc_priv);

static inline bool
crt_req_timedout(struct crt_rpc_priv *rpc_priv)
{
	return (rpc_priv->crp_state == RPC_STATE_REQ_SENT ||
		rpc_priv->crp_state == RPC_STATE_URI_LOOKUP ||
		rpc_priv->crp_state == RPC_STATE_TIMEOUT ||
		rpc_priv->crp_state == RPC_STATE_FWD_UNREACH) &&
	       !rpc_priv->crp_in_binheap;
}

static inline void
crt_set_timeout(struct crt_rpc_priv *rpc_priv)
{
	D_ASSERT(rpc_priv != NULL);

	if (rpc_priv->crp_timeout_sec == 0)
		rpc_priv->crp_timeout_sec = crt_gdata.cg_timeout;

	rpc_priv->crp_timeout_ts = d_timeus_secdiff(rpc_priv->crp_timeout_sec);
}

/* Convert opcode to string. Only returns string for internal RPCs */
char *crt_opc_to_str(crt_opcode_t opc);

bool crt_rpc_completed(struct crt_rpc_priv *rpc_priv);

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
int crt_iv_sync_corpc_pre_forward(crt_rpc_t *rpc, void *arg);

/* crt_register.c */
int
crt_proto_register_internal(struct crt_proto_format *crf);

#endif /* __CRT_RPC_H__ */
