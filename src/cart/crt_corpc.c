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
 *
 * This file is part of CaRT. It implements the main collective RPC routines.
 */
#define D_LOGFAC	DD_FAC(corpc)

#include "crt_internal.h"

static inline int
crt_corpc_info_init(struct crt_rpc_priv *rpc_priv,
		    struct crt_grp_priv *grp_priv, bool grp_ref_taken,
		    d_rank_list_t *excluded_ranks, uint32_t grp_ver,
		    crt_bulk_t co_bulk_hdl, void *priv, uint32_t flags,
		    int tree_topo, d_rank_t grp_root, bool init_hdr,
		    bool root_excluded)
{
	struct crt_corpc_info	*co_info;
	struct crt_corpc_hdr	*co_hdr;
	int			 rc = 0;
	d_rank_list_t		*membs;

	D_ASSERT(rpc_priv != NULL);
	D_ASSERT(grp_priv != NULL);

	D_ALLOC_PTR(co_info);
	if (co_info == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = d_rank_list_dup_sort_uniq(&co_info->co_excluded_ranks,
				       excluded_ranks);
	if (rc != 0) {
		D_ERROR("d_rank_list_dup failed, rc: %d.\n", rc);
		D_FREE_PTR(co_info);
		D_GOTO(out, rc);
	}
	if (!grp_ref_taken)
		crt_grp_priv_addref(grp_priv);
	co_info->co_grp_ref_taken = 1;
	co_info->co_grp_priv = grp_priv;

	membs = grp_priv_get_membs(grp_priv);
	d_rank_list_filter(membs,
			   co_info->co_excluded_ranks, false /* exclude */);
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
		else if (flags & CRT_RPC_FLAG_GRP_DESTROY)
			rpc_priv->crp_flags |= CRT_RPC_FLAG_GRP_DESTROY;

		co_hdr->coh_int_grpid = grp_priv->gp_int_grpid;
		co_hdr->coh_excluded_ranks = co_info->co_excluded_ranks;
		co_hdr->coh_inline_ranks = NULL;
		co_hdr->coh_grp_ver = grp_ver;
		co_hdr->coh_tree_topo = tree_topo;
		co_hdr->coh_root = grp_root;
	}
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
	d_rank_list_free(rpc_priv->crp_corpc_info->co_excluded_ranks);
	if (rpc_priv->crp_corpc_info->co_grp_ref_taken)
		crt_grp_priv_decref(rpc_priv->crp_corpc_info->co_grp_priv);
	D_FREE_PTR(rpc_priv->crp_corpc_info);
}

static int
crt_corpc_initiate(struct crt_rpc_priv *rpc_priv)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_grp_priv	*grp_priv;
	struct crt_corpc_hdr	*co_hdr;
	bool			 grp_ref_taken = false;
	int			 rc = 0;

	D_ASSERT(rpc_priv != NULL && (rpc_priv->crp_flags & CRT_RPC_FLAG_COLL));
	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);

	co_hdr = &rpc_priv->crp_coreq_hdr;
	if (rpc_priv->crp_flags & CRT_RPC_FLAG_PRIMARY_GRP) {
		grp_priv = grp_gdata->gg_srv_pri_grp;
		D_ASSERT(grp_priv != NULL);
	} else {
		grp_priv = crt_grp_lookup_int_grpid(co_hdr->coh_int_grpid);
		if (grp_priv != NULL) {
			grp_ref_taken = true;
		} else {
			D_ERROR("crt_grp_lookup_int_grpid "DF_X64" failed.\n",
				co_hdr->coh_int_grpid);
			D_GOTO(out, rc = -DER_INVAL);
		}
	}

	rc = crt_corpc_info_init(rpc_priv, grp_priv, grp_ref_taken,
			co_hdr->coh_excluded_ranks,
			co_hdr->coh_grp_ver /* grp_ver */,
			rpc_priv->crp_pub.cr_co_bulk_hdl,
			NULL /* priv */, rpc_priv->crp_flags,
			co_hdr->coh_tree_topo, co_hdr->coh_root,
			false /* init_hdr */, false /* root_excluded */);
	if (rc != 0) {
		/* rollback refcount taken in above crt_grp_lookup_int_grpid */
		if (grp_ref_taken)
			crt_grp_priv_decref(grp_priv);
		D_ERROR("crt_corpc_info_init failed, rc: %d, opc: %#x.\n",
			rc, rpc_priv->crp_pub.cr_opc);
		D_GOTO(out, rc);
	}

	rc = crt_corpc_req_hdlr(rpc_priv);
	if (rc != 0)
		D_ERROR("crt_corpc_req_hdlr failed, rc: %d, opc: %#x.\n",
			rc, rpc_priv->crp_pub.cr_opc);

out:
	return rc;
}

static int
crt_corpc_chained_bulk_cb(const struct crt_bulk_cb_info *cb_info)
{
	crt_rpc_t			*rpc_req;
	struct crt_rpc_priv		*rpc_priv;
	struct crt_bulk_desc		*bulk_desc;
	crt_bulk_t			 local_bulk_hdl;
	void				*bulk_buf;
	int				 rc = 0;

	rc = cb_info->bci_rc;
	bulk_desc = cb_info->bci_bulk_desc;
	rpc_req = bulk_desc->bd_rpc;
	bulk_buf = cb_info->bci_arg;
	D_ASSERT(rpc_req != NULL && bulk_buf != NULL);

	rpc_priv = container_of(rpc_req, struct crt_rpc_priv, crp_pub);

	local_bulk_hdl = bulk_desc->bd_local_hdl;
	D_ASSERT(local_bulk_hdl != NULL);

	if (rc != 0) {
		D_ERROR("crt_corpc_chained_bulk_cb, bulk failed, rc: %d, "
			"opc: %#x.\n", rc, rpc_req->cr_opc);
		D_FREE(bulk_buf);
		D_GOTO(out, rc);
	}

	rpc_priv->crp_pub.cr_co_bulk_hdl = local_bulk_hdl;
	rc = crt_corpc_initiate(rpc_priv);
	if (rc != 0) {
		RPC_ERROR(rpc_priv,
			  "crt_corpc_initiate failed, rc: %d\n",
			  rc);
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
		D_ERROR("crt_bulk_access failed, rc: %d.\n", rc);
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
		D_ERROR("crt_bulk_access failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	for (i = 0; i < seg_num; i++)
		free(iovs[i].iov_buf);

	rc = crt_bulk_free(bulk_hdl);
	if (rc != 0)
		D_ERROR("crt_bulk_free failed, rc: %d.\n", rc);

out:
	if (iovs != NULL)
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

	if (!crt_is_service()) {
		D_ERROR("corpc invalid on client-side.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}
	if (!crt_initialized()) {
		D_ERROR("CaRT not initialized yet.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}

	/* handle possible chained bulk first and then initiate the corpc */
	co_hdr = &rpc_priv->crp_coreq_hdr;
	parent_bulk_hdl = co_hdr->coh_bulk_hdl;
	if (parent_bulk_hdl != CRT_BULK_NULL) {
		rc = crt_bulk_get_len(parent_bulk_hdl, &bulk_len);
		if (rc != 0 || bulk_len == 0) {
			D_ERROR("crt_bulk_get_len failed, rc: %d, opc: %#x.\n",
				rc, rpc_priv->crp_pub.cr_opc);
			D_GOTO(out, rc);
		}

		bulk_iov.iov_buf = calloc(1, bulk_len);
		if (bulk_iov.iov_buf == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		bulk_iov.iov_buf_len = bulk_len;
		bulk_sgl.sg_nr = 1;
		bulk_sgl.sg_iovs = &bulk_iov;

		rc = crt_bulk_create(rpc_priv->crp_pub.cr_ctx, &bulk_sgl,
				     CRT_BULK_RW, &local_bulk_hdl);
		if (rc != 0) {
			D_ERROR("crt_bulk_create failed, rc: %d, opc: %#x.\n",
				rc, rpc_priv->crp_pub.cr_opc);
			free(bulk_iov.iov_buf);
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
			D_ERROR("crt_bulk_transfer failed, rc: %d,opc: %#x.\n",
				rc, rpc_priv->crp_pub.cr_opc);
			D_FREE(bulk_iov.iov_buf);
			RPC_DECREF(rpc_priv);
		}
		D_GOTO(out, rc);
	} else {
		rpc_priv->crp_pub.cr_co_bulk_hdl = CRT_BULK_NULL;
		rc = crt_corpc_initiate(rpc_priv);
		if (rc != 0)
			D_ERROR("crt_corpc_initiate failed,rc: %d,opc: %#x.\n",
				rc, rpc_priv->crp_pub.cr_opc);
	}

out:
	if (rc != 0)
		D_ERROR("crt_corpc_common_hdlr failed, rc: %d, opc: %#x.\n",
			rc, rpc_priv->crp_pub.cr_opc);
	return rc;
}

int
crt_corpc_req_create(crt_context_t crt_ctx, crt_group_t *grp,
		     d_rank_list_t *excluded_ranks, crt_opcode_t opc,
		     crt_bulk_t co_bulk_hdl, void *priv,  uint32_t flags,
		     int tree_topo, crt_rpc_t **req)
{
	struct crt_grp_priv	*grp_priv = NULL;
	struct crt_grp_priv	*default_grp_priv;
	struct crt_grp_gdata	*grp_gdata;
	struct crt_rpc_priv	*rpc_priv = NULL;
	d_rank_list_t		*tobe_excluded_ranks = NULL;
	bool			 root_excluded = false;
	d_rank_t		 grp_root, pri_root;
	uint32_t		 grp_ver;
	int			 rc = 0;

	if (crt_ctx == CRT_CONTEXT_NULL || req == NULL) {
		D_ERROR("invalid parameter (NULL crt_ctx or req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (!crt_is_service()) {
		D_ERROR("corpc invalid on client-side.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}
	if (!crt_initialized()) {
		D_ERROR("CaRT not initialized yet.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	if (!crt_tree_topo_valid(tree_topo)) {
		D_ERROR("invalid parameter of tree_topo: %#x.\n", tree_topo);
		D_GOTO(out, rc = -DER_INVAL);
	}

	default_grp_priv = crt_grp_pub2priv(NULL);
	D_ASSERT(default_grp_priv != NULL);

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);
	if (grp == NULL) {
		grp_priv = grp_gdata->gg_srv_pri_grp;
	} else {
		grp_priv = container_of(grp, struct crt_grp_priv, gp_pub);
		if (grp_priv->gp_primary && !grp_priv->gp_local) {
			D_ERROR("cannot create corpc for attached group.\n");
			D_GOTO(out, rc = -DER_INVAL);
		}
	}

	rc = crt_rpc_priv_alloc(opc, &rpc_priv, false /* forward */);
	if (rc != 0) {
		D_ERROR("crt_rpc_priv_alloc, rc: %d, opc: %#x.\n", rc, opc);
		D_GOTO(out, rc);
	}

	D_ASSERT(rpc_priv != NULL);
	rc = crt_rpc_priv_init(rpc_priv, crt_ctx, false /* srv_flag */);
	if (rc != 0) {
		D_ERROR("crt_rpc_priv_init, rc: %d, opc: %#x.\n", rc, opc);
		D_GOTO(out, rc);
	}

	/* grp_root is logical rank number in this group */

	grp_root = grp_priv->gp_self;
	pri_root = grp_priv_get_primary_rank(grp_priv, grp_root);


	tobe_excluded_ranks = excluded_ranks;
	/*
	 * if bcast initiator is in excluded ranks, here we remove it and set
	 * a special flag to indicate need not to execute RPC handler.
	 */
	if (d_rank_in_rank_list(excluded_ranks, pri_root)) {
		d_rank_list_t		tmp_rank_list;
		d_rank_t		tmp_rank;

		tmp_rank = pri_root;
		tmp_rank_list.rl_nr = 1;
		tmp_rank_list.rl_ranks = &tmp_rank;

		rc =  d_rank_list_dup(&tobe_excluded_ranks, excluded_ranks);
		if (rc != 0)
			D_GOTO(out, rc);

		d_rank_list_filter(&tmp_rank_list, tobe_excluded_ranks,
				   true /* exclude */);
		root_excluded = true;
	}

	D_RWLOCK_RDLOCK(grp_priv->gp_rwlock_ft);
	grp_ver = default_grp_priv->gp_membs_ver;
	D_RWLOCK_UNLOCK(grp_priv->gp_rwlock_ft);

	rc = crt_corpc_info_init(rpc_priv, grp_priv, false, tobe_excluded_ranks,
				 grp_ver /* grp_ver */, co_bulk_hdl, priv,
				 flags, tree_topo, grp_root,
				 true /* init_hdr */, root_excluded);
	if (rc != 0) {
		D_ERROR("crt_corpc_info_init failed, rc: %d, opc: %#x.\n",
			rc, opc);
		D_GOTO(out, rc);
	}

	*req = &rpc_priv->crp_pub;
out:
	if (rc < 0)
		crt_rpc_priv_free(rpc_priv);
	if (root_excluded)
		d_rank_list_free(tobe_excluded_ranks);
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
	child_co_hdr->coh_int_grpid = parent_co_hdr->coh_int_grpid;
	/* child's coh_bulk_hdl is different with parent_co_hdr */
	child_co_hdr->coh_bulk_hdl = parent_rpc_priv->crp_pub.cr_co_bulk_hdl;
	child_co_hdr->coh_excluded_ranks = parent_co_hdr->coh_excluded_ranks;
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
	d_rank_t	 myrank;

	crt_group_rank(NULL, &myrank);

	parent_rpc_priv->crp_reply_hdr.cch_rc = failed_rc;
	parent_rpc_priv->crp_corpc_info->co_rc = failed_rc;
	D_ERROR("myrank %d, set parent rpc (opc %#x) as failed, rc: %d.\n",
		myrank, parent_rpc_priv->crp_pub.cr_opc, failed_rc);
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
		crt_rpc_complete(rpc_priv, co_info->co_rc);
		RPC_DECREF(rpc_priv); /* destroy */
	} else {
		if (co_info->co_rc != 0)
			crt_corpc_fail_parent_rpc(rpc_priv, co_info->co_rc);
		rc = crt_hg_reply_send(rpc_priv);
		if (rc != 0)
			D_ERROR("crt_hg_reply_send failed, rc: %d,opc: %#x.\n",
				rc, rpc_priv->crp_pub.cr_opc);
		/*
		 * on root node, don't need to free chained bulk handle as it is
		 * created and passed in by user.
		 */
		rc = crt_corpc_free_chained_bulk(
			rpc_priv->crp_coreq_hdr.coh_bulk_hdl);
		if (rc != 0)
			D_ERROR("crt_corpc_free_chainded_bulk failed, rc: %d, "
				"opc: %#x.\n", rc, rpc_priv->crp_pub.cr_opc);
		/*
		 * reset it to NULL to avoid crt_proc_corpc_hdr->
		 * crt_proc_crt_bulk_t free the bulk handle again.
		 */
		rpc_priv->crp_coreq_hdr.coh_bulk_hdl = NULL;
	}
	if (co_info->co_rc == 0 && co_info->co_root_excluded == 0 &&
	    (rpc_priv->crp_flags & CRT_RPC_FLAG_GRP_DESTROY))
		crt_grp_priv_decref(co_info->co_grp_priv);

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
	if (parent_rpc_priv == child_rpc_priv &&
	    child_req->cr_opc == CRT_OPC_RANK_EVICT &&
	    co_info->co_local_done != 1) {
		D_SPIN_UNLOCK(&parent_rpc_priv->crp_lock);
		D_GOTO(out, rc);
	}


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
		D_ERROR("RPC(opc: %#x) error, rc: %d.\n",
			child_req->cr_opc, rc);
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
				D_ERROR("co_ops->co_aggregate failed, rc: %d, "
					"opc: %#x.\n", rc, child_req->cr_opc);
				rc = 0;
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
				 * content to parent.
				 */
				memcpy(parent_rpc_priv->crp_pub.cr_output,
				       child_rpc_priv->crp_pub.cr_output,
				       parent_rpc_priv->crp_pub.cr_output_size);
			} else {
				D_ASSERT(co_ops->co_aggregate != NULL);
				rc = co_ops->co_aggregate(child_req,
					&parent_rpc_priv->crp_pub,
					co_info->co_priv);
				if (rc != 0) {
					D_ERROR("co_ops->co_aggregate failed, "
						"rc: %d, opc: %#x.\n",
						rc, child_req->cr_opc);
					rc = 0;
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

	if (req_done)
		crt_corpc_complete(parent_rpc_priv);

out:
	return;
}

int
crt_corpc_req_hdlr(struct crt_rpc_priv *rpc_priv)
{
	struct crt_corpc_info	*co_info;
	d_rank_list_t		*children_rank_list = NULL;
	d_rank_t		 grp_rank;
	struct crt_rpc_priv	*child_rpc_priv;
	struct crt_opc_info	*opc_info;
	struct crt_corpc_ops	*co_ops;
	bool			 ver_match;
	int			 i, rc = 0;

	co_info = rpc_priv->crp_corpc_info;
	D_ASSERT(co_info != NULL);

	grp_rank = co_info->co_grp_priv->gp_self;

	/* corresponds to decref in crt_corpc_complete */
	RPC_ADDREF(rpc_priv);

	if (co_info->co_root_excluded == 0 &&
	    rpc_priv->crp_pub.cr_opc == CRT_OPC_RANK_EVICT) {
		rc = crt_rpc_common_hdlr(rpc_priv);
		if (rc != 0) {
			RPC_ERROR(rpc_priv,
				  "crt_rpc_common_hdlr failed, rc: %d\n",
				  rc);
			crt_corpc_fail_parent_rpc(rpc_priv, rc);
			D_GOTO(forward_done, rc);
		}
	};

	opc_info = rpc_priv->crp_opc_info;
	co_ops = opc_info->coi_co_ops;

	/* Invoke pre-forward callback first if it is registered */
	if (co_ops && co_ops->co_pre_forward) {
		rc = co_ops->co_pre_forward(&rpc_priv->crp_pub,
					    co_info->co_priv);
		if (rc != 0) {
			RPC_ERROR(rpc_priv,
				  "co_pre_forward(group %s) failed, rc: %d\n",
				  co_info->co_grp_priv->gp_pub.cg_grpid,
				  rc);
			crt_corpc_fail_parent_rpc(rpc_priv, rc);
			D_GOTO(forward_done, rc);
		}
	}


	rc = crt_tree_get_children(co_info->co_grp_priv, co_info->co_grp_ver,
				   co_info->co_excluded_ranks,
				   co_info->co_tree_topo, co_info->co_root,
				   co_info->co_grp_priv->gp_self,
				   &children_rank_list, &ver_match);


	if (rc != 0) {
		RPC_ERROR(rpc_priv,
			  "crt_tree_get_children(group %s) failed, rc: %d\n",
			  co_info->co_grp_priv->gp_pub.cg_grpid,
			  rc);
		crt_corpc_fail_parent_rpc(rpc_priv, rc);
		D_GOTO(forward_done, rc);
	}

	co_info->co_child_num = (children_rank_list == NULL) ? 0 :
				children_rank_list->rl_nr;
	co_info->co_child_ack_num = 0;

	D_DEBUG(DB_TRACE, "group %s grp_rank %d, co_info->co_child_num: %d.\n",
		co_info->co_grp_priv->gp_pub.cg_grpid, grp_rank,
		co_info->co_child_num);

	if (!ver_match) {
		D_INFO("parent version and local version mismatch.\n");
		rc = -DER_MISMATCH;
		co_info->co_child_num = 0;
		crt_corpc_fail_parent_rpc(rpc_priv, rc);
		D_GOTO(forward_done, rc);
	}

	/* firstly forward RPC to children if any */
	for (i = 0; i < co_info->co_child_num; i++) {
		crt_rpc_t	*child_rpc;
		crt_endpoint_t	 tgt_ep = {0};

		tgt_ep.ep_rank = children_rank_list->rl_ranks[i];

		rc = crt_req_create_internal(rpc_priv->crp_pub.cr_ctx, &tgt_ep,
					     rpc_priv->crp_pub.cr_opc,
					     true /* forward */, &child_rpc);
		if (rc != 0) {
			RPC_ERROR(rpc_priv,
				  "crt_req_create failed, tgt_ep: %d, rc: %d\n",
				  tgt_ep.ep_rank, rc);
			crt_corpc_fail_child_rpc(rpc_priv,
						 co_info->co_child_num - i, rc);
			D_GOTO(forward_done, rc);
		}
		D_ASSERT(child_rpc != NULL);
		D_ASSERT(child_rpc->cr_output_size == rpc_priv->crp_pub.cr_output_size);
		D_ASSERT(child_rpc->cr_output_size == 0 ||
			 child_rpc->cr_output != NULL);
		D_ASSERT(child_rpc->cr_input_size == 0);
		D_ASSERT(child_rpc->cr_input == NULL);

		child_rpc_priv = container_of(child_rpc, struct crt_rpc_priv,
					      crp_pub);
		corpc_add_child_rpc(rpc_priv, child_rpc_priv);

		rc = crt_req_send(child_rpc, crt_corpc_reply_hdlr, rpc_priv);
		if (rc != 0) {
			RPC_ERROR(rpc_priv,
				  "crt_req_send failed, tgt_ep: %d, rc: %d\n",
				  tgt_ep.ep_rank, rc);
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

	if (co_info->co_root_excluded == 1)
		D_GOTO(out, rc);

	/* invoke RPC handler on local node */
	if (rpc_priv->crp_pub.cr_opc == CRT_OPC_RANK_EVICT) {
		struct crt_cb_info cb_info = {};

		D_SPIN_LOCK(&rpc_priv->crp_lock);
		co_info->co_local_done = 1;
		D_SPIN_UNLOCK(&rpc_priv->crp_lock);

		cb_info.cci_rpc = &rpc_priv->crp_pub;
		cb_info.cci_arg = rpc_priv;

		crt_corpc_reply_hdlr(&cb_info);
	} else {
		rc = crt_rpc_common_hdlr(rpc_priv);
		if (rc != 0)
			RPC_ERROR(rpc_priv,
				  "crt_rpc_common_hdlr failed, rc: %d\n",
				  rc);
	}


out:
	if (children_rank_list != NULL)
		d_rank_list_free(children_rank_list);

	return rc;
}
