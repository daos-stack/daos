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
 * This file is part of daos_transport. It implements the main RPC routines.
 */

#include <dtp_internal.h>

/* DTP internal RPC format definitions */

struct dtp_msg_field *dtp_grp_create_in_fields[] = {
	&DMF_GRP_ID,		/* gc_grp_id */
	&DMF_RANK_LIST,		/* gc_membs */
	&DMF_RANK,		/* gc_initiate_rank */
};

struct dtp_msg_field *dtp_grp_create_out_fields[] = {
	&DMF_RANK_LIST,		/* gc_failed_ranks */
	&DMF_RANK,		/* gc_rank */
	&DMF_INT,		/* gc_rc */
};

struct dtp_req_format DQF_DTP_GRP_CREATE =
	DEFINE_DTP_REQ_FMT("DTP_GRP_CREATE", dtp_grp_create_in_fields,
			   dtp_grp_create_out_fields);

struct dtp_msg_field *dtp_grp_destroy_in_fields[] = {
	&DMF_GRP_ID,		/* gd_grp_id */
	&DMF_RANK,		/* gd_initiate_rank */
};

struct dtp_msg_field *dtp_grp_destroy_out_fields[] = {
	&DMF_RANK_LIST,		/* gd_failed_ranks */
	&DMF_RANK,		/* gd_rank */
	&DMF_INT,		/* gd_rc */
};

struct dtp_req_format DQF_DTP_GRP_DESTROY =
	DEFINE_DTP_REQ_FMT("DTP_GRP_DESTROY", dtp_grp_destroy_in_fields,
			   dtp_grp_destroy_out_fields);


struct dtp_internal_rpc dtp_internal_rpcs[] = {
	{
		.ir_name	= "DTP_GRP_CREATE",
		.ir_opc		= DTP_OPC_GRP_CREATE,
		.ir_ver		= 1,
		.ir_flags	= 0,
		.ir_req_fmt	= &DQF_DTP_GRP_CREATE,
		.ir_hdlr	= dtp_hdlr_grp_create,
		.ir_co_ops	= NULL,
	}, {
		.ir_name	= "DTP_GRP_DESTROY",
		.ir_opc		= DTP_OPC_GRP_DESTROY,
		.ir_ver		= 1,
		.ir_flags	= 0,
		.ir_req_fmt	= &DQF_DTP_GRP_DESTROY,
		.ir_hdlr	= dtp_hdlr_grp_destroy,
		.ir_co_ops	= NULL,
	}, {
		.ir_opc		= 0
	}
};

/* DTP RPC related APIs or internal functions */

int
dtp_internal_rpc_register(void)
{
	struct dtp_internal_rpc *rpc;
	int	rc = 0;

	/* walk through the handler list and register each individual RPC */
	for (rpc = dtp_internal_rpcs; rpc->ir_opc != 0; rpc++) {
		D_ASSERT(rpc->ir_hdlr != NULL);
		rc = dtp_rpc_reg_internal(rpc->ir_opc, rpc->ir_req_fmt,
					   rpc->ir_hdlr, rpc->ir_co_ops);
		if (rc) {
			D_ERROR("opcode 0x%x registration failed, rc: %d.\n",
				rpc->ir_opc, rc);
			break;
		}
	}
	return rc;
}

int
dtp_rpc_priv_alloc(dtp_opcode_t opc, struct dtp_rpc_priv **priv_allocated)
{
	struct dtp_rpc_priv	*rpc_priv;
	struct dtp_opc_info	*opc_info;
	int			rc = 0;

	D_ASSERT(priv_allocated != NULL);

	opc_info = dtp_opc_lookup(dtp_gdata.dg_opc_map, opc, DTP_UNLOCK);
	if (opc_info == NULL) {
		D_ERROR("opc: 0x%x, lookup failed.\n", opc);
		D_GOTO(out, rc = -DER_DTP_UNREG);
	}
	D_ASSERT(opc_info->doi_input_size <= DTP_MAX_INPUT_SIZE &&
		 opc_info->doi_output_size <= DTP_MAX_OUTPUT_SIZE);

	D_ALLOC_PTR(rpc_priv);
	if (rpc_priv == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rpc_priv->drp_opc_info = opc_info;
	*priv_allocated = rpc_priv;

out:
	return rc;
}

void
dtp_rpc_priv_free(struct dtp_rpc_priv *rpc_priv)
{
	if (rpc_priv == NULL)
		return;

	if (rpc_priv->drp_coll && rpc_priv->drp_corpc_info) {
		daos_rank_list_free(
			rpc_priv->drp_corpc_info->co_excluded_ranks);
		D_FREE_PTR(rpc_priv->drp_corpc_info);
	}

	pthread_spin_destroy(&rpc_priv->drp_lock);
	D_FREE_PTR(rpc_priv);
}

int
dtp_req_create(dtp_context_t dtp_ctx, dtp_endpoint_t tgt_ep, dtp_opcode_t opc,
	       dtp_rpc_t **req)
{
	struct dtp_context	*ctx;
	struct dtp_rpc_priv	*rpc_priv = NULL;
	dtp_rpc_t		*rpc_pub;
	int			rc = 0;

	if (dtp_ctx == DTP_CONTEXT_NULL || req == NULL) {
		D_ERROR("invalid parameter (NULL dtp_ctx or req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	/* TODO: possibly with multiple service group */
	if (tgt_ep.ep_rank >= dtp_gdata.dg_mcl_srv_set->size) {
		D_ERROR("invalid parameter, rank %d, group_size: %d.\n",
			tgt_ep.ep_rank, dtp_gdata.dg_mcl_srv_set->size);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dtp_rpc_priv_alloc(opc, &rpc_priv);
	if (rc != 0) {
		D_ERROR("dtp_rpc_priv_alloc, rc: %d, opc: 0x%x.\n", rc, opc);
		D_GOTO(out, rc);
	}

	D_ASSERT(rpc_priv != NULL);
	rpc_pub = &rpc_priv->drp_pub;
	rpc_pub->dr_ep = tgt_ep;

	rc = dtp_rpc_inout_buff_init(rpc_pub);
	if (rc != 0)
		D_GOTO(out, rc);

	dtp_rpc_priv_init(rpc_priv, dtp_ctx, opc, 0);

	ctx = (struct dtp_context *)dtp_ctx;
	rc = dtp_hg_req_create(&ctx->dc_hg_ctx, tgt_ep, rpc_priv);
	if (rc != 0) {
		D_ERROR("dtp_hg_req_create failed, rc: %d, opc: 0x%x.\n",
			rc, opc);
		D_GOTO(out, rc);
	}

	*req = rpc_pub;

out:
	if (rc < 0)
		dtp_rpc_priv_free(rpc_priv);
	return rc;
}

static inline int
dtp_corpc_info_init(struct dtp_rpc_priv *rpc_priv, dtp_group_t *grp,
		    daos_rank_list_t *excluded_ranks, dtp_bulk_t co_bulk_hdl,
		    void *priv, uint32_t flags, int tree_topo)
{
	struct dtp_corpc_info	*co_info;
	int			 rc = 0;

	D_ASSERT(rpc_priv != NULL);
	D_ASSERT(grp != NULL);

	D_ALLOC_PTR(co_info);
	if (co_info == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	co_info->co_grp_priv = container_of(grp, struct dtp_grp_priv, gp_pub);
	rc = daos_rank_list_dup(&co_info->co_excluded_ranks, excluded_ranks,
				true /* input */);
	if (rc != 0) {
		D_ERROR("daos_rank_list_dup failed, rc: %d.\n", rc);
		D_FREE_PTR(rpc_priv);
		D_GOTO(out, rc);
	}
	daos_rank_list_sort(co_info->co_excluded_ranks);
	rpc_priv->drp_pub.dr_co_bulk_hdl = co_bulk_hdl;
	co_info->co_priv = priv;
	co_info->co_tree_topo = tree_topo;
	co_info->co_grp_destroy =
		(flags & DTP_CORPC_FLAG_GRP_DESTROY) ? 1 : 0;
	co_info->co_parent_rpc = NULL;
	DAOS_INIT_LIST_HEAD(&co_info->co_child_rpcs);
	/* dtp_group_size(grp, &co_info->co_child_num); */
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
dtp_corpc_req_create(dtp_context_t dtp_ctx, dtp_group_t *grp,
		     daos_rank_list_t *excluded_ranks, dtp_opcode_t opc,
		     dtp_bulk_t co_bulk_hdl, void *priv,  uint32_t flags,
		     int tree_topo, dtp_rpc_t **req)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	dtp_rpc_t		*rpc_pub;
	int			rc = 0;

	if (dtp_ctx == DTP_CONTEXT_NULL || grp == NULL || req == NULL) {
		D_ERROR("invalid parameter (NULL dtp_ctx, grp or req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dtp_rpc_priv_alloc(opc, &rpc_priv);
	if (rc != 0) {
		D_ERROR("dtp_rpc_priv_alloc, rc: %d, opc: 0x%x.\n", rc, opc);
		D_GOTO(out, rc);
	}

	D_ASSERT(rpc_priv != NULL);
	rpc_pub = &rpc_priv->drp_pub;
	rc = dtp_rpc_inout_buff_init(rpc_pub);
	if (rc != 0)
		D_GOTO(out, rc);
	dtp_rpc_priv_init(rpc_priv, dtp_ctx, opc, 0);

	rc = dtp_corpc_info_init(rpc_priv, grp, excluded_ranks, co_bulk_hdl,
				 priv, flags, tree_topo);
	if (rc != 0) {
		D_ERROR("dtp_corpc_info_init failed, rc: %d, opc: 0x%x.\n",
			rc, opc);
		D_GOTO(out, rc);
	}

	*req = rpc_pub;
out:
	if (rc < 0)
		dtp_rpc_priv_free(rpc_priv);
	return rc;
}

int
dtp_req_addref(dtp_rpc_t *req)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	pthread_spin_lock(&rpc_priv->drp_lock);
	rpc_priv->drp_refcount++;
	pthread_spin_unlock(&rpc_priv->drp_lock);

out:
	return rc;
}

int
dtp_req_decref(dtp_rpc_t *req)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	int			rc = 0, destroy = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	pthread_spin_lock(&rpc_priv->drp_lock);
	rpc_priv->drp_refcount--;
	if (rpc_priv->drp_refcount == 0)
		destroy = 1;
	pthread_spin_unlock(&rpc_priv->drp_lock);

	if (destroy == 1) {
		rc = dtp_hg_req_destroy(rpc_priv);
		if (rc != 0)
			D_ERROR("dtp_hg_req_destroy failed, rc: %d, "
				"opc: 0x%x.\n", rc, req->dr_opc);
	}

out:
	return rc;
}

struct corpc_child_req {
	daos_list_t	 cr_link;
	dtp_rpc_t	*cr_rpc;
};

static inline int
corpc_add_child_rpc(struct dtp_rpc_priv *parent_rpc,
		    struct dtp_rpc_priv *child_rpc)
{
	struct dtp_corpc_info	*co_info;
	struct corpc_child_req	*child_req_item;
	int			rc = 0;

	D_ASSERT(parent_rpc != NULL);
	D_ASSERT(child_rpc != NULL);
	D_ASSERT(parent_rpc->drp_coll == 1 &&
		 parent_rpc->drp_corpc_info != NULL);

	co_info = parent_rpc->drp_corpc_info;

	D_ALLOC_PTR(child_req_item);
	if (child_req_item == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	DAOS_INIT_LIST_HEAD(&child_req_item->cr_link);
	child_req_item->cr_rpc = &child_rpc->drp_pub;

	rc = dtp_req_addref(&child_rpc->drp_pub);
	D_ASSERT(rc == 0);

	pthread_spin_lock(&parent_rpc->drp_lock);
	daos_list_add_tail(&child_req_item->cr_link, &co_info->co_child_rpcs);
	pthread_spin_unlock(&parent_rpc->drp_lock);

out:
	return rc;
}

static inline void
corpc_del_child_rpc(struct dtp_rpc_priv *parent_rpc,
		    struct dtp_rpc_priv *child_rpc)
{
	struct dtp_corpc_info	*co_info;
	struct corpc_child_req	*child_req_item, *next;
	int			rc = 0;

	D_ASSERT(parent_rpc != NULL);
	D_ASSERT(child_rpc != NULL);
	D_ASSERT(parent_rpc->drp_coll == 1 &&
		 parent_rpc->drp_corpc_info != NULL);

	co_info = parent_rpc->drp_corpc_info;
	pthread_spin_lock(&parent_rpc->drp_lock);
	daos_list_for_each_entry_safe(child_req_item, next,
				      &co_info->co_child_rpcs, cr_link) {
		if (child_req_item->cr_rpc == &child_rpc->drp_pub) {
			daos_list_del_init(&child_req_item->cr_link);
			/* decref corresponds to the addref in
			 * corpc_add_child_rpc */
			rc = dtp_req_decref(&child_rpc->drp_pub);
			D_ASSERT(rc == 0);
			D_FREE_PTR(child_req_item);
			break;
		}
	}
	pthread_spin_unlock(&parent_rpc->drp_lock);
}

static int
corpc_child_cb(const struct dtp_cb_info *cb_info)
{
	struct dtp_rpc_priv	*parent_rpc_priv;
	struct dtp_corpc_info	*co_info;
	struct dtp_rpc_priv	*child_rpc_priv;
	dtp_rpc_t		*child_req;
	struct dtp_opc_info	*opc_info;
	struct dtp_corpc_ops	*co_ops;
	daos_rank_t		 my_rank;
	bool			 req_done = false;
	int			 rc = 0;

	child_req = cb_info->dci_rpc;
	rc = cb_info->dci_rc;
	parent_rpc_priv = (struct dtp_rpc_priv *)cb_info->dci_arg;
	D_ASSERT(child_req != NULL && parent_rpc_priv != NULL);
	child_rpc_priv = container_of(child_req, struct dtp_rpc_priv, drp_pub);
	co_info = parent_rpc_priv->drp_corpc_info;
	D_ASSERT(co_info != NULL);
	D_ASSERT(parent_rpc_priv->drp_pub.dr_opc == child_req->dr_opc);
	opc_info = parent_rpc_priv->drp_opc_info;
	D_ASSERT(opc_info != NULL);
	co_ops = opc_info->doi_co_ops;

	dtp_group_rank(NULL, &my_rank);

	pthread_spin_lock(&parent_rpc_priv->drp_lock);
	if (rc != 0) {
		D_ERROR("RPC(opc: 0x%x) error, rc: %d.\n",
			child_req->dr_opc, rc);
		co_info->co_rc = rc;
	}
	co_info->co_child_ack_num++;
	D_ASSERT(co_info->co_child_num >= co_info->co_child_ack_num);
	if (co_info->co_child_num == co_info->co_child_ack_num)
		req_done = true;
	/* call user aggregate callback */
	if (co_ops != NULL) {
		D_ASSERT(co_ops->co_aggregate != NULL);
		rc = co_ops->co_aggregate(child_req, &parent_rpc_priv->drp_pub,
					  co_info->co_priv);
		if (rc != 0) {
			D_ERROR("co_ops->co_aggregate failed, rc: %d, "
				"opc: 0x%x.\n", rc, child_req->dr_opc);
			rc = 0;
		}
	}
	pthread_spin_unlock(&parent_rpc_priv->drp_lock);

	corpc_del_child_rpc(parent_rpc_priv, child_rpc_priv);

	if (req_done == false)
		D_GOTO(out, rc);

	dtp_rpc_complete(parent_rpc_priv, co_info->co_rc);

out:
	return rc;
}

int
dtp_corpc_send(dtp_rpc_t *req)
{
	struct dtp_corpc_info	*co_info;
	daos_rank_list_t	*member_ranks;
	struct dtp_rpc_priv	*rpc_priv, *child_rpc_priv;
	bool			child_req_sent = false;
	int			i, rc = 0;

	D_ASSERT(req != NULL);
	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	co_info = rpc_priv->drp_corpc_info;
	D_ASSERT(co_info != NULL);
	member_ranks = co_info->co_grp_priv->gp_membs;
	D_ASSERT(member_ranks != NULL);

	D_ASSERT(co_info->co_child_num == member_ranks->rl_nr.num);

	/* now send P2P RPC one by one */
	for (i = 0; i < co_info->co_child_num; i++) {
		dtp_rpc_t	*child_rpc;
		dtp_endpoint_t	 tgt_ep;

		if (daos_rank_in_rank_list(co_info->co_excluded_ranks,
					   member_ranks->rl_ranks[i])) {
			D_DEBUG(DF_TP, "rank %d in excluded list, ignored.\n",
				member_ranks->rl_ranks[i]);
			co_info->co_child_ack_num++;
			continue;
		}
		tgt_ep.ep_grp = NULL;
		tgt_ep.ep_rank = member_ranks->rl_ranks[i];
		tgt_ep.ep_tag = 0;
		rc = dtp_req_create(req->dr_ctx, tgt_ep, req->dr_opc,
				    &child_rpc);
		if (rc != 0) {
			D_ERROR("dtp_req_create(opc: 0x%x) failed, tgt_ep: %d, "
				"rc: %d.\n", req->dr_opc, tgt_ep.ep_rank, rc);
			co_info->co_child_ack_num += co_info->co_child_num - i;
			co_info->co_rc = rc;
			D_GOTO(out, rc);
		}

		D_ASSERT(child_rpc != NULL);
		D_ASSERT(child_rpc->dr_input_size == req->dr_input_size);
		D_ASSERT(child_rpc->dr_output_size == req->dr_output_size);
		child_rpc_priv = container_of(child_rpc, struct dtp_rpc_priv,
					      drp_pub);

		/* TODO: should avoid this memcpy */
		if (child_rpc->dr_input_size != 0) {
			D_ASSERT(child_rpc->dr_input != NULL);
			D_ASSERT(req->dr_input != NULL);
			memcpy(child_rpc->dr_input, req->dr_input,
			       req->dr_input_size);
		}
		rc = dtp_req_send(child_rpc, corpc_child_cb, rpc_priv);
		if (rc != 0) {
			D_ERROR("dtp_req_send(opc: 0x%x) failed, tgt_ep: %d, "
				"rc: %d.\n", req->dr_opc, tgt_ep.ep_rank, rc);
			co_info->co_child_ack_num += co_info->co_child_num - i;
			co_info->co_rc = rc;
			D_GOTO(out, rc);
		}
		rc = corpc_add_child_rpc(rpc_priv, child_rpc_priv);
		D_ASSERT(rc == 0);

		child_req_sent =  true;
	}

out:
	if (child_req_sent == false) {
		D_ASSERT(rc != 0);
		D_ERROR("dtp_corpc_send(rpc: 0x%x) failed, rc: %d.\n",
			req->dr_opc, rc);
		dtp_req_addref(req);
		dtp_rpc_complete(rpc_priv, rc);
		dtp_req_decref(req);
	}
	return rc;
}

int
dtp_req_send(dtp_rpc_t *req, dtp_cb_t complete_cb, void *arg)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (req->dr_ctx == NULL) {
		D_ERROR("invalid parameter (NULL req->dr_ctx).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	rpc_priv->drp_complete_cb = complete_cb;
	rpc_priv->drp_arg = arg;

	if (rpc_priv->drp_coll) {
		rc = dtp_corpc_send(req);
		if (rc != 0)
			D_ERROR("dtp_corpc_send failed, rc: %d, opc: 0x%x.\n",
				rc, req->dr_opc);
		D_GOTO(out, rc);
	}

	rc = dtp_context_req_track(req);
	if (rc == DTP_REQ_TRACK_IN_INFLIGHQ) {
		/* tracked in dtp_ep_inflight::epi_req_q */
		/* set state before sending to avoid race with complete_cb */
		rpc_priv->drp_state = RPC_REQ_SENT;
		rc = dtp_hg_req_send(rpc_priv);
		if (rc != 0) {
			D_ERROR("dtp_hg_req_send failed, rc: %d, opc: 0x%x.\n",
				rc, rpc_priv->drp_pub.dr_opc);
			rpc_priv->drp_state = RPC_INITED;
			dtp_context_req_untrack(req);
		}
	} else if (rc == DTP_REQ_TRACK_IN_WAITQ) {
		/* queued in dtp_hg_context::dhc_req_q */
		rc = 0;
	} else {
		D_ERROR("dtp_req_track failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
	}

out:
	/* internally destroy the req when failed */
	if (rc != 0 && req != NULL)
		dtp_req_decref(req);
	return rc;
}

int
dtp_reply_send(dtp_rpc_t *req)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	rc = dtp_hg_reply_send(rpc_priv);
	if (rc != 0) {
		D_ERROR("dtp_hg_reply_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
	}

out:
	return rc;
}

int
dtp_req_abort(dtp_rpc_t *req)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	rc = dtp_hg_req_cancel(rpc_priv);
	if (rc != 0) {
		D_ERROR("dtp_hg_req_cancel failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
	}

out:
	return rc;
}

#define DTP_DEFAULT_TIMEOUT	(20 * 1000 * 1000) /* Milli-seconds */

static int
dtp_cb_common(const struct dtp_cb_info *cb_info)
{
	*(int *)cb_info->dci_arg = 1;
	return 0;
}

/**
 * Send rpc synchronously
 *
 * \param[IN] rpc	point to DTP request.
 * \param[IN] timeout	timeout (Micro-seconds) to wait, if
 *                      timeout <= 0, it will wait infinitely.
 * \return		0 if rpc return successfuly.
 * \return		negative errno if sending fails or timeout.
 */
int
dtp_sync_req(dtp_rpc_t *rpc, uint64_t timeout)
{
	uint64_t now;
	uint64_t end;
	int rc;
	int complete = 0;

	/* Send request */
	rc = dtp_req_send(rpc, dtp_cb_common, &complete);
	if (rc != 0)
		return rc;

	/* Check if we are lucky */
	if (complete)
		return 0;

	timeout = timeout ? timeout : DTP_DEFAULT_TIMEOUT;
	/* Wait the request to be completed in timeout milliseconds */
	end = dtp_time_usec(0) + timeout;

	while (1) {
		uint64_t interval = 1000; /* milliseconds */

		rc = dtp_progress(rpc->dr_ctx, interval, NULL, NULL);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("dtp_progress failed rc: %d.\n", rc);
			break;
		}

		if (complete) {
			rc = 0;
			break;
		}

		now = dtp_time_usec(0);
		if (now >= end) {
			rc = -DER_TIMEDOUT;
			break;
		}
	}

	return rc;
}

void
dtp_rpc_priv_init(struct dtp_rpc_priv *rpc_priv, dtp_context_t dtp_ctx,
		  dtp_opcode_t opc, int srv_flag)
{
	D_ASSERT(rpc_priv != NULL);
	DAOS_INIT_LIST_HEAD(&rpc_priv->drp_epi_link);
	DAOS_INIT_LIST_HEAD(&rpc_priv->drp_tmp_link);
	rpc_priv->drp_complete_cb = NULL;
	rpc_priv->drp_arg = NULL;
	dtp_common_hdr_init(&rpc_priv->drp_req_hdr, opc);
	dtp_common_hdr_init(&rpc_priv->drp_reply_hdr, opc);
	rpc_priv->drp_state = RPC_INITED;
	rpc_priv->drp_srv = (srv_flag != 0);
	/* initialize as 1, so user can cal dtp_req_decref to destroy new req */
	rpc_priv->drp_refcount = 1;
	pthread_spin_init(&rpc_priv->drp_lock, PTHREAD_PROCESS_PRIVATE);

	rpc_priv->drp_pub.dr_opc = opc;
	rpc_priv->drp_pub.dr_ctx = dtp_ctx;
}

void
dtp_rpc_inout_buff_fini(dtp_rpc_t *rpc_pub)
{
	D_ASSERT(rpc_pub != NULL);

	if (rpc_pub->dr_input != NULL) {
		D_ASSERT(rpc_pub->dr_input_size != 0);
		D_FREE(rpc_pub->dr_input, rpc_pub->dr_input_size);
		rpc_pub->dr_input_size = 0;
	}

	if (rpc_pub->dr_output != NULL) {
		D_ASSERT(rpc_pub->dr_output_size != 0);
		D_FREE(rpc_pub->dr_output, rpc_pub->dr_output_size);
		rpc_pub->dr_output_size = 0;
	}
}

int
dtp_rpc_inout_buff_init(dtp_rpc_t *rpc_pub)
{
	struct dtp_rpc_priv	*rpc_priv;
	struct dtp_opc_info	*opc_info;
	int			rc = 0;

	D_ASSERT(rpc_pub != NULL);
	D_ASSERT(rpc_pub->dr_input == NULL);
	D_ASSERT(rpc_pub->dr_output == NULL);
	rpc_priv = container_of(rpc_pub, struct dtp_rpc_priv, drp_pub);
	opc_info = rpc_priv->drp_opc_info;
	D_ASSERT(opc_info != NULL);

	if (opc_info->doi_input_size > 0) {
		D_ALLOC(rpc_pub->dr_input, opc_info->doi_input_size);
		if (rpc_pub->dr_input == NULL) {
			D_ERROR("cannot allocate memory(size "DF_U64") for "
				"dr_input.\n", opc_info->doi_input_size);
			D_GOTO(out, rc = -DER_NOMEM);
		}
		rpc_pub->dr_input_size = opc_info->doi_input_size;
	}
	if (opc_info->doi_output_size > 0) {
		D_ALLOC(rpc_pub->dr_output, opc_info->doi_output_size);
		if (rpc_pub->dr_output == NULL) {
			D_ERROR("cannot allocate memory(size "DF_U64") for "
				"dr_putput.\n", opc_info->doi_input_size);
			D_GOTO(out, rc = -DER_NOMEM);
		}
		rpc_pub->dr_output_size = opc_info->doi_output_size;
	}

out:
	if (rc < 0)
		dtp_rpc_inout_buff_fini(rpc_pub);
	return rc;
}
