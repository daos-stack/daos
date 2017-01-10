/* Copyright (C) 2016-2017 Intel Corporation
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

#include <crt_util/heap.h>

#define CRT_RPC_MAGIC			(0xAB0C01EC)
#define CRT_RPC_VERSION			(0x00000001)

/* default RPC timeout 60 second */
#define CRT_DEFAULT_TIMEOUT_S	(60) /* second */
#define CRT_DEFAULT_TIMEOUT_US	(CRT_DEFAULT_TIMEOUT_S * 1e6) /* micro-second */

/* uri lookup RPC timeout 500mS */
#define CRT_URI_LOOKUP_TIMEOUT		(1000 * 500)

extern struct crt_binheap_ops crt_timeout_bh_ops;

enum crt_rpc_flags_internal {
	/* flag of collective RPC (bcast) */
	CRT_RPC_FLAG_COLL		= (1U << 16),
	/* flag of incast variable */
	CRT_RPC_FLAG_INCAST		= (1U << 17),
	/* flag of targeting primary group */
	CRT_RPC_FLAG_PRIMARY_GRP	= (1U << 18),
	/* group members piggyback */
	CRT_RPC_FLAG_MEMBS_INLINE	= (1U << 19),
};

struct crt_corpc_hdr {
	/* internal group ID */
	uint64_t		 coh_int_grpid;
	/* collective bulk handle */
	crt_bulk_t		 coh_bulk_hdl;
	/* optional excluded ranks */
	crt_rank_list_t		*coh_excluded_ranks;
	/* optional inline ranks, for example piggyback the group members */
	crt_rank_list_t		*coh_inline_ranks;
	/* group membership version */
	uint32_t		 coh_grp_ver;
	uint32_t		 coh_tree_topo;
	/* root rank of the tree, it is the logical rank within the group */
	uint32_t		 coh_root;
	uint32_t		 coh_padding;
};

/* CaRT layer common header */
struct crt_common_hdr {
	uint32_t	cch_magic;
	uint32_t	cch_version; /* RPC version */
	uint32_t	cch_opc;
	uint32_t	cch_cksum;
	/* RPC request flag, see enum crt_rpc_flags/crt_rpc_flags_internal */
	uint32_t	cch_flags;
	/* gid and rank identify the rpc request sender */
	crt_rank_t	cch_rank;
	/* TODO: maybe used as a tier ID or something else, ignore for now */
	uint32_t	cch_grp_id;
	/* used only in crp_reply_hdr to propagate corpc failure back to root */
	uint32_t	cch_co_rc;
};

typedef enum {
	RPC_INITED = 0x36,
	RPC_QUEUED, /* queued for flow controlling */
	RPC_REQ_SENT,
	RPC_REPLY_RECVED,
	RPC_COMPLETED,
	RPC_CANCELED,
	RPC_TIMEOUT,
} crt_rpc_state_t;

struct crt_rpc_priv;

/* corpc info to track the tree topo and child RPCs info */
struct crt_corpc_info {
	struct crt_grp_priv	*co_grp_priv;
	crt_rank_list_t		*co_excluded_ranks;
	uint32_t		 co_grp_ver;
	uint32_t		 co_tree_topo;
	crt_rank_t		 co_root;
	/* the priv passed in crt_corpc_req_create */
	void			*co_priv;
	/* child RPCs list */
	crt_list_t		 co_child_rpcs;
	/*
	 * replied child RPC list, when a child RPC being replied and parent
	 * RPC has not been locally handled, we can not aggregate the reply
	 * as it possibly be over-written by local RPC handler. So when child
	 * RPC being replied and parent RPC not finished, the child RPC is
	 * queued at co_replied_rpcs.
	 */
	crt_list_t		 co_replied_rpcs;
	uint32_t		 co_child_num;
	uint32_t		 co_child_ack_num;
	uint32_t		 co_child_failed_num;
	/*
	 * co_local_done is the flag of local RPC finish handling
	 * (local reply ready).
	 */
	uint32_t		 co_local_done:1,
	/* co_root_excluded is the flag of root in excluded rank list */
				 co_root_excluded:1;
	int			 co_rc;
};

struct crt_rpc_priv {
	/* link to crt_ep_inflight::epi_req_q/::epi_req_waitq */
	crt_list_t		crp_epi_link;
	/* tmp_link used in crt_context_req_untrack */
	crt_list_t		crp_tmp_link;
	/* link to parent RPC crp_opc_info->co_child_rpcs/co_replied_rpcs */
	crt_list_t		crp_parent_link;
	/* binheap node for timeout management, in crt_context::cc_bh_timeout */
	struct crt_binheap_node	crp_timeout_bp_node;
	/* time stamp to be timeout, the key of timeout binheap */
	uint64_t		crp_timeout_ts;
	crt_cb_t		crp_complete_cb;
	void			*crp_arg; /* argument for crp_complete_cb */
	struct crt_ep_inflight	*crp_epi; /* point back to inflight ep */

	crt_rpc_t		crp_pub; /* public part */
	crt_rpc_state_t		crp_state; /* RPC state */
	hg_handle_t		crp_hg_hdl;
	na_addr_t		crp_na_addr;
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
				/* flag of forwarded rpc for corpc */
				crp_forward:1,
				/* flag of in timeout binheap */
				crp_in_binheap:1;
	uint32_t		crp_refcount;
	struct crt_opc_info	*crp_opc_info;
	/* corpc info, only valid when (crp_coll == 1) */
	struct crt_corpc_info	*crp_corpc_info;
	pthread_spinlock_t	crp_lock;
	struct crt_common_hdr	crp_reply_hdr; /* common header for reply */
	struct crt_common_hdr	crp_req_hdr; /* common header for request */
	struct crt_corpc_hdr	crp_coreq_hdr; /* collective request header */
};

/* CRT internal opcode definitions, must be 0xFFFFxxxx.*/
enum {
	CRT_OPC_GRP_CREATE			= CRT_OPC_INTERNAL_BASE + 0x1,
	CRT_OPC_GRP_DESTROY			= CRT_OPC_INTERNAL_BASE + 0x2,

	CRT_OPC_GRP_ATTACH			= CRT_OPC_INTERNAL_BASE + 0x100,
	CRT_OPC_GRP_DETACH			= CRT_OPC_INTERNAL_BASE + 0x101,
	CRT_OPC_URI_LOOKUP			= CRT_OPC_INTERNAL_BASE + 0x102,

	CRT_OPC_RANK_EVICT			= CRT_OPC_INTERNAL_BASE + 0x103,
	CRT_OPC_SELF_TEST_PING_BOTH_EMPTY	= CRT_OPC_INTERNAL_BASE + 0x200,
	CRT_OPC_SELF_TEST_PING_SEND_EMPTY	= CRT_OPC_INTERNAL_BASE + 0x201,
	CRT_OPC_SELF_TEST_PING_REPLY_EMPTY	= CRT_OPC_INTERNAL_BASE + 0x202,
	CRT_OPC_SELF_TEST_PING_BOTH_NONEMPTY	= CRT_OPC_INTERNAL_BASE + 0x203,

	CRT_OPC_IV_FETCH			= CRT_OPC_INTERNAL_BASE + 0x300,
	CRT_OPC_IV_UPDATE			= CRT_OPC_INTERNAL_BASE + 0x301,
	CRT_OPC_IV_INVALIDATE			= CRT_OPC_INTERNAL_BASE + 0x302,
	CRT_OPC_IV_REFRESH			= CRT_OPC_INTERNAL_BASE + 0x303,
};

/* CRT internal RPC definitions */
struct crt_grp_create_in {
	/* user visible grp id (group name) */
	crt_group_id_t		 gc_grp_id;
	/* internal subgrp id */
	uint64_t		 gc_int_grpid;
	crt_rank_list_t		*gc_membs;
	/* the rank initiated the group create */
	crt_rank_t		 gc_initiate_rank;
};

struct crt_grp_create_out {
	/* failed rank list, can be used to aggregate the reply from child */
	crt_rank_list_t		*gc_failed_ranks;
	/* the rank sent out the reply */
	crt_rank_t		 gc_rank;
	/* return code, if failed the gc_rank should be in gc_failed_ranks */
	int			 gc_rc;
};

struct crt_grp_destroy_in {
	crt_group_id_t		gd_grp_id;
	/* the rank initiated the group destroy */
	crt_rank_t		gd_initiate_rank;
};

struct crt_grp_destroy_out {
	/* failed rank list, can be used to aggregate the reply from child */
	crt_rank_list_t		*gd_failed_ranks;
	/* the rank sent out the reply */
	crt_rank_t		 gd_rank;
	/* return code, if failed the gc_rank should be in gc_failed_ranks */
	int			 gd_rc;
};

struct crt_uri_lookup_in {
	crt_group_id_t		 ul_grp_id;
	crt_rank_t		 ul_rank;
};

struct crt_uri_lookup_out {
	crt_phy_addr_t		 ul_uri;
	int			 ul_rc;
};

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

static inline void
crt_common_hdr_init(struct crt_common_hdr *hdr, crt_opcode_t opc)
{
	C_ASSERT(hdr != NULL);
	hdr->cch_opc = opc;
	hdr->cch_magic = CRT_RPC_MAGIC;
	hdr->cch_version = CRT_RPC_VERSION;
	hdr->cch_grp_id = 0;
	C_ASSERT(crt_group_rank(0, &hdr->cch_rank) == 0);
}

/* crt_rpc.c */
int crt_rpc_priv_alloc(crt_opcode_t opc, struct crt_rpc_priv **priv_allocated);
void crt_rpc_priv_free(struct crt_rpc_priv *rpc_priv);
int crt_rpc_priv_init(struct crt_rpc_priv *rpc_priv, crt_context_t crt_ctx,
		       crt_opcode_t opc, bool srv_flag, bool forward);
void crt_rpc_priv_fini(struct crt_rpc_priv *rpc_priv);
int crt_req_create_internal(crt_context_t crt_ctx, crt_endpoint_t tgt_ep,
			    crt_opcode_t opc, bool forward, crt_rpc_t **req);
int crt_internal_rpc_register(void);
int crt_req_send_sync(crt_rpc_t *rpc, uint64_t timeout);
int crt_rpc_common_hdlr(struct crt_rpc_priv *rpc_priv);

static inline bool
crt_req_timedout(crt_rpc_t *rpc)
{
	struct crt_rpc_priv *rpc_priv;

	rpc_priv = container_of(rpc, struct crt_rpc_priv, crp_pub);
	return rpc_priv->crp_state == RPC_REQ_SENT &&
	       !rpc_priv->crp_in_binheap;
}

static inline bool
crt_req_aborted(crt_rpc_t *rpc)
{
	struct crt_rpc_priv *rpc_priv;

	rpc_priv = container_of(rpc, struct crt_rpc_priv, crp_pub);
	return rpc_priv->crp_state == RPC_CANCELED;
}

/* crt_corpc.c */
int crt_corpc_req_hdlr(crt_rpc_t *req);
int crt_corpc_reply_hdlr(const struct crt_cb_info *cb_info);
int crt_corpc_common_hdlr(struct crt_rpc_priv *rpc_priv);

/* crt_iv.c */
int crt_hdlr_iv_fetch(crt_rpc_t *rpc_req);

#endif /* __CRT_RPC_H__ */
