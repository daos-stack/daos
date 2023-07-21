/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It implements the main collective RPC routines.
 */
#define D_LOGFAC	DD_FAC(corpc)

#include "crt_internal.h"

static inline int
crt_corpc_info_init(struct crt_rpc_priv *rpc_priv,
		    struct crt_grp_priv *grp_priv, bool grp_ref_taken,
		    d_rank_list_t *filter_ranks, uint32_t grp_ver,
		    crt_bulk_t co_bulk_hdl, void *priv, uint32_t flags,
		    int tree_topo, d_rank_t grp_root, bool init_hdr,
		    bool root_excluded)
{
	struct crt_corpc_info	*co_info;
	struct crt_corpc_hdr	*co_hdr;
	int			 rc;

	D_ASSERT(rpc_priv != NULL);
	D_ASSERT(grp_priv != NULL);

	D_ALLOC_PTR(co_info);
	if (co_info == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = d_rank_list_dup_sort_uniq(&co_info->co_filter_ranks, filter_ranks);
	if (rc != 0) {
		RPC_ERROR(rpc_priv, "d_rank_list_dup failed: "DF_RC"\n",
			  DP_RC(rc));
		D_FREE(co_info);
		D_GOTO(out, rc);
	}
	if (!grp_ref_taken)
		crt_grp_priv_addref(grp_priv);
	co_info->co_grp_ref_taken = 1;
	co_info->co_grp_priv = grp_priv;

	co_info->co_grp_ver = grp_ver;
	co_info->co_tree_topo = tree_topo;
	co_info->co_root = grp_root;
	co_info->co_root_excluded = root_excluded;

	rpc_priv->crp_pub.cr_co_bulk_hdl = co_bulk_hdl;
	co_info->co_priv = priv;
	D_INIT_LIST_HEAD(&co_info->co_child_rpcs);
	D_INIT_LIST_HEAD(&co_info->co_replied_rpcs);

	/* init the corpc header */
	co_hdr = &rpc_priv->crp_coreq_hdr;
	if (init_hdr) {
		rpc_priv->crp_flags |= CRT_RPC_FLAG_COLL;
		if (co_info->co_grp_priv->gp_primary)
			rpc_priv->crp_flags |= CRT_RPC_FLAG_PRIMARY_GRP;
		if (flags & CRT_RPC_FLAG_FILTER_INVERT)
			rpc_priv->crp_flags |= CRT_RPC_FLAG_FILTER_INVERT;

		co_hdr->coh_grpid = grp_priv->gp_pub.cg_grpid;
		co_hdr->coh_filter_ranks = co_info->co_filter_ranks;
		co_hdr->coh_inline_ranks = NULL;
		co_hdr->coh_grp_ver = grp_ver;
		co_hdr->coh_tree_topo = tree_topo;
		co_hdr->coh_root = grp_root;
	}

	D_ASSERT(co_hdr->coh_bulk_hdl == CRT_BULK_NULL);
	co_hdr->coh_bulk_hdl = co_bulk_hdl;

	rpc_priv->crp_corpc_info = co_info;
	rpc_priv->crp_coll = 1;

out:
	return rc;
}

void
crt_corpc_info_fini(struct crt_rpc_priv *rpc_priv)
{
	D_ASSERT(rpc_priv->crp_coll && rpc_priv->crp_corpc_info);
	d_rank_list_free(rpc_priv->crp_corpc_info->co_filter_ranks);
	if (rpc_priv->crp_corpc_info->co_grp_ref_taken)
		crt_grp_priv_decref(rpc_priv->crp_corpc_info->co_grp_priv);
	D_FREE(rpc_priv->crp_corpc_info);
}

static int
crt_corpc_initiate(struct crt_rpc_priv *rpc_priv)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_grp_priv	*grp_priv;
	struct crt_corpc_hdr	*co_hdr;
	int			 src_timeout;
	bool			 grp_ref_taken = false;
	int			 rc = 0;

	D_ASSERT(rpc_priv != NULL && (rpc_priv->crp_flags & CRT_RPC_FLAG_COLL));
	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);

	co_hdr = &rpc_priv->crp_coreq_hdr;
	if (rpc_priv->crp_flags & CRT_RPC_FLAG_PRIMARY_GRP) {
		grp_priv = grp_gdata->gg_primary_grp;
		D_ASSERT(grp_priv != NULL);
	} else {
		grp_priv = crt_grp_lookup_grpid(co_hdr->coh_grpid);
		if (grp_priv != NULL) {
			grp_ref_taken = true;
		} else {
			/* the local SG does not match others SG, so let's
			 * return GRPVER to retry until pool map is updated
			 * or the pool is stopped.
			 */
			RPC_ERROR(rpc_priv, "crt_grp_lookup_grpid: %s failed: "
				  DF_RC"\n", co_hdr->coh_grpid,
				  DP_RC(-DER_GRPVER));
			D_GOTO(out, rc = -DER_GRPVER);
		}
	}

	/* Inherit a timeout from a source */
	src_timeout = rpc_priv->crp_req_hdr.cch_src_timeout;

	if (src_timeout != 0)
		rpc_priv->crp_timeout_sec = src_timeout;

	rc = crt_corpc_info_init(rpc_priv, grp_priv, grp_ref_taken,
				 co_hdr->coh_filter_ranks,
				 co_hdr->coh_grp_ver /* grp_ver */,
				 rpc_priv->crp_pub.cr_co_bulk_hdl,
				 NULL /* priv */, rpc_priv->crp_flags,
				 co_hdr->coh_tree_topo, co_hdr->coh_root,
				 false /* init_hdr */,
				 false /* root_excluded */);
	if (rc != 0) {
		/* rollback refcount taken in above */
		if (grp_ref_taken)
			crt_grp_priv_decref(grp_priv);
		RPC_ERROR(rpc_priv, "crt_corpc_info_init failed: "DF_RC"\n",
			  DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = crt_corpc_req_hdlr(rpc_priv);
	if (rc != 0)
		RPC_ERROR(rpc_priv, "crt_corpc_req_hdlr failed: "DF_RC"\n",
			  DP_RC(rc));

out:
	return rc;
}

static int
crt_corpc_chained_bulk_cb(const struct crt_bulk_cb_info *cb_info)
{
	crt_rpc_t			*rpc_req;
	struct crt_rpc_priv		*rpc_priv;
	struct crt_corpc_hdr		*co_hdr;
	struct crt_bulk_desc		*bulk_desc;
	crt_bulk_t			 local_bulk_hdl;
	crt_bulk_t			 remote_bulk_hdl;
	void				*bulk_buf;
	int				 rc = 0;

	rc = cb_info->bci_rc;
	bulk_desc = cb_info->bci_bulk_desc;
	rpc_req = bulk_desc->bd_rpc;
	bulk_buf = cb_info->bci_arg;
	D_ASSERT(rpc_req != NULL && bulk_buf != NULL);

	rpc_priv = container_of(rpc_req, struct crt_rpc_priv, crp_pub);
	co_hdr = &rpc_priv->crp_coreq_hdr;

	local_bulk_hdl = bulk_desc->bd_local_hdl;
	remote_bulk_hdl = bulk_desc->bd_remote_hdl;
	D_ASSERT(local_bulk_hdl != NULL);

	/* chained bulk done, free remote_bulk_hdl, reset co_hdr->coh_bulk_hdl as NULL as
	 * crt_corpc_initiate() will reuse it as chained bulk handle for child RPC.
	 */
	D_ASSERT(remote_bulk_hdl != NULL && remote_bulk_hdl == co_hdr->coh_bulk_hdl);
	crt_bulk_free(remote_bulk_hdl);
	co_hdr->coh_bulk_hdl = NULL;

	if (rc != 0) {
		RPC_ERROR(rpc_priv, "crt_corpc_chained_bulk_cb, bulk failed: "
			  DF_RC"\n", DP_RC(rc));
		D_FREE(bulk_buf);
		D_GOTO(out, rc);
	}

	rpc_priv->crp_pub.cr_co_bulk_hdl = local_bulk_hdl;
	rc = crt_corpc_initiate(rpc_priv);
	if (rc != 0) {
		RPC_ERROR(rpc_priv, "crt_corpc_initiate failed: "DF_RC"\n",
			  DP_RC(rc));
		crt_hg_reply_error_send(rpc_priv, rc);
	}

out:
	RPC_DECREF(rpc_priv);
	return rc;
}

static int
crt_corpc_free_chained_bulk(crt_bulk_t bulk_hdl)
{
	d_iov_t		*iovs = NULL;
	d_sg_list_t	 sgl;
	uint32_t	 seg_num;
	int		 i, rc = 0;

	if (bulk_hdl == CRT_BULK_NULL)
		return 0;

	sgl.sg_nr = 0;
	sgl.sg_iovs = NULL;
	rc = crt_bulk_access(bulk_hdl, &sgl);
	if (rc != -DER_TRUNC) {
		if (rc == 0)
			rc = -DER_PROTO;
		D_ERROR("crt_bulk_access failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	seg_num = sgl.sg_nr_out;
	if (seg_num == 0) {
		D_ERROR("bad zero seg_num.\n");
		D_GOTO(out, rc = -DER_PROTO);
	}
	D_ALLOC_ARRAY(iovs, seg_num);
	if (iovs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	sgl.sg_nr = seg_num;
	sgl.sg_iovs = iovs;
	rc = crt_bulk_access(bulk_hdl, &sgl);
	if (rc != 0) {
		D_ERROR("crt_bulk_access failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	for (i = 0; i < seg_num; i++)
		D_FREE(iovs[i].iov_buf);

	rc = crt_bulk_free(bulk_hdl);
	if (rc != 0)
		D_ERROR("crt_bulk_free failed: "DF_RC"\n", DP_RC(rc));

out:
	D_FREE(iovs);
	return rc;
}

/* only be called in crt_rpc_handler_common after RPC header unpacked */
int
crt_corpc_common_hdlr(struct crt_rpc_priv *rpc_priv)
{
	struct crt_corpc_hdr	*co_hdr;
	crt_bulk_t		 parent_bulk_hdl, local_bulk_hdl;
	d_sg_list_t		 bulk_sgl;
	d_iov_t			 bulk_iov;
	size_t			 bulk_len;
	struct crt_bulk_desc	 bulk_desc;
	int			 rc = 0;

	D_ASSERT(rpc_priv != NULL && (rpc_priv->crp_flags & CRT_RPC_FLAG_COLL));

	if (!crt_initialized()) {
		D_ERROR("CaRT not initialized yet.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	if (!crt_is_service()) {
		D_ERROR("corpc invalid on client-side.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	/* handle possible chained bulk first and then initiate the corpc */
	co_hdr = &rpc_priv->crp_coreq_hdr;
	parent_bulk_hdl = co_hdr->coh_bulk_hdl;
	if (parent_bulk_hdl != CRT_BULK_NULL) {
		rc = crt_bulk_get_len(parent_bulk_hdl, &bulk_len);
		if (rc != 0 || bulk_len == 0) {
			RPC_ERROR(rpc_priv, "crt_bulk_get_len failed: "
				  DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}

		D_ALLOC(bulk_iov.iov_buf, bulk_len);
		if (bulk_iov.iov_buf == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		bulk_iov.iov_buf_len = bulk_len;
		bulk_sgl.sg_nr = 1;
		bulk_sgl.sg_iovs = &bulk_iov;

		rc = crt_bulk_create(rpc_priv->crp_pub.cr_ctx, &bulk_sgl,
				     CRT_BULK_RW, &local_bulk_hdl);
		if (rc != 0) {
			RPC_ERROR(rpc_priv, "crt_bulk_create failed: "
				  DF_RC"\n", DP_RC(rc));
			D_FREE(bulk_iov.iov_buf);
			D_GOTO(out, rc);
		}

		bulk_desc.bd_rpc = &rpc_priv->crp_pub;
		bulk_desc.bd_bulk_op = CRT_BULK_GET;
		bulk_desc.bd_remote_hdl = parent_bulk_hdl;
		bulk_desc.bd_remote_off = 0;
		bulk_desc.bd_local_hdl = local_bulk_hdl;
		bulk_desc.bd_local_off = 0;
		bulk_desc.bd_len = bulk_len;

		RPC_ADDREF(rpc_priv);

		rc = crt_bulk_transfer(&bulk_desc, crt_corpc_chained_bulk_cb,
				       bulk_iov.iov_buf, NULL);
		if (rc != 0) {
			RPC_ERROR(rpc_priv, "crt_bulk_transfer failed: "
				  DF_RC"\n", DP_RC(rc));
			D_FREE(bulk_iov.iov_buf);
			RPC_DECREF(rpc_priv);
		}
		D_GOTO(out, rc);
	} else {
		rpc_priv->crp_pub.cr_co_bulk_hdl = CRT_BULK_NULL;
		rc = crt_corpc_initiate(rpc_priv);
		if (rc != 0)
			RPC_ERROR(rpc_priv, "crt_corpc_initiate failed: "
				  DF_RC"\n", DP_RC(rc));
	}

out:
	if (rc != 0)
		RPC_ERROR(rpc_priv, "crt_corpc_common_hdlr failed: "
			  DF_RC"\n", DP_RC(rc));
	return rc;
}

int
crt_corpc_req_create(crt_context_t crt_ctx, crt_group_t *grp,
		     d_rank_list_t *filter_ranks, crt_opcode_t opc,
		     crt_bulk_t co_bulk_hdl, void *priv,  uint32_t flags,
		     int tree_topo, crt_rpc_t **req)
{
	struct crt_grp_priv	*grp_priv = NULL;
	struct crt_grp_gdata	*grp_gdata;
	struct crt_rpc_priv	*rpc_priv = NULL;
	d_rank_list_t		*tobe_filter_ranks = NULL;
	bool			 root_excluded = false;
	bool			 filter_invert;
	d_rank_t		 grp_root, pri_root;
	uint32_t		 grp_ver;
	int			 rc = 0;

	if (crt_ctx == CRT_CONTEXT_NULL || req == NULL) {
		D_ERROR("invalid parameter (NULL crt_ctx or req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (!crt_initialized()) {
		D_ERROR("CaRT not initialized yet.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	if (!crt_is_service()) {
		D_ERROR("corpc invalid on client-side.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}
	if (!crt_tree_topo_valid(tree_topo)) {
		D_ERROR("invalid parameter of tree_topo: %#x.\n", tree_topo);
		D_GOTO(out, rc = -DER_INVAL);
	}

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);

	grp_priv = crt_grp_pub2priv(grp);

	rc = crt_rpc_priv_alloc(opc, &rpc_priv, false /* forward */);
	if (rc != 0) {
		D_ERROR("crt_rpc_priv_alloc(opc: %#x) failed: "DF_RC"\n", opc,
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_ASSERT(rpc_priv != NULL);
	crt_rpc_priv_init(rpc_priv, crt_ctx, false /* srv_flag */);

	rpc_priv->crp_grp_priv = grp_priv;

	/* grp_root is logical rank number in this group */
	grp_root = grp_priv->gp_self;
	if (grp_root == CRT_NO_RANK) {
		D_DEBUG(DB_NET, "%s: self rank not known yet\n",
			grp_priv->gp_pub.cg_grpid);
		D_GOTO(out, rc = -DER_GRPVER);
	}
	pri_root = crt_grp_priv_get_primary_rank(grp_priv, grp_root);

	tobe_filter_ranks = filter_ranks;
	/*
	 * if bcast initiator is not in the scope, here we add it and set
	 * a special flag to indicate need not to execute RPC handler.
	 */
	rc = d_rank_in_rank_list(filter_ranks, pri_root);
	filter_invert = flags & CRT_RPC_FLAG_FILTER_INVERT;
	if ((filter_invert && !rc) || (!filter_invert && rc)) {
		rc = d_rank_list_dup(&tobe_filter_ranks, filter_ranks);
		if (rc != 0)
			D_GOTO(out, rc);

		root_excluded = true;

		/* make sure pri_root is in the scope */
		if (filter_invert) {
			rc = d_rank_list_append(tobe_filter_ranks, pri_root);
			if (rc != 0)
				D_GOTO(out, rc);
		} else {
			d_rank_list_t	tmp_rank_list;
			d_rank_t	tmp_rank;

			tmp_rank = pri_root;
			tmp_rank_list.rl_nr = 1;
			tmp_rank_list.rl_ranks = &tmp_rank;
			d_rank_list_filter(&tmp_rank_list, tobe_filter_ranks,
					   true /* exclude */);
		}
	}

	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
	grp_ver = grp_priv->gp_membs_ver;
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	rc = crt_corpc_info_init(rpc_priv, grp_priv, false, tobe_filter_ranks,
				 grp_ver /* grp_ver */, co_bulk_hdl, priv,
				 flags, tree_topo, grp_root,
				 true /* init_hdr */, root_excluded);
	if (rc != 0) {
		RPC_ERROR(rpc_priv, "crt_corpc_info_init failed: "DF_RC"\n",
			  DP_RC(rc));
		D_GOTO(out, rc);
	}

	*req = &rpc_priv->crp_pub;
out:
	if (rc < 0)
		crt_rpc_priv_free(rpc_priv);
	if (root_excluded)
		d_rank_list_free(tobe_filter_ranks);
	return rc;
}

static inline void
corpc_add_child_rpc(struct crt_rpc_priv *parent_rpc_priv,
		    struct crt_rpc_priv *child_rpc_priv)
{
	crt_rpc_t		*parent_rpc;
	crt_rpc_t		*child_rpc;
	struct crt_corpc_info	*co_info;
	struct crt_corpc_hdr	*parent_co_hdr, *child_co_hdr;

	D_ASSERT(parent_rpc_priv != NULL);
	D_ASSERT(child_rpc_priv != NULL);
	D_ASSERT(parent_rpc_priv->crp_coll == 1 &&
		 parent_rpc_priv->crp_corpc_info != NULL);
	D_ASSERT(child_rpc_priv->crp_forward);

	parent_rpc = &parent_rpc_priv->crp_pub;
	child_rpc = &child_rpc_priv->crp_pub;

	/*
	 * child RPC inherit input buffers from parent RPC, in crt_rpc_priv_init
	 * use rpc_priv->crp_forward flag to indicate it cr_input is not part of
	 * the rpc allocation.  see crt_rpc_inout_buff_fini.
	 */
	child_rpc->cr_input_size = parent_rpc->cr_input_size;
	child_rpc->cr_input = parent_rpc->cr_input;

	/* inherit crp_flag from parent */
	child_rpc_priv->crp_flags = parent_rpc_priv->crp_flags;

	/* inherit crp_coreq_hdr from parent */
	parent_co_hdr = &parent_rpc_priv->crp_coreq_hdr;
	child_co_hdr = &child_rpc_priv->crp_coreq_hdr;
	child_co_hdr->coh_grpid = parent_co_hdr->coh_grpid;
	/* child's coh_bulk_hdl is different with parent_co_hdr */
	child_co_hdr->coh_bulk_hdl = parent_rpc_priv->crp_pub.cr_co_bulk_hdl;
	child_co_hdr->coh_filter_ranks = parent_co_hdr->coh_filter_ranks;
	child_co_hdr->coh_inline_ranks = parent_co_hdr->coh_inline_ranks;
	child_co_hdr->coh_grp_ver = parent_co_hdr->coh_grp_ver;
	child_co_hdr->coh_tree_topo = parent_co_hdr->coh_tree_topo;
	child_co_hdr->coh_root = parent_co_hdr->coh_root;
	child_co_hdr->coh_padding = parent_co_hdr->coh_padding;

	co_info = parent_rpc_priv->crp_corpc_info;

	RPC_ADDREF(child_rpc_priv);

	D_SPIN_LOCK(&parent_rpc_priv->crp_lock);
	d_list_add_tail(&child_rpc_priv->crp_parent_link,
			&co_info->co_child_rpcs);
	D_SPIN_UNLOCK(&parent_rpc_priv->crp_lock);
}

static inline void
corpc_del_child_rpc_locked(struct crt_rpc_priv *parent_rpc_priv,
			   struct crt_rpc_priv *child_rpc_priv)
{
	D_ASSERT(parent_rpc_priv != NULL);
	D_ASSERT(child_rpc_priv != NULL);
	D_ASSERT(parent_rpc_priv->crp_coll == 1 &&
		 parent_rpc_priv->crp_corpc_info != NULL);

	d_list_del_init(&child_rpc_priv->crp_parent_link);
	/* decref corresponds to the addref in corpc_add_child_rpc */
	RPC_DECREF(child_rpc_priv);
}

static inline void
crt_corpc_fail_parent_rpc(struct crt_rpc_priv *parent_rpc_priv, int failed_rc)
{
	parent_rpc_priv->crp_reply_hdr.cch_rc = failed_rc;
	parent_rpc_priv->crp_corpc_info->co_rc = failed_rc;
	if (failed_rc != -DER_GRPVER)
		RPC_ERROR(parent_rpc_priv,
			  "CORPC failed: "DF_RC"\n", DP_RC(failed_rc));
	else
		RPC_TRACE(DB_NET, parent_rpc_priv,
			  "CORPC failed: "DF_RC"\n", DP_RC(failed_rc));
}

static inline void
crt_corpc_complete(struct crt_rpc_priv *rpc_priv)
{
	struct crt_corpc_info	*co_info;
	d_rank_t		 myrank;
	bool			 am_root;
	int			 rc;

	co_info = rpc_priv->crp_corpc_info;
	D_ASSERT(co_info != NULL);

	myrank = co_info->co_grp_priv->gp_self;
	am_root = (myrank == co_info->co_root);
	if (am_root) {
		crt_rpc_lock(rpc_priv);
		crt_rpc_complete_and_unlock(rpc_priv, co_info->co_rc);
	} else {
		if (co_info->co_rc != 0)
			crt_corpc_fail_parent_rpc(rpc_priv, co_info->co_rc);
		rc = crt_hg_reply_send(rpc_priv);
		if (rc != 0)
			RPC_ERROR(rpc_priv, "crt_hg_reply_send failed: "
				  DF_RC"\n", DP_RC(rc));
		/*
		 * on root node, don't need to free chained bulk handle as it is
		 * created and passed in by user.
		 */
		rc = crt_corpc_free_chained_bulk(
			rpc_priv->crp_coreq_hdr.coh_bulk_hdl);
		if (rc != 0)
			RPC_ERROR(rpc_priv,
				  "crt_corpc_free_chainded_bulk failed: "
				  DF_RC"\n", DP_RC(rc));
		/*
		 * reset it to NULL to avoid crt_proc_corpc_hdr->
		 * crt_proc_crt_bulk_t free the bulk handle again.
		 */
		rpc_priv->crp_coreq_hdr.coh_bulk_hdl = NULL;
	}

	/* correspond to addref in crt_corpc_req_hdlr */
	RPC_DECREF(rpc_priv);
}

static inline void
crt_corpc_fail_child_rpc(struct crt_rpc_priv *parent_rpc_priv,
			 uint32_t failed_num, int failed_rc)
{
	struct crt_corpc_info	*co_info;
	uint32_t		 wait_num;
	uint32_t		 done_num;
	bool			 req_done = false;

	D_ASSERT(parent_rpc_priv != NULL);
	co_info = parent_rpc_priv->crp_corpc_info;
	D_ASSERT(co_info != NULL);

	D_SPIN_LOCK(&parent_rpc_priv->crp_lock);

	wait_num = co_info->co_child_num;
	/* the extra +1 is for local RPC handler */
	if (co_info->co_root_excluded == 0)
		wait_num++;

	done_num = co_info->co_child_ack_num + co_info->co_child_failed_num;
	done_num += failed_num;
	D_ASSERT(done_num <= wait_num);
	co_info->co_rc = failed_rc;
	co_info->co_child_failed_num += failed_num;
	if (wait_num == done_num)
		req_done = true;
	crt_corpc_fail_parent_rpc(parent_rpc_priv, failed_rc);

	D_SPIN_UNLOCK(&parent_rpc_priv->crp_lock);

	if (req_done == true)
		crt_corpc_complete(parent_rpc_priv);
}

void
crt_corpc_reply_hdlr(const struct crt_cb_info *cb_info)
{
	struct crt_rpc_priv	*parent_rpc_priv;
	struct crt_corpc_info	*co_info;
	struct crt_rpc_priv	*child_rpc_priv;
	crt_rpc_t		*child_req;
	struct crt_opc_info	*opc_info;
	struct crt_corpc_ops	*co_ops;
	bool			 req_done = false;
	uint32_t		 wait_num, done_num;
	int			 rc = 0;

	child_req = cb_info->cci_rpc;
	parent_rpc_priv = cb_info->cci_arg;
	D_ASSERT(child_req != NULL && parent_rpc_priv != NULL);
	child_rpc_priv = container_of(child_req, struct crt_rpc_priv, crp_pub);
	co_info = parent_rpc_priv->crp_corpc_info;
	D_ASSERT(co_info != NULL);
	D_ASSERT(parent_rpc_priv->crp_pub.cr_opc == child_req->cr_opc);
	opc_info = parent_rpc_priv->crp_opc_info;
	D_ASSERT(opc_info != NULL);

	D_SPIN_LOCK(&parent_rpc_priv->crp_lock);

	wait_num = co_info->co_child_num;
	/* the extra +1 is for local RPC handler */
	if (co_info->co_root_excluded == 0) {
		wait_num++;
	} else {
		D_ASSERT(parent_rpc_priv != child_rpc_priv);
		co_info->co_local_done = 1;
	}

	rc = cb_info->cci_rc;
	if (rc != 0) {
		RPC_CERROR(crt_quiet_error(rc), DB_NET, child_rpc_priv, "error, rc: "DF_RC"\n",
			   DP_RC(rc));
		co_info->co_rc = rc;
	}
	/* propagate failure rc to parent */
	if (child_rpc_priv->crp_reply_hdr.cch_rc != 0)
		crt_corpc_fail_parent_rpc(parent_rpc_priv,
					  child_rpc_priv->crp_reply_hdr.cch_rc);

	co_ops = opc_info->coi_co_ops;
	if (co_ops == NULL) {
		co_info->co_child_ack_num++;
		if (parent_rpc_priv != child_rpc_priv)
			corpc_del_child_rpc_locked(parent_rpc_priv,
						   child_rpc_priv);
		goto aggregate_done;
	}

	if (parent_rpc_priv == child_rpc_priv) {
		struct crt_rpc_priv	*tmp_rpc_priv, *next;

		co_info->co_local_done = 1;
		/* aggregate previously replied RPCs */
		d_list_for_each_entry_safe(tmp_rpc_priv, next,
					   &co_info->co_replied_rpcs,
					   crp_parent_link) {
			D_ASSERT(tmp_rpc_priv != parent_rpc_priv);
			D_ASSERT(co_ops->co_aggregate != NULL);
			rc = co_ops->co_aggregate(&tmp_rpc_priv->crp_pub,
						  &parent_rpc_priv->crp_pub,
						  co_info->co_priv);
			if (rc != 0) {
				D_ERROR("co_ops->co_aggregate(opc: %#x) "
					"failed: "DF_RC"\n",
					child_req->cr_opc, DP_RC(rc));
				if (co_info->co_rc == 0)
					co_info->co_rc = rc;
			}
			co_info->co_child_ack_num++;
			D_DEBUG(DB_NET, "parent rpc %p, child rpc %p, "
				"wait_num %d, ack_num %d.\n", parent_rpc_priv,
				child_rpc_priv, wait_num,
				co_info->co_child_ack_num);
			corpc_del_child_rpc_locked(parent_rpc_priv,
						   tmp_rpc_priv);
		}
	}

	/* reply aggregate */
	if (co_info->co_local_done == 1) {
		if (child_rpc_priv != parent_rpc_priv) {
			if (co_info->co_root_excluded == 1 &&
			    co_info->co_child_ack_num == 0 &&
			    parent_rpc_priv->crp_pub.cr_output_size > 0) {
				/*
				 * when root excluded, copy first reply's
				 * content to parent and zero child's copy so
				 * that any pointers contained therein belong
				 * solely to parent.
				 */
				memcpy(parent_rpc_priv->crp_pub.cr_output,
				       child_rpc_priv->crp_pub.cr_output,
				       parent_rpc_priv->crp_pub.cr_output_size);
				memset(child_rpc_priv->crp_pub.cr_output, 0,
				       child_rpc_priv->crp_pub.cr_output_size);
			} else {
				D_ASSERT(co_ops->co_aggregate != NULL);
				rc = co_ops->co_aggregate(child_req,
					&parent_rpc_priv->crp_pub,
					co_info->co_priv);
				if (rc != 0) {
					D_ERROR("co_ops->co_aggregate(opc: %#x)"
						" failed: "DF_RC"\n",
						child_req->cr_opc, DP_RC(rc));
					if (co_info->co_rc == 0)
						co_info->co_rc = rc;
				}
			}
		}
		co_info->co_child_ack_num++;
		D_DEBUG(DB_NET, "parent rpc %p, child rpc %p, wait_num %d, "
			"ack_num %d.\n", parent_rpc_priv, child_rpc_priv,
			wait_num, co_info->co_child_ack_num);
		if (parent_rpc_priv != child_rpc_priv)
			corpc_del_child_rpc_locked(parent_rpc_priv,
						   child_rpc_priv);
	} else {
		D_ASSERT(wait_num > co_info->co_child_ack_num);
		d_list_move_tail(&child_rpc_priv->crp_parent_link,
				 &co_info->co_replied_rpcs);
		D_DEBUG(DB_NET, "parent rpc %p, child rpc %p move to "
			"replided rpcs.\n", parent_rpc_priv, child_rpc_priv);
	}

aggregate_done:
	done_num = co_info->co_child_ack_num + co_info->co_child_failed_num;
	D_ASSERT(wait_num >= done_num);
	if (wait_num == done_num)
		req_done = true;

	D_SPIN_UNLOCK(&parent_rpc_priv->crp_lock);

	if (req_done) {
		bool am_root;

		RPC_ADDREF(parent_rpc_priv);
		crt_corpc_complete(parent_rpc_priv);

		am_root = (co_info->co_grp_priv->gp_self ==
			   co_info->co_root);
		if (co_ops && co_ops->co_post_reply && !am_root)
			co_ops->co_post_reply(&parent_rpc_priv->crp_pub,
					co_info->co_priv);
		RPC_DECREF(parent_rpc_priv);
	}

	if (parent_rpc_priv != child_rpc_priv) {
		/* Corresponding ADDREF done before crt_req_send() */
		RPC_DECREF(parent_rpc_priv);
	}
}

int
crt_corpc_req_hdlr(struct crt_rpc_priv *rpc_priv)
{
	struct crt_corpc_info	*co_info;
	d_rank_list_t		*children_rank_list = NULL;
	struct crt_rpc_priv	*child_rpc_priv;
	struct crt_opc_info	*opc_info;
	struct crt_corpc_ops	*co_ops;
	bool			 ver_match;
	int			 i, rc = 0;

	co_info = rpc_priv->crp_corpc_info;
	D_ASSERT(co_info != NULL);

	/* corresponds to decref in crt_corpc_complete */
	RPC_ADDREF(rpc_priv);

	opc_info = rpc_priv->crp_opc_info;
	co_ops = opc_info->coi_co_ops;

	if (rpc_priv->crp_fail_hlc)
		D_GOTO(forward_done, rc = -DER_HLC_SYNC);

	/* Invoke pre-forward callback first if it is registered */
	if (co_ops && co_ops->co_pre_forward) {
		rc = co_ops->co_pre_forward(&rpc_priv->crp_pub,
					    co_info->co_priv);
		if (rc != 0) {
			RPC_ERROR(rpc_priv,
				  "co_pre_forward(group %s) failed: "DF_RC"\n",
				  co_info->co_grp_priv->gp_pub.cg_grpid,
				  DP_RC(rc));
			crt_corpc_fail_parent_rpc(rpc_priv, rc);
			D_GOTO(forward_done, rc);
		}
	}

	/*
	 * Check the self rank after calling the pre-forward callback, which
	 * might have changed the self rank from CRT_NO_RANK to a valid value.
	 */
	if (co_info->co_grp_priv->gp_self == CRT_NO_RANK) {
		RPC_TRACE(DB_NET, rpc_priv, "%s: self rank not known yet\n",
			  co_info->co_grp_priv->gp_pub.cg_grpid);
		rc = -DER_GRPVER;
		crt_corpc_fail_parent_rpc(rpc_priv, rc);
		D_GOTO(forward_done, rc);
	}

	rc = crt_tree_get_children(co_info->co_grp_priv, co_info->co_grp_ver,
				   rpc_priv->crp_flags &
				   CRT_RPC_FLAG_FILTER_INVERT,
				   co_info->co_filter_ranks,
				   co_info->co_tree_topo, co_info->co_root,
				   co_info->co_grp_priv->gp_self,
				   &children_rank_list, &ver_match);

	if (rc != 0) {
		RPC_CERROR(crt_quiet_error(rc), DB_NET, rpc_priv,
			   "crt_tree_get_children(group %s) failed: "DF_RC"\n",
			   co_info->co_grp_priv->gp_pub.cg_grpid, DP_RC(rc));
		crt_corpc_fail_parent_rpc(rpc_priv, rc);
		D_GOTO(forward_done, rc);
	}

	co_info->co_child_num = (children_rank_list == NULL) ? 0 :
				children_rank_list->rl_nr;
	co_info->co_child_ack_num = 0;

	D_DEBUG(DB_TRACE, "group %s grp_rank %d, co_info->co_child_num: %d.\n",
		co_info->co_grp_priv->gp_pub.cg_grpid,
		co_info->co_grp_priv->gp_self, co_info->co_child_num);

	if (!ver_match) {
		D_INFO("parent version and local version mismatch.\n");
		rc = -DER_GRPVER;
		co_info->co_child_num = 0;
		crt_corpc_fail_parent_rpc(rpc_priv, rc);
		D_GOTO(forward_done, rc);
	}

	/* firstly forward RPC to children if any */
	for (i = 0; i < co_info->co_child_num; i++) {
		crt_rpc_t	*child_rpc;
		crt_endpoint_t	 tgt_ep = {0};

		tgt_ep.ep_rank = children_rank_list->rl_ranks[i];
		tgt_ep.ep_tag = rpc_priv->crp_pub.cr_ep.ep_tag;
		tgt_ep.ep_grp = &co_info->co_grp_priv->gp_pub;

		rc = crt_req_create_internal(rpc_priv->crp_pub.cr_ctx, &tgt_ep,
					     rpc_priv->crp_pub.cr_opc,
					     true /* forward */, &child_rpc);
		if (rc != 0) {
			RPC_ERROR(rpc_priv,
				  "crt_req_create(tgt_ep: %d) failed: "
				  DF_RC"\n", tgt_ep.ep_rank, DP_RC(rc));
			crt_corpc_fail_child_rpc(rpc_priv,
						 co_info->co_child_num - i, rc);
			D_GOTO(forward_done, rc);
		}
		D_ASSERT(child_rpc != NULL);
		D_ASSERT(child_rpc->cr_output_size ==
			rpc_priv->crp_pub.cr_output_size);
		D_ASSERT(child_rpc->cr_output_size == 0 ||
			 child_rpc->cr_output != NULL);
		D_ASSERT(child_rpc->cr_input_size == 0);
		D_ASSERT(child_rpc->cr_input == NULL);

		child_rpc_priv = container_of(child_rpc, struct crt_rpc_priv,
					      crp_pub);

		child_rpc_priv->crp_timeout_sec = rpc_priv->crp_timeout_sec;
		corpc_add_child_rpc(rpc_priv, child_rpc_priv);

		child_rpc_priv->crp_grp_priv = co_info->co_grp_priv;

		RPC_ADDREF(rpc_priv);
		rc = crt_req_send(child_rpc, crt_corpc_reply_hdlr, rpc_priv);
		if (rc != 0) {
			RPC_ERROR(rpc_priv,
				  "crt_req_send(tgt_ep: %d) failed: "
				  DF_RC"\n", tgt_ep.ep_rank, DP_RC(rc));
			RPC_DECREF(rpc_priv);

			/*
			 * in the case of failure, the crt_corpc_reply_hdlr
			 * will be called for this child_rpc, so just need
			 * to fail rest child rpcs
			 */
			if (i != (co_info->co_child_num - 1))
				crt_corpc_fail_child_rpc(rpc_priv,
					co_info->co_child_num - i - 1, rc);
			D_GOTO(forward_done, rc);
		}
	}

forward_done:
	/* NOOP bcast (no child and root excluded) */
	if (co_info->co_child_num == 0 && co_info->co_root_excluded)
		crt_corpc_complete(rpc_priv);

	if (co_info->co_root_excluded == 1) {
		if (co_info->co_grp_priv->gp_self == co_info->co_root) {
			/* don't return error for root */
			rc = 0;
		}
		D_GOTO(out, rc);
	}

	/* invoke RPC handler on local node */
	rc = crt_rpc_common_hdlr(rpc_priv);
	if (rc != 0) {
		RPC_ERROR(rpc_priv, "crt_rpc_common_hdlr failed: "DF_RC"\n",
			  DP_RC(rc));
		crt_corpc_fail_child_rpc(rpc_priv, 1, rc);

		D_SPIN_LOCK(&rpc_priv->crp_lock);
		co_info->co_local_done = 1;
		rpc_priv->crp_reply_pending = 0;
		D_SPIN_UNLOCK(&rpc_priv->crp_lock);

		/* Handle ref count difference between call on root vs
		 * call on intermediate nodes
		 */
		if (co_info->co_root != co_info->co_grp_priv->gp_self)
			RPC_DECREF(rpc_priv);

		rc = 0;
	}

out:
	if (children_rank_list != NULL)
		d_rank_list_free(children_rank_list);

	return rc;
}
