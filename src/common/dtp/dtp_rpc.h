/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of daos_transport. It gives out the data types internally
 * used by dtp and not in other specific header files.
 */

#ifndef __DTP_RPC_H__
#define __DTP_RPC_H__

#define DTP_RPC_MAGIC			(0xAB0C01EC)
#define DTP_RPC_VERSION			(0x00000001)

/* dtp layer common header */
struct dtp_common_hdr {
	uint32_t	dch_magic;
	uint32_t	dch_version;
	uint32_t	dch_opc;
	uint32_t	dch_cksum;
	uint32_t	dch_flags;
	/* gid and rank identify the rpc request sender */
	dtp_rank_t	dch_rank; /* uint32_t */
	uint32_t	dch_grp_id; /* internal grp_id within the rank */
	uint32_t	dch_padding[1];
};

typedef enum {
	RPC_INITED = 0x36,
	RPC_QUEUED, /* queued for flow controlling */
	RPC_REQ_SENT,
	RPC_REPLY_RECVED,
	RPC_COMPLETED,
	RPC_CANCELED,
} dtp_rpc_state_t;

struct dtp_rpc_priv;

struct dtp_corpc_info {
	struct dtp_grp_priv	*co_grp_priv;
	dtp_rank_list_t	*co_excluded_ranks;
	/* dtp_bulk_t		 co_bulk_hdl; */ /* collective bulk handle */
	/* the priv passed in dtp_corpc_req_create */
	void			*co_priv;
	int			 co_tree_topo;
	uint32_t		 co_grp_destroy:1; /* grp destroy flag */

	struct dtp_rpc_priv	*co_parent_rpc; /* parent RPC, NULL on root */
	daos_list_t		 co_child_rpcs; /* child RPCs list */
	uint32_t		 co_child_num;
	uint32_t		 co_child_ack_num;
	int			 co_rc;
};

struct dtp_rpc_priv {
	/* link to dtp_ep_inflight::epi_req_q/::epi_req_waitq */
	daos_list_t		drp_epi_link;
	/* tmp_link used in dtp_context_req_untrack */
	daos_list_t		drp_tmp_link;
	uint64_t		drp_ts; /* time stamp */
	dtp_cb_t		drp_complete_cb;
	void			*drp_arg; /* argument for drp_complete_cb */
	struct dtp_ep_inflight	*drp_epi; /* point back to inflight ep */

	dtp_rpc_t		drp_pub; /* public part */
	struct dtp_common_hdr	drp_req_hdr; /* common header for request */
	struct dtp_common_hdr	drp_reply_hdr; /* common header for reply */
	dtp_rpc_state_t		drp_state; /* RPC state */
	hg_handle_t		drp_hg_hdl;
	na_addr_t		drp_na_addr;
	uint32_t		drp_srv:1, /* flag of server received request */
				drp_output_got:1,
				drp_input_got:1,
				drp_coll:1; /* flag of collective RPC */
	uint32_t		drp_refcount;
	pthread_spinlock_t	drp_lock;
	struct dtp_opc_info	*drp_opc_info;
	/* corpc info, only valid when (drp_coll == 1) */
	struct dtp_corpc_info	*drp_corpc_info;
};

static inline void
dtp_common_hdr_init(struct dtp_common_hdr *hdr, dtp_opcode_t opc)
{
	D_ASSERT(hdr != NULL);
	hdr->dch_opc = opc;
	hdr->dch_magic = DTP_RPC_MAGIC;
	hdr->dch_version = DTP_RPC_VERSION;
	hdr->dch_grp_id = 0; /* TODO primary group with internal grp_id as 0 */
	D_ASSERT(dtp_group_rank(0, &hdr->dch_rank) == 0);
}

void dtp_rpc_priv_init(struct dtp_rpc_priv *rpc_priv, dtp_context_t dtp_ctx,
		       dtp_opcode_t opc, int srv_flag);
void dtp_rpc_inout_buff_fini(dtp_rpc_t *rpc_pub);
int dtp_rpc_inout_buff_init(dtp_rpc_t *rpc_pub);
void dtp_rpc_priv_free(struct dtp_rpc_priv *rpc_priv);

/* DTP internal opcode definitions, must be 0xFFFFxxxx.*/
enum {
	DTP_OPC_INTERNAL_BASE	= 0xFFFF0000,
	DTP_OPC_GRP_CREATE	= DTP_OPC_INTERNAL_BASE + 0x1,
	DTP_OPC_GRP_DESTROY	= DTP_OPC_INTERNAL_BASE + 0x2,
};

/* DTP internal RPC definitions */
struct dtp_grp_create_in {
	dtp_group_id_t		 gc_grp_id;
	dtp_rank_list_t	*gc_membs;
	/* the rank initiated the group create */
	dtp_rank_t		 gc_initiate_rank;
};

struct dtp_grp_create_out {
	/* failed rank list, can be used to aggregate the reply from child */
	dtp_rank_list_t	*gc_failed_ranks;
	/* the rank sent out the reply */
	dtp_rank_t		 gc_rank;
	/* return code, if failed the gc_rank should be in gc_failed_ranks */
	int			 gc_rc;
};

struct dtp_grp_destroy_in {
	dtp_group_id_t		gd_grp_id;
	/* the rank initiated the group destroy */
	dtp_rank_t		gd_initiate_rank;
};

struct dtp_grp_destroy_out {
	/* failed rank list, can be used to aggregate the reply from child */
	dtp_rank_list_t	*gd_failed_ranks;
	/* the rank sent out the reply */
	dtp_rank_t		 gd_rank;
	/* return code, if failed the gc_rank should be in gc_failed_ranks */
	int			 gd_rc;
};

/* DTP internal RPC format definitions */
struct dtp_internal_rpc {
	/* Name of the RPC */
	const char		*ir_name;
	/* Operation code associated with the RPC */
	dtp_opcode_t		 ir_opc;
	/* RPC version */
	int			 ir_ver;
	/* Operation flags, TBD */
	int			 ir_flags;
	/* RPC request format */
	struct dtp_req_format	*ir_req_fmt;
	/* RPC handler */
	dtp_rpc_cb_t		 ir_hdlr;
	/* collective ops */
	struct dtp_corpc_ops	*ir_co_ops;
};

int dtp_internal_rpc_register(void);

#endif /* __DTP_RPC_H__ */
