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
 * This file is part of CaRT. It implements the main RPC routines.
 */

#include <crt_internal.h>

/* CRT internal RPC format definitions */

/* group create */
struct crt_msg_field *crt_grp_create_in_fields[] = {
	&DMF_GRP_ID,		/* gc_grp_id */
	&DMF_RANK_LIST,		/* gc_membs */
	&DMF_RANK,		/* gc_initiate_rank */
};

struct crt_msg_field *crt_grp_create_out_fields[] = {
	&DMF_RANK_LIST,		/* gc_failed_ranks */
	&DMF_RANK,		/* gc_rank */
	&DMF_INT,		/* gc_rc */
};

struct crt_req_format DQF_CRT_GRP_CREATE =
	DEFINE_CRT_REQ_FMT("CRT_GRP_CREATE", crt_grp_create_in_fields,
			   crt_grp_create_out_fields);

/* group destroy */
struct crt_msg_field *crt_grp_destroy_in_fields[] = {
	&DMF_GRP_ID,		/* gd_grp_id */
	&DMF_RANK,		/* gd_initiate_rank */
};

struct crt_msg_field *crt_grp_destroy_out_fields[] = {
	&DMF_RANK_LIST,		/* gd_failed_ranks */
	&DMF_RANK,		/* gd_rank */
	&DMF_INT,		/* gd_rc */
};

struct crt_req_format DQF_CRT_GRP_DESTROY =
	DEFINE_CRT_REQ_FMT("CRT_GRP_DESTROY", crt_grp_destroy_in_fields,
			   crt_grp_destroy_out_fields);

/* uri lookup */
struct crt_msg_field *crt_uri_lookup_in_fields[] = {
	&DMF_GRP_ID,		/* ul_grp_id */
	&DMF_RANK,		/* ul_rank */
};

struct crt_msg_field *crt_uri_lookup_out_fields[] = {
	&DMF_PHY_ADDR,		/* ul_uri */
	&DMF_INT,		/* ul_rc */
};

struct crt_req_format DQF_CRT_URI_LOOKUP =
	DEFINE_CRT_REQ_FMT("CRT_URI_LOOKUP", crt_uri_lookup_in_fields,
			   crt_uri_lookup_out_fields);

struct crt_internal_rpc crt_internal_rpcs[] = {
	{
		.ir_name	= "CRT_GRP_CREATE",
		.ir_opc		= CRT_OPC_GRP_CREATE,
		.ir_ver		= 1,
		.ir_flags	= 0,
		.ir_req_fmt	= &DQF_CRT_GRP_CREATE,
		.ir_hdlr	= crt_hdlr_grp_create,
		.ir_co_ops	= NULL,
	}, {
		.ir_name	= "CRT_GRP_DESTROY",
		.ir_opc		= CRT_OPC_GRP_DESTROY,
		.ir_ver		= 1,
		.ir_flags	= 0,
		.ir_req_fmt	= &DQF_CRT_GRP_DESTROY,
		.ir_hdlr	= crt_hdlr_grp_destroy,
		.ir_co_ops	= NULL,
	}, {
		.ir_name	= "CRT_URI_LOOKUP",
		.ir_opc		= CRT_OPC_URI_LOOKUP,
		.ir_ver		= 1,
		.ir_flags	= 0,
		.ir_req_fmt	= &DQF_CRT_URI_LOOKUP,
		.ir_hdlr	= crt_hdlr_uri_lookup,
		.ir_co_ops	= NULL,
	}, {
		.ir_opc		= 0
	}
};

/* CRT RPC related APIs or internal functions */

int
crt_internal_rpc_register(void)
{
	struct crt_internal_rpc *rpc;
	int	rc = 0;

	/* walk through the handler list and register each individual RPC */
	for (rpc = crt_internal_rpcs; rpc->ir_opc != 0; rpc++) {
		C_ASSERT(rpc->ir_hdlr != NULL);
		rc = crt_rpc_reg_internal(rpc->ir_opc, rpc->ir_req_fmt,
					   rpc->ir_hdlr, rpc->ir_co_ops);
		if (rc) {
			C_ERROR("opcode 0x%x registration failed, rc: %d.\n",
				rpc->ir_opc, rc);
			break;
		}
	}
	return rc;
}

int
crt_rpc_priv_alloc(crt_opcode_t opc, struct crt_rpc_priv **priv_allocated)
{
	struct crt_rpc_priv	*rpc_priv;
	struct crt_opc_info	*opc_info;
	int			rc = 0;

	C_ASSERT(priv_allocated != NULL);

	opc_info = crt_opc_lookup(crt_gdata.cg_opc_map, opc, CRT_UNLOCK);
	if (opc_info == NULL) {
		C_ERROR("opc: 0x%x, lookup failed.\n", opc);
		C_GOTO(out, rc = -CER_UNREG);
	}
	C_ASSERT(opc_info->doi_input_size <= CRT_MAX_INPUT_SIZE &&
		 opc_info->doi_output_size <= CRT_MAX_OUTPUT_SIZE);

	C_ALLOC_PTR(rpc_priv);
	if (rpc_priv == NULL)
		C_GOTO(out, rc = -CER_NOMEM);

	rpc_priv->drp_opc_info = opc_info;
	*priv_allocated = rpc_priv;

out:
	return rc;
}

void
crt_rpc_priv_free(struct crt_rpc_priv *rpc_priv)
{
	if (rpc_priv == NULL)
		return;

	if (rpc_priv->drp_coll && rpc_priv->drp_corpc_info) {
		crt_rank_list_free(
			rpc_priv->drp_corpc_info->co_excluded_ranks);
		C_FREE_PTR(rpc_priv->drp_corpc_info);
	}

	pthread_spin_destroy(&rpc_priv->drp_lock);
	C_FREE_PTR(rpc_priv);
}

int
crt_req_create(crt_context_t crt_ctx, crt_endpoint_t tgt_ep, crt_opcode_t opc,
	       crt_rpc_t **req)
{
	struct crt_context	*ctx;
	struct crt_rpc_priv	*rpc_priv = NULL;
	crt_rpc_t		*rpc_pub;
	int			rc = 0;

	if (crt_ctx == CRT_CONTEXT_NULL || req == NULL) {
		C_ERROR("invalid parameter (NULL crt_ctx or req).\n");
		C_GOTO(out, rc = -CER_INVAL);
	}
	/* TODO: possibly with multiple service group */
	if (tgt_ep.ep_rank >= crt_gdata.cg_grp->gg_srv_pri_grp->gp_size) {
		C_ERROR("invalid parameter, rank %d, group_size: %d.\n",
			tgt_ep.ep_rank,
			crt_gdata.cg_grp->gg_srv_pri_grp->gp_size);
		C_GOTO(out, rc = -CER_INVAL);
	}

	rc = crt_rpc_priv_alloc(opc, &rpc_priv);
	if (rc != 0) {
		C_ERROR("crt_rpc_priv_alloc, rc: %d, opc: 0x%x.\n", rc, opc);
		C_GOTO(out, rc);
	}

	C_ASSERT(rpc_priv != NULL);
	rpc_pub = &rpc_priv->drp_pub;
	rpc_pub->dr_ep = tgt_ep;

	rc = crt_rpc_inout_buff_init(rpc_pub);
	if (rc != 0)
		C_GOTO(out, rc);

	crt_rpc_priv_init(rpc_priv, crt_ctx, opc, 0);

	ctx = (struct crt_context *)crt_ctx;
	rc = crt_hg_req_create(&ctx->dc_hg_ctx, tgt_ep, rpc_priv);
	if (rc != 0) {
		C_ERROR("crt_hg_req_create failed, rc: %d, opc: 0x%x.\n",
			rc, opc);
		C_GOTO(out, rc);
	}

	*req = rpc_pub;

out:
	if (rc < 0)
		crt_rpc_priv_free(rpc_priv);
	return rc;
}

static inline int
crt_corpc_info_init(struct crt_rpc_priv *rpc_priv, crt_group_t *grp,
		    crt_rank_list_t *excluded_ranks, crt_bulk_t co_bulk_hdl,
		    void *priv, uint32_t flags, int tree_topo)
{
	struct crt_corpc_info	*co_info;
	int			 rc = 0;

	C_ASSERT(rpc_priv != NULL);
	C_ASSERT(grp != NULL);

	C_ALLOC_PTR(co_info);
	if (co_info == NULL)
		C_GOTO(out, rc = -CER_NOMEM);

	co_info->co_grp_priv = container_of(grp, struct crt_grp_priv, gp_pub);
	rc = crt_rank_list_dup(&co_info->co_excluded_ranks, excluded_ranks,
				true /* input */);
	if (rc != 0) {
		C_ERROR("crt_rank_list_dup failed, rc: %d.\n", rc);
		C_FREE_PTR(rpc_priv);
		C_GOTO(out, rc);
	}
	crt_rank_list_sort(co_info->co_excluded_ranks);
	rpc_priv->drp_pub.dr_co_bulk_hdl = co_bulk_hdl;
	co_info->co_priv = priv;
	co_info->co_tree_topo = tree_topo;
	co_info->co_grp_destroy =
		(flags & CRT_CORPC_FLAG_GRP_DESTROY) ? 1 : 0;
	co_info->co_parent_rpc = NULL;
	CRT_INIT_LIST_HEAD(&co_info->co_child_rpcs);
	/* crt_group_size(grp, &co_info->co_child_num); */
	co_info->co_child_num = co_info->co_grp_priv->gp_membs->rl_nr.num;
	co_info->co_child_ack_num = 0;

	rpc_priv->drp_corpc_info = co_info;
	rpc_priv->drp_coll = 1;

out:
	return rc;
}

/* TODO:
 * 1. refine the process of req create/destroy, for P2P RPC, corpc and in
 *    RPC handler
 * 2. collective bulk
 */
int
crt_corpc_req_create(crt_context_t crt_ctx, crt_group_t *grp,
		     crt_rank_list_t *excluded_ranks, crt_opcode_t opc,
		     crt_bulk_t co_bulk_hdl, void *priv,  uint32_t flags,
		     int tree_topo, crt_rpc_t **req)
{
	struct crt_rpc_priv	*rpc_priv = NULL;
	crt_rpc_t		*rpc_pub;
	int			rc = 0;

	if (crt_ctx == CRT_CONTEXT_NULL || grp == NULL || req == NULL) {
		C_ERROR("invalid parameter (NULL crt_ctx, grp or req).\n");
		C_GOTO(out, rc = -CER_INVAL);
	}

	rc = crt_rpc_priv_alloc(opc, &rpc_priv);
	if (rc != 0) {
		C_ERROR("crt_rpc_priv_alloc, rc: %d, opc: 0x%x.\n", rc, opc);
		C_GOTO(out, rc);
	}

	C_ASSERT(rpc_priv != NULL);
	rpc_pub = &rpc_priv->drp_pub;
	rc = crt_rpc_inout_buff_init(rpc_pub);
	if (rc != 0)
		C_GOTO(out, rc);
	crt_rpc_priv_init(rpc_priv, crt_ctx, opc, 0);

	rc = crt_corpc_info_init(rpc_priv, grp, excluded_ranks, co_bulk_hdl,
				 priv, flags, tree_topo);
	if (rc != 0) {
		C_ERROR("crt_corpc_info_init failed, rc: %d, opc: 0x%x.\n",
			rc, opc);
		C_GOTO(out, rc);
	}

	*req = rpc_pub;
out:
	if (rc < 0)
		crt_rpc_priv_free(rpc_priv);
	return rc;
}

int
crt_req_addref(crt_rpc_t *req)
{
	struct crt_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		C_ERROR("invalid parameter (NULL req).\n");
		C_GOTO(out, rc = -CER_INVAL);
	}

	rpc_priv = container_of(req, struct crt_rpc_priv, drp_pub);
	pthread_spin_lock(&rpc_priv->drp_lock);
	rpc_priv->drp_refcount++;
	pthread_spin_unlock(&rpc_priv->drp_lock);

out:
	return rc;
}

int
crt_req_decref(crt_rpc_t *req)
{
	struct crt_rpc_priv	*rpc_priv = NULL;
	int			rc = 0, destroy = 0;

	if (req == NULL) {
		C_ERROR("invalid parameter (NULL req).\n");
		C_GOTO(out, rc = -CER_INVAL);
	}

	rpc_priv = container_of(req, struct crt_rpc_priv, drp_pub);
	pthread_spin_lock(&rpc_priv->drp_lock);
	rpc_priv->drp_refcount--;
	if (rpc_priv->drp_refcount == 0)
		destroy = 1;
	pthread_spin_unlock(&rpc_priv->drp_lock);

	if (destroy == 1) {
		rc = crt_hg_req_destroy(rpc_priv);
		if (rc != 0)
			C_ERROR("crt_hg_req_destroy failed, rc: %d, "
				"opc: 0x%x.\n", rc, req->dr_opc);
	}

out:
	return rc;
}

struct corpc_child_req {
	crt_list_t	 cr_link;
	crt_rpc_t	*cr_rpc;
};

static inline int
corpc_add_child_rpc(struct crt_rpc_priv *parent_rpc,
		    struct crt_rpc_priv *child_rpc)
{
	struct crt_corpc_info	*co_info;
	struct corpc_child_req	*child_req_item;
	int			rc = 0;

	C_ASSERT(parent_rpc != NULL);
	C_ASSERT(child_rpc != NULL);
	C_ASSERT(parent_rpc->drp_coll == 1 &&
		 parent_rpc->drp_corpc_info != NULL);

	co_info = parent_rpc->drp_corpc_info;

	C_ALLOC_PTR(child_req_item);
	if (child_req_item == NULL)
		C_GOTO(out, rc = -CER_NOMEM);

	CRT_INIT_LIST_HEAD(&child_req_item->cr_link);
	child_req_item->cr_rpc = &child_rpc->drp_pub;

	rc = crt_req_addref(&child_rpc->drp_pub);
	C_ASSERT(rc == 0);

	pthread_spin_lock(&parent_rpc->drp_lock);
	crt_list_add_tail(&child_req_item->cr_link, &co_info->co_child_rpcs);
	pthread_spin_unlock(&parent_rpc->drp_lock);

out:
	return rc;
}

static inline void
corpc_del_child_rpc(struct crt_rpc_priv *parent_rpc,
		    struct crt_rpc_priv *child_rpc)
{
	struct crt_corpc_info	*co_info;
	struct corpc_child_req	*child_req_item, *next;
	int			rc = 0;

	C_ASSERT(parent_rpc != NULL);
	C_ASSERT(child_rpc != NULL);
	C_ASSERT(parent_rpc->drp_coll == 1 &&
		 parent_rpc->drp_corpc_info != NULL);

	co_info = parent_rpc->drp_corpc_info;
	pthread_spin_lock(&parent_rpc->drp_lock);
	crt_list_for_each_entry_safe(child_req_item, next,
				      &co_info->co_child_rpcs, cr_link) {
		if (child_req_item->cr_rpc == &child_rpc->drp_pub) {
			crt_list_del_init(&child_req_item->cr_link);
			/* decref corresponds to the addref in
			 * corpc_add_child_rpc */
			rc = crt_req_decref(&child_rpc->drp_pub);
			C_ASSERT(rc == 0);
			C_FREE_PTR(child_req_item);
			break;
		}
	}
	pthread_spin_unlock(&parent_rpc->drp_lock);
}

static int
corpc_child_cb(const struct crt_cb_info *cb_info)
{
	struct crt_rpc_priv	*parent_rpc_priv;
	struct crt_corpc_info	*co_info;
	struct crt_rpc_priv	*child_rpc_priv;
	crt_rpc_t		*child_req;
	struct crt_opc_info	*opc_info;
	struct crt_corpc_ops	*co_ops;
	crt_rank_t		 my_rank;
	bool			 req_done = false;
	int			 rc = 0;

	child_req = cb_info->dci_rpc;
	rc = cb_info->dci_rc;
	parent_rpc_priv = (struct crt_rpc_priv *)cb_info->dci_arg;
	C_ASSERT(child_req != NULL && parent_rpc_priv != NULL);
	child_rpc_priv = container_of(child_req, struct crt_rpc_priv, drp_pub);
	co_info = parent_rpc_priv->drp_corpc_info;
	C_ASSERT(co_info != NULL);
	C_ASSERT(parent_rpc_priv->drp_pub.dr_opc == child_req->dr_opc);
	opc_info = parent_rpc_priv->drp_opc_info;
	C_ASSERT(opc_info != NULL);
	co_ops = opc_info->doi_co_ops;

	crt_group_rank(NULL, &my_rank);

	pthread_spin_lock(&parent_rpc_priv->drp_lock);
	if (rc != 0) {
		C_ERROR("RPC(opc: 0x%x) error, rc: %d.\n",
			child_req->dr_opc, rc);
		co_info->co_rc = rc;
	}
	co_info->co_child_ack_num++;
	C_ASSERT(co_info->co_child_num >= co_info->co_child_ack_num);
	if (co_info->co_child_num == co_info->co_child_ack_num)
		req_done = true;
	/* call user aggregate callback */
	if (co_ops != NULL) {
		C_ASSERT(co_ops->co_aggregate != NULL);
		rc = co_ops->co_aggregate(child_req, &parent_rpc_priv->drp_pub,
					  co_info->co_priv);
		if (rc != 0) {
			C_ERROR("co_ops->co_aggregate failed, rc: %d, "
				"opc: 0x%x.\n", rc, child_req->dr_opc);
			rc = 0;
		}
	}
	pthread_spin_unlock(&parent_rpc_priv->drp_lock);

	corpc_del_child_rpc(parent_rpc_priv, child_rpc_priv);

	if (req_done == false)
		C_GOTO(out, rc);

	crt_rpc_complete(parent_rpc_priv, co_info->co_rc);

out:
	return rc;
}

int
crt_corpc_send(crt_rpc_t *req)
{
	struct crt_corpc_info	*co_info;
	crt_rank_list_t	*member_ranks;
	struct crt_rpc_priv	*rpc_priv, *child_rpc_priv;
	bool			child_req_sent = false;
	int			i, rc = 0;

	C_ASSERT(req != NULL);
	rpc_priv = container_of(req, struct crt_rpc_priv, drp_pub);
	co_info = rpc_priv->drp_corpc_info;
	C_ASSERT(co_info != NULL);
	member_ranks = co_info->co_grp_priv->gp_membs;
	C_ASSERT(member_ranks != NULL);

	C_ASSERT(co_info->co_child_num == member_ranks->rl_nr.num);

	/* now send P2P RPC one by one */
	for (i = 0; i < co_info->co_child_num; i++) {
		crt_rpc_t	*child_rpc;
		crt_endpoint_t	 tgt_ep;

		if (crt_rank_in_rank_list(co_info->co_excluded_ranks,
					   member_ranks->rl_ranks[i])) {
			C_DEBUG(CF_TP, "rank %d in excluded list, ignored.\n",
				member_ranks->rl_ranks[i]);
			co_info->co_child_ack_num++;
			continue;
		}
		tgt_ep.ep_grp = NULL;
		tgt_ep.ep_rank = member_ranks->rl_ranks[i];
		tgt_ep.ep_tag = 0;
		rc = crt_req_create(req->dr_ctx, tgt_ep, req->dr_opc,
				    &child_rpc);
		if (rc != 0) {
			C_ERROR("crt_req_create(opc: 0x%x) failed, tgt_ep: %d, "
				"rc: %d.\n", req->dr_opc, tgt_ep.ep_rank, rc);
			co_info->co_child_ack_num += co_info->co_child_num - i;
			co_info->co_rc = rc;
			C_GOTO(out, rc);
		}

		C_ASSERT(child_rpc != NULL);
		C_ASSERT(child_rpc->dr_input_size == req->dr_input_size);
		C_ASSERT(child_rpc->dr_output_size == req->dr_output_size);
		child_rpc_priv = container_of(child_rpc, struct crt_rpc_priv,
					      drp_pub);

		/* TODO: should avoid this memcpy */
		if (child_rpc->dr_input_size != 0) {
			C_ASSERT(child_rpc->dr_input != NULL);
			C_ASSERT(req->dr_input != NULL);
			memcpy(child_rpc->dr_input, req->dr_input,
			       req->dr_input_size);
		}
		rc = crt_req_send(child_rpc, corpc_child_cb, rpc_priv);
		if (rc != 0) {
			C_ERROR("crt_req_send(opc: 0x%x) failed, tgt_ep: %d, "
				"rc: %d.\n", req->dr_opc, tgt_ep.ep_rank, rc);
			co_info->co_child_ack_num += co_info->co_child_num - i;
			co_info->co_rc = rc;
			C_GOTO(out, rc);
		}
		rc = corpc_add_child_rpc(rpc_priv, child_rpc_priv);
		C_ASSERT(rc == 0);

		child_req_sent =  true;
	}

out:
	if (child_req_sent == false) {
		C_ASSERT(rc != 0);
		C_ERROR("crt_corpc_send(rpc: 0x%x) failed, rc: %d.\n",
			req->dr_opc, rc);
		crt_req_addref(req);
		crt_rpc_complete(rpc_priv, rc);
		crt_req_decref(req);
	}
	return rc;
}

int
crt_req_send(crt_rpc_t *req, crt_cb_t complete_cb, void *arg)
{
	struct crt_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		C_ERROR("invalid parameter (NULL req).\n");
		C_GOTO(out, rc = -CER_INVAL);
	}
	if (req->dr_ctx == NULL) {
		C_ERROR("invalid parameter (NULL req->dr_ctx).\n");
		C_GOTO(out, rc = -CER_INVAL);
	}

	rpc_priv = container_of(req, struct crt_rpc_priv, drp_pub);
	rpc_priv->drp_complete_cb = complete_cb;
	rpc_priv->drp_arg = arg;

	if (rpc_priv->drp_coll) {
		rc = crt_corpc_send(req);
		if (rc != 0)
			C_ERROR("crt_corpc_send failed, rc: %d, opc: 0x%x.\n",
				rc, req->dr_opc);
		C_GOTO(out, rc);
	}

	rc = crt_context_req_track(req);
	if (rc == CRT_REQ_TRACK_IN_INFLIGHQ) {
		/* tracked in crt_ep_inflight::epi_req_q */
		/* set state before sending to avoid race with complete_cb */
		rpc_priv->drp_state = RPC_REQ_SENT;
		rc = crt_hg_req_send(rpc_priv);
		if (rc != 0) {
			C_ERROR("crt_hg_req_send failed, rc: %d, opc: 0x%x.\n",
				rc, rpc_priv->drp_pub.dr_opc);
			rpc_priv->drp_state = RPC_INITED;
			crt_context_req_untrack(req);
		}
	} else if (rc == CRT_REQ_TRACK_IN_WAITQ) {
		/* queued in crt_hg_context::dhc_req_q */
		rc = 0;
	} else {
		C_ERROR("crt_req_track failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
	}

out:
	/* internally destroy the req when failed */
	if (rc != 0 && req != NULL)
		crt_req_decref(req);
	return rc;
}

int
crt_reply_send(crt_rpc_t *req)
{
	struct crt_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		C_ERROR("invalid parameter (NULL req).\n");
		C_GOTO(out, rc = -CER_INVAL);
	}

	rpc_priv = container_of(req, struct crt_rpc_priv, drp_pub);
	rc = crt_hg_reply_send(rpc_priv);
	if (rc != 0) {
		C_ERROR("crt_hg_reply_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
	}

out:
	return rc;
}

int
crt_req_abort(crt_rpc_t *req)
{
	struct crt_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		C_ERROR("invalid parameter (NULL req).\n");
		C_GOTO(out, rc = -CER_INVAL);
	}

	rpc_priv = container_of(req, struct crt_rpc_priv, drp_pub);
	rc = crt_hg_req_cancel(rpc_priv);
	if (rc != 0) {
		C_ERROR("crt_hg_req_cancel failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
	}

out:
	return rc;
}

#define CRT_DEFAULT_TIMEOUT	(20 * 1000 * 1000) /* Milli-seconds */

static int
crt_cb_common(const struct crt_cb_info *cb_info)
{
	*(int *)cb_info->dci_arg = 1;
	return 0;
}

/**
 * Send rpc synchronously
 *
 * \param[IN] rpc	point to CRT request.
 * \param[IN] timeout	timeout (Micro-seconds) to wait, if
 *                      timeout <= 0, it will wait infinitely.
 * \return		0 if rpc return successfuly.
 * \return		negative errno if sending fails or timeout.
 */
int
crt_sync_req(crt_rpc_t *rpc, uint64_t timeout)
{
	uint64_t now;
	uint64_t end;
	int rc;
	int complete = 0;

	/* Send request */
	rc = crt_req_send(rpc, crt_cb_common, &complete);
	if (rc != 0)
		return rc;

	/* Check if we are lucky */
	if (complete)
		return 0;

	timeout = timeout ? timeout : CRT_DEFAULT_TIMEOUT;
	/* Wait the request to be completed in timeout milliseconds */
	end = crt_time_usec(0) + timeout;

	while (1) {
		uint64_t interval = 1000; /* milliseconds */

		rc = crt_progress(rpc->dr_ctx, interval, NULL, NULL);
		if (rc != 0 && rc != -CER_TIMEDOUT) {
			C_ERROR("crt_progress failed rc: %d.\n", rc);
			break;
		}

		if (complete) {
			rc = 0;
			break;
		}

		now = crt_time_usec(0);
		if (now >= end) {
			rc = -CER_TIMEDOUT;
			break;
		}
	}

	return rc;
}

void
crt_rpc_priv_init(struct crt_rpc_priv *rpc_priv, crt_context_t crt_ctx,
		  crt_opcode_t opc, int srv_flag)
{
	C_ASSERT(rpc_priv != NULL);
	CRT_INIT_LIST_HEAD(&rpc_priv->drp_epi_link);
	CRT_INIT_LIST_HEAD(&rpc_priv->drp_tmp_link);
	rpc_priv->drp_complete_cb = NULL;
	rpc_priv->drp_arg = NULL;
	crt_common_hdr_init(&rpc_priv->drp_req_hdr, opc);
	crt_common_hdr_init(&rpc_priv->drp_reply_hdr, opc);
	rpc_priv->drp_state = RPC_INITED;
	rpc_priv->drp_srv = (srv_flag != 0);
	/* initialize as 1, so user can cal crt_req_decref to destroy new req */
	rpc_priv->drp_refcount = 1;
	pthread_spin_init(&rpc_priv->drp_lock, PTHREAD_PROCESS_PRIVATE);

	rpc_priv->drp_pub.dr_opc = opc;
	rpc_priv->drp_pub.dr_ctx = crt_ctx;
}

void
crt_rpc_inout_buff_fini(crt_rpc_t *rpc_pub)
{
	C_ASSERT(rpc_pub != NULL);

	if (rpc_pub->dr_input != NULL) {
		C_ASSERT(rpc_pub->dr_input_size != 0);
		C_FREE(rpc_pub->dr_input, rpc_pub->dr_input_size);
		rpc_pub->dr_input_size = 0;
	}

	if (rpc_pub->dr_output != NULL) {
		C_ASSERT(rpc_pub->dr_output_size != 0);
		C_FREE(rpc_pub->dr_output, rpc_pub->dr_output_size);
		rpc_pub->dr_output_size = 0;
	}
}

int
crt_rpc_inout_buff_init(crt_rpc_t *rpc_pub)
{
	struct crt_rpc_priv	*rpc_priv;
	struct crt_opc_info	*opc_info;
	int			rc = 0;

	C_ASSERT(rpc_pub != NULL);
	C_ASSERT(rpc_pub->dr_input == NULL);
	C_ASSERT(rpc_pub->dr_output == NULL);
	rpc_priv = container_of(rpc_pub, struct crt_rpc_priv, drp_pub);
	opc_info = rpc_priv->drp_opc_info;
	C_ASSERT(opc_info != NULL);

	if (opc_info->doi_input_size > 0) {
		C_ALLOC(rpc_pub->dr_input, opc_info->doi_input_size);
		if (rpc_pub->dr_input == NULL) {
			C_ERROR("cannot allocate memory(size "CF_U64") for "
				"dr_input.\n", opc_info->doi_input_size);
			C_GOTO(out, rc = -CER_NOMEM);
		}
		rpc_pub->dr_input_size = opc_info->doi_input_size;
	}
	if (opc_info->doi_output_size > 0) {
		C_ALLOC(rpc_pub->dr_output, opc_info->doi_output_size);
		if (rpc_pub->dr_output == NULL) {
			C_ERROR("cannot allocate memory(size "CF_U64") for "
				"dr_putput.\n", opc_info->doi_input_size);
			C_GOTO(out, rc = -CER_NOMEM);
		}
		rpc_pub->dr_output_size = opc_info->doi_output_size;
	}

out:
	if (rc < 0)
		crt_rpc_inout_buff_fini(rpc_pub);
	return rc;
}
