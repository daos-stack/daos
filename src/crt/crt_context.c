/* Copyright (C) 2016 Intel Corporation
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
 * This file is part of CaRT. It implements the CaRT context related APIs.
 */
#include <abt.h>
#include <crt_internal.h>

static void crt_epi_destroy(struct crt_ep_inflight *epi);

static struct crt_ep_inflight *
epi_link2ptr(crt_list_t *rlink)
{
	C_ASSERT(rlink != NULL);
	return container_of(rlink, struct crt_ep_inflight, epi_link);
}

static int
epi_op_key_get(struct chash_table *hhtab, crt_list_t *rlink, void **key_pp)
{
	struct crt_ep_inflight *epi = epi_link2ptr(rlink);

	/* TODO: use global rank */
	*key_pp = (void *)&epi->epi_ep.ep_rank;
	return sizeof(epi->epi_ep.ep_rank);
}

static uint32_t
epi_op_key_hash(struct chash_table *hhtab, const void *key, unsigned int ksize)
{
	C_ASSERT(ksize == sizeof(crt_rank_t));

	return (unsigned int)(*(const uint32_t *)key %
		(1U << CRT_EPI_TABLE_BITS));
}

static bool
epi_op_key_cmp(struct chash_table *hhtab, crt_list_t *rlink,
	  const void *key, unsigned int ksize)
{
	struct crt_ep_inflight *epi = epi_link2ptr(rlink);

	C_ASSERT(ksize == sizeof(crt_rank_t));
	/* TODO: use global rank */

	return epi->epi_ep.ep_rank == *(crt_rank_t *)key;
}

static void
epi_op_rec_addref(struct chash_table *hhtab, crt_list_t *rlink)
{
	epi_link2ptr(rlink)->epi_ref++;
}

static bool
epi_op_rec_decref(struct chash_table *hhtab, crt_list_t *rlink)
{
	struct crt_ep_inflight *epi = epi_link2ptr(rlink);

	epi->epi_ref--;
	return epi->epi_ref == 0;
}

static void
epi_op_rec_free(struct chash_table *hhtab, crt_list_t *rlink)
{
	crt_epi_destroy(epi_link2ptr(rlink));
}

static chash_table_ops_t epi_table_ops = {
	.hop_key_get		= epi_op_key_get,
	.hop_key_hash		= epi_op_key_hash,
	.hop_key_cmp		= epi_op_key_cmp,
	.hop_rec_addref		= epi_op_rec_addref,
	.hop_rec_decref		= epi_op_rec_decref,
	.hop_rec_free		= epi_op_rec_free,
};

static void
crt_epi_destroy(struct crt_ep_inflight *epi)
{
	C_ASSERT(epi != NULL);

	C_ASSERT(epi->epi_ref == 0);
	C_ASSERT(epi->epi_initialized == 1);

	C_ASSERT(crt_list_empty(&epi->epi_req_waitq));
	C_ASSERT(epi->epi_req_wait_num == 0);

	C_ASSERT(crt_list_empty(&epi->epi_req_q));
	C_ASSERT(epi->epi_req_num >= epi->epi_reply_num);

	/* crt_list_del_init(&epi->epi_link); */
	pthread_mutex_destroy(&epi->epi_mutex);

	C_FREE_PTR(epi);
}

static int
crt_context_init(crt_context_t crt_ctx)
{
	struct crt_context	*ctx;
	uint32_t		 bh_node_cnt;
	int			 rc;

	C_ASSERT(crt_ctx != NULL);
	ctx = crt_ctx;

	CRT_INIT_LIST_HEAD(&ctx->cc_link);

	/* create timeout binheap */
	bh_node_cnt = CRT_MAX_INFLIGHT_PER_EP_CTX * 64;
	rc = crt_binheap_create_inplace(CBH_FT_NOLOCK, bh_node_cnt,
					NULL /* priv */, &crt_timeout_bh_ops,
					&ctx->cc_bh_timeout);
	if (rc != 0) {
		C_ERROR("crt_binheap_create_inplace failed, rc: %d.\n", rc);
		C_GOTO(out, rc);
	}

	/* create epi table, use external lock */
	rc =  chash_table_create_inplace(DHASH_FT_NOLOCK, CRT_EPI_TABLE_BITS,
			NULL, &epi_table_ops, &ctx->cc_epi_table);
	if (rc != 0) {
		C_ERROR("chash_table_create_inplace failed, rc: %d.\n", rc);
		crt_binheap_destroy_inplace(&ctx->cc_bh_timeout);
		C_GOTO(out, rc);
	}

	pthread_mutex_init(&ctx->cc_mutex, NULL);

out:
	return rc;
}

int
crt_context_create(void *arg, crt_context_t *crt_ctx)
{
	struct crt_context	*ctx = NULL;
	int			rc = 0;

	if (crt_ctx == NULL) {
		C_ERROR("invalid parameter of NULL crt_ctx.\n");
		C_GOTO(out, rc = -CER_INVAL);
	}

	C_ALLOC_PTR(ctx);
	if (ctx == NULL)
		C_GOTO(out, rc = -CER_NOMEM);

	rc = crt_context_init(ctx);
	if (rc != 0) {
		C_ERROR("crt_context_init failed, rc: %d.\n", rc);
		C_FREE_PTR(ctx);
		C_GOTO(out, rc);
	}

	pthread_rwlock_wrlock(&crt_gdata.cg_rwlock);

	rc = crt_hg_ctx_init(&ctx->cc_hg_ctx, crt_gdata.cg_ctx_num);
	if (rc != 0) {
		C_ERROR("crt_hg_ctx_init failed rc: %d.\n", rc);
		C_FREE_PTR(ctx);
		pthread_rwlock_unlock(&crt_gdata.cg_rwlock);
		C_GOTO(out, rc);
	}

	ctx->cc_idx = crt_gdata.cg_ctx_num;
	crt_list_add_tail(&ctx->cc_link, &crt_gdata.cg_ctx_list);
	crt_gdata.cg_ctx_num++;

	ctx->cc_pool = arg;
	pthread_rwlock_unlock(&crt_gdata.cg_rwlock);

	*crt_ctx = (crt_context_t)ctx;

out:
	return rc;
}

void
crt_rpc_complete(struct crt_rpc_priv *rpc_priv, int rc)
{
	C_ASSERT(rpc_priv != NULL);

	if (rc == -CER_CANCELED)
		rpc_priv->crp_state = RPC_STATE_CANCELED;
	else if (rc == -CER_TIMEDOUT)
		rpc_priv->crp_state = RPC_STATE_TIMEOUT;
	else
		rpc_priv->crp_state = RPC_STATE_COMPLETED;

	if (rpc_priv->crp_complete_cb != NULL) {
		struct crt_cb_info	cbinfo;

		cbinfo.cci_rpc = &rpc_priv->crp_pub;
		cbinfo.cci_arg = rpc_priv->crp_arg;
		cbinfo.cci_rc = rc;
		if (cbinfo.cci_rc == 0)
			cbinfo.cci_rc = rpc_priv->crp_reply_hdr.cch_co_rc;
		rc = rpc_priv->crp_complete_cb(&cbinfo);
		if (rc != 0)
			C_ERROR("req_cbinfo->rsc_cb returned %d.\n", rc);
	}
}

/* abort the RPCs in inflight queue and waitq in the epi. */
static int
crt_ctx_epi_abort(crt_list_t *rlink, void *args)
{
	struct crt_ep_inflight	*epi;
	struct crt_context	*ctx;
	struct crt_rpc_priv	*rpc_priv, *rpc_next;
	bool			msg_logged;
	int			force;
	int			rc = 0;

	C_ASSERT(rlink != NULL);
	C_ASSERT(args != NULL);
	epi = epi_link2ptr(rlink);
	ctx = epi->epi_ctx;
	C_ASSERT(ctx != NULL);

	/* empty queue, nothing to do */
	if (crt_list_empty(&epi->epi_req_waitq) &&
	    crt_list_empty(&epi->epi_req_q))
		C_GOTO(out, rc = 0);

	force = *(int *)args;
	if (force == 0) {
		C_ERROR("cannot abort endpoint (idx %d, rank %d, req_wait_num "
			CF_U64", req_num "CF_U64", reply_num "CF_U64", "
			"inflight "CF_U64", with force == 0.\n", ctx->cc_idx,
			epi->epi_ep.ep_rank, epi->epi_req_wait_num,
			epi->epi_req_num, epi->epi_reply_num,
			epi->epi_req_num - epi->epi_reply_num);
		C_GOTO(out, rc = -CER_BUSY);
	}

	/* abort RPCs in waitq */
	msg_logged = false;
	crt_list_for_each_entry_safe(rpc_priv, rpc_next, &epi->epi_req_waitq,
				      crp_epi_link) {
		C_ASSERT(epi->epi_req_wait_num > 0);
		if (msg_logged == false) {
			C_DEBUG("destroy context (idx %d, rank %d, "
				"req_wait_num "CF_U64").\n", ctx->cc_idx,
				epi->epi_ep.ep_rank, epi->epi_req_wait_num);
			msg_logged = true;
		}
		/* Just remove from wait_q, decrease the wait_num and destroy
		 * the request. Trigger the possible completion callback. */
		C_ASSERT(rpc_priv->crp_state == RPC_STATE_QUEUED);
		crt_list_del_init(&rpc_priv->crp_epi_link);
		epi->epi_req_wait_num--;
		crt_rpc_complete(rpc_priv, -CER_CANCELED);
		/* corresponds to ref taken when adding to waitq */
		crt_req_decref(&rpc_priv->crp_pub);
	}

	/* abort RPCs in inflight queue */
	msg_logged = false;
	crt_list_for_each_entry_safe(rpc_priv, rpc_next, &epi->epi_req_q,
				      crp_epi_link) {
		C_ASSERT(epi->epi_req_num > epi->epi_reply_num);
		if (msg_logged == false) {
			C_DEBUG("destroy context (idx %d, rank %d, "
				"epi_req_num "CF_U64", epi_reply_num "
				""CF_U64", inflight "CF_U64").\n",
				ctx->cc_idx, epi->epi_ep.ep_rank,
				epi->epi_req_num, epi->epi_reply_num,
				epi->epi_req_num - epi->epi_reply_num);
			msg_logged = true;
		}

		rc = crt_req_abort(&rpc_priv->crp_pub);
		if (rc != 0) {
			C_ERROR("crt_req_abort(opc: 0x%x) failed, rc: %d.\n",
				rpc_priv->crp_pub.cr_opc, rc);
			break;
		}
	}

out:
	return rc;
}

int
crt_context_destroy(crt_context_t crt_ctx, int force)
{
	struct crt_context	*ctx;
	int			rc = 0;

	if (crt_ctx == CRT_CONTEXT_NULL) {
		C_ERROR("invalid parameter (NULL crt_ctx).\n");
		C_GOTO(out, rc = -CER_INVAL);
	}

	ctx = (struct crt_context *)crt_ctx;

	pthread_mutex_lock(&ctx->cc_mutex);

	rc = chash_table_traverse(&ctx->cc_epi_table, crt_ctx_epi_abort,
				  &force);
	if (rc != 0) {
		C_DEBUG("destroy context (idx %d, force %d), "
			"chash_table_traverse failed rc: %d.\n",
			ctx->cc_idx, force, rc);
		pthread_mutex_unlock(&ctx->cc_mutex);
		C_GOTO(out, rc);
	}

	rc = chash_table_destroy_inplace(&ctx->cc_epi_table,
					 true /* force */);
	if (rc != 0) {
		C_ERROR("destroy context (idx %d, force %d), "
			"chash_table_destroy_inplace failed, rc: %d.\n",
			ctx->cc_idx, force, rc);
		pthread_mutex_unlock(&ctx->cc_mutex);
		C_GOTO(out, rc);
	}

	crt_binheap_destroy_inplace(&ctx->cc_bh_timeout);

	pthread_mutex_unlock(&ctx->cc_mutex);
	pthread_mutex_destroy(&ctx->cc_mutex);

	rc = crt_hg_ctx_fini(&ctx->cc_hg_ctx);
	if (rc == 0) {
		pthread_rwlock_wrlock(&crt_gdata.cg_rwlock);
		crt_gdata.cg_ctx_num--;
		crt_list_del_init(&ctx->cc_link);
		pthread_rwlock_unlock(&crt_gdata.cg_rwlock);
		C_FREE_PTR(ctx);
	} else {
		C_ERROR("crt_hg_ctx_fini failed rc: %d.\n", rc);
	}

out:
	return rc;
}

int
crt_ep_abort(crt_endpoint_t ep)
{
	struct crt_context	*ctx = NULL;
	crt_list_t		*rlink;
	int			force;
	int			rc = 0;

	pthread_rwlock_rdlock(&crt_gdata.cg_rwlock);

	crt_list_for_each_entry(ctx, &crt_gdata.cg_ctx_list, cc_link) {
		rc = 0;
		pthread_mutex_lock(&ctx->cc_mutex);
		rlink = chash_rec_find(&ctx->cc_epi_table, (void *)&ep.ep_rank,
				       sizeof(ep.ep_rank));
		if (rlink != NULL) {
			force = true;
			rc = crt_ctx_epi_abort(rlink, &force);
			chash_rec_decref(&ctx->cc_epi_table, rlink);
		}
		pthread_mutex_unlock(&ctx->cc_mutex);
		if (rc != 0) {
			C_ERROR("context (idx %d), ep_abort (rank %d), "
				"failed rc: %d.\n",
				ctx->cc_idx, ep.ep_rank, rc);
			break;
		}
	}

	pthread_rwlock_unlock(&crt_gdata.cg_rwlock);

	return rc;
}

/* caller should already hold crt_ctx->cc_mutex */
static int
crt_req_timeout_track(crt_rpc_t *req)
{
	struct crt_context	*crt_ctx;
	struct crt_rpc_priv	*rpc_priv;
	int			 rc;

	crt_ctx = (struct crt_context *)req->cr_ctx;
	C_ASSERT(crt_ctx != NULL);
	rpc_priv = container_of(req, struct crt_rpc_priv, crp_pub);

	/* add to binheap for timeout tracking */
	crt_req_addref(req); /* decref in crt_req_timeout_untrack */
	rc = crt_binheap_insert(&crt_ctx->cc_bh_timeout,
				&rpc_priv->crp_timeout_bp_node);
	if (rc == 0) {
		rpc_priv->crp_in_binheap = 1;
	} else {
		C_ERROR("rpc_priv %p (opc 0x%x), crt_binheap_insert "
			"failed, rc: %d.\n", rpc_priv,
			rpc_priv->crp_pub.cr_opc, rc);
		crt_req_decref(req);
	}

	return rc;
}

/* caller should already hold crt_ctx->cc_mutex */
static void
crt_req_timeout_untrack(crt_rpc_t *req)
{
	struct crt_context	*crt_ctx;
	struct crt_rpc_priv	*rpc_priv;

	crt_ctx = (struct crt_context *)req->cr_ctx;
	C_ASSERT(crt_ctx != NULL);
	rpc_priv = container_of(req, struct crt_rpc_priv, crp_pub);

	/* remove from timeout binheap */
	if (rpc_priv->crp_in_binheap == 1) {
		rpc_priv->crp_in_binheap = 0;
		crt_binheap_remove(&crt_ctx->cc_bh_timeout,
				   &rpc_priv->crp_timeout_bp_node);
		crt_req_decref(req); /* addref in crt_req_timeout_track */
	}
}

static void
crt_exec_timeout_cb(struct crt_rpc_priv *rpc_priv)
{
	struct crt_timeout_cb_priv	*timeout_cb_priv;
	crt_list_t			*curr_node;
	crt_list_t			*tmp_node;

	if (crt_plugin_gdata.cpg_inited == 0)
		return;
	if (rpc_priv == NULL) {
		C_ERROR("Invalid parameter, rpc_priv == NULL\n");
		return;
	}
	pthread_rwlock_rdlock(&crt_plugin_gdata.cpg_timeout_rwlock);
	crt_list_for_each_safe(curr_node, tmp_node,
			       &crt_plugin_gdata.cpg_timeout_cbs) {
		timeout_cb_priv =
			container_of(curr_node, struct crt_timeout_cb_priv,
				     ctcp_link);
		pthread_rwlock_unlock(&crt_plugin_gdata.cpg_timeout_rwlock);
		timeout_cb_priv->ctcp_func(rpc_priv->crp_pub.cr_ctx,
					   &rpc_priv->crp_pub,
					   timeout_cb_priv->ctcp_args);
		pthread_rwlock_rdlock(&crt_plugin_gdata.cpg_timeout_rwlock);
	}
	pthread_rwlock_unlock(&crt_plugin_gdata.cpg_timeout_rwlock);
}

static inline void
crt_req_timeout_hdlr(struct crt_rpc_priv *rpc_priv)
{
	struct crt_grp_priv		*grp_priv;
	crt_endpoint_t			*tgt_ep;
	crt_rpc_t			*ul_req;
	struct crt_uri_lookup_in	*ul_in;

	tgt_ep = &rpc_priv->crp_pub.cr_ep;
	if (tgt_ep->ep_grp == NULL)
		grp_priv = crt_gdata.cg_grp->gg_srv_pri_grp;
	else
		grp_priv = container_of(tgt_ep->ep_grp, struct crt_grp_priv,
					gp_pub);

	switch (rpc_priv->crp_state) {
	case RPC_STATE_URI_LOOKUP:
		ul_req = rpc_priv->crp_ul_req;
		C_ASSERT(ul_req != NULL);
		ul_in = crt_req_get(ul_req);
		C_ERROR("rpc opc: 0x%x timedout due to URI_LOOKUP to group %s, "
			"rank %d through PSR %d timedout.\n",
			rpc_priv->crp_pub.cr_opc, ul_in->ul_grp_id,
			ul_in->ul_rank, ul_req->cr_ep.ep_rank);
		crt_req_abort(ul_req);
		crt_rpc_complete(rpc_priv, -CER_PROTO);
		break;
	case RPC_STATE_ADDR_LOOKUP:
		C_ERROR("rpc opc: 0x%x timedout due to ADDR_LOOKUP to group %s,"
			" rank %d, tgt_uri %s timedout.\n",
			rpc_priv->crp_pub.cr_opc, grp_priv->gp_pub.cg_grpid,
			tgt_ep->ep_rank, rpc_priv->crp_tgt_uri);
		crt_rpc_complete(rpc_priv, -CER_UNREACH);
		break;
	default:
		/* At this point, RPC should always be completed by Mercury */
		crt_req_abort(&rpc_priv->crp_pub);
		break;
	}
}

static void
crt_context_timeout_check(struct crt_context *crt_ctx)
{
	struct crt_rpc_priv		*rpc_priv, *next;
	struct crt_binheap_node		*bh_node;
	crt_list_t			 timeout_list;
	uint64_t			 ts_now;

	C_ASSERT(crt_ctx != NULL);

	CRT_INIT_LIST_HEAD(&timeout_list);
	ts_now = crt_timeus_secdiff(0);

	pthread_mutex_lock(&crt_ctx->cc_mutex);
	while (1) {
		bh_node = crt_binheap_root(&crt_ctx->cc_bh_timeout);
		if (bh_node == NULL)
			break;
		rpc_priv = container_of(bh_node, struct crt_rpc_priv,
					crp_timeout_bp_node);
		if (rpc_priv->crp_timeout_ts > ts_now)
			break;

		/* +1 to prevent it from being released in timeout_untrack */
		crt_req_addref(&rpc_priv->crp_pub);
		crt_req_timeout_untrack(&rpc_priv->crp_pub);

		crt_list_add_tail(&rpc_priv->crp_tmp_link, &timeout_list);
		C_ERROR("rpc_priv %p (opc 0x%x) timed out.\n", rpc_priv,
			rpc_priv->crp_pub.cr_opc);
	};
	pthread_mutex_unlock(&crt_ctx->cc_mutex);

	/* handle the timeout RPCs */
	crt_list_for_each_entry_safe(rpc_priv, next, &timeout_list,
				     crp_tmp_link) {
		/* check for and execute RPC timeout callbacks here */
		crt_exec_timeout_cb(rpc_priv);
		crt_list_del_init(&rpc_priv->crp_tmp_link);
		crt_req_timeout_hdlr(rpc_priv);
		crt_req_decref(&rpc_priv->crp_pub);
	}
}

static inline uint64_t
crt_get_timeout(struct crt_rpc_priv *rpc_priv)
{
	uint32_t	timeout_sec;

	timeout_sec = rpc_priv->crp_timeout_sec > 0 ?
		      rpc_priv->crp_timeout_sec : crt_gdata.cg_timeout;

	return crt_timeus_secdiff(timeout_sec);
}

/*
 * Track the rpc request per context
 * return CRT_REQ_TRACK_IN_INFLIGHQ - tacked in crt_ep_inflight::epi_req_q
 *        CRT_REQ_TRACK_IN_WAITQ    - queued in crt_ep_inflight::epi_req_waitq
 *        negative value            - other error case such as -CER_NOMEM
 */
int
crt_context_req_track(crt_rpc_t *req)
{
	struct crt_rpc_priv	*rpc_priv;
	struct crt_context	*crt_ctx;
	struct crt_ep_inflight	*epi;
	crt_list_t		*rlink;
	crt_rank_t		ep_rank;
	int			rc = 0;

	C_ASSERT(req != NULL);
	crt_ctx = (struct crt_context *)req->cr_ctx;
	C_ASSERT(crt_ctx != NULL);

	if (req->cr_opc == CRT_OPC_URI_LOOKUP) {
		C_DEBUG("bypass tracking for URI_LOOKUP.\n");
		C_GOTO(out, rc = CRT_REQ_TRACK_IN_INFLIGHQ);
	}
	/* TODO use global rank */
	ep_rank = req->cr_ep.ep_rank;

	/* lookup the crt_ep_inflight (create one if not found) */
	pthread_mutex_lock(&crt_ctx->cc_mutex);
	rlink = chash_rec_find(&crt_ctx->cc_epi_table, (void *)&ep_rank,
			       sizeof(ep_rank));
	if (rlink == NULL) {
		C_ALLOC_PTR(epi);
		if (epi == NULL) {
			pthread_mutex_unlock(&crt_ctx->cc_mutex);
			C_GOTO(out, rc = -CER_NOMEM);
		}

		/* init the epi fields */
		CRT_INIT_LIST_HEAD(&epi->epi_link);
		epi->epi_ep.ep_rank = ep_rank;
		epi->epi_ctx = crt_ctx;
		CRT_INIT_LIST_HEAD(&epi->epi_req_q);
		epi->epi_req_num = 0;
		epi->epi_reply_num = 0;
		CRT_INIT_LIST_HEAD(&epi->epi_req_waitq);
		epi->epi_req_wait_num = 0;
		/* epi_ref init as 1 to avoid other thread delete it but here
		 * still need to access it, decref before exit this routine. */
		epi->epi_ref = 1;
		epi->epi_initialized = 1;
		pthread_mutex_init(&epi->epi_mutex, NULL);

		rc = chash_rec_insert(&crt_ctx->cc_epi_table, &ep_rank,
				      sizeof(ep_rank), &epi->epi_link,
				      true /* exclusive */);
		if (rc != 0)
			C_ERROR("chash_rec_insert failed, rc: %d.\n", rc);
	} else {
		epi = epi_link2ptr(rlink);
		C_ASSERT(epi->epi_ctx == crt_ctx);
	}
	pthread_mutex_unlock(&crt_ctx->cc_mutex);

	if (rc != 0)
		C_GOTO(out, rc);

	/* add the RPC req to crt_ep_inflight */
	rpc_priv = container_of(req, struct crt_rpc_priv, crp_pub);
	pthread_mutex_lock(&epi->epi_mutex);
	C_ASSERT(epi->epi_req_num >= epi->epi_reply_num);
	rpc_priv->crp_timeout_ts = crt_get_timeout(rpc_priv);
	rpc_priv->crp_epi = epi;
	crt_req_addref(req);
	if ((epi->epi_req_num - epi->epi_reply_num) >=
	    CRT_MAX_INFLIGHT_PER_EP_CTX) {
		crt_list_add_tail(&rpc_priv->crp_epi_link,
				   &epi->epi_req_waitq);
		epi->epi_req_wait_num++;
		rpc_priv->crp_state = RPC_STATE_QUEUED;
		rc = CRT_REQ_TRACK_IN_WAITQ;
	} else {
		pthread_mutex_lock(&crt_ctx->cc_mutex);
		rc = crt_req_timeout_track(req);
		pthread_mutex_unlock(&crt_ctx->cc_mutex);
		if (rc == 0) {
			crt_list_add_tail(&rpc_priv->crp_epi_link,
					  &epi->epi_req_q);
			epi->epi_req_num++;
			rc = CRT_REQ_TRACK_IN_INFLIGHQ;
		} else {
			C_ERROR("crt_req_timeout_track failed, "
				"rc: %d.\n", rc);
			/* roll back the addref above */
			crt_req_decref(req);
		}
	}

	pthread_mutex_unlock(&epi->epi_mutex);

	/* reference taken by chash_rec_find or "epi->epi_ref = 1" above */
	pthread_mutex_lock(&crt_ctx->cc_mutex);
	chash_rec_decref(&crt_ctx->cc_epi_table, &epi->epi_link);
	pthread_mutex_unlock(&crt_ctx->cc_mutex);

out:
	return rc;
}

void
crt_context_req_untrack(crt_rpc_t *req)
{
	struct crt_rpc_priv	*rpc_priv, *next;
	struct crt_ep_inflight	*epi;
	struct crt_context	*crt_ctx;
	int64_t			 credits, inflight;
	crt_list_t		 submit_list;
	int			 rc;

	C_ASSERT(req != NULL);
	crt_ctx = (struct crt_context *)req->cr_ctx;
	C_ASSERT(crt_ctx != NULL);
	rpc_priv = container_of(req, struct crt_rpc_priv, crp_pub);

	if (req->cr_opc == CRT_OPC_URI_LOOKUP) {
		C_DEBUG("bypass untracking for URI_LOOKUP.\n");
		return;
	}

	C_ASSERT(rpc_priv->crp_state == RPC_STATE_INITED    ||
		 rpc_priv->crp_state == RPC_STATE_COMPLETED ||
		 rpc_priv->crp_state == RPC_STATE_TIMEOUT ||
		 rpc_priv->crp_state == RPC_STATE_CANCELED);
	epi = rpc_priv->crp_epi;
	C_ASSERT(epi != NULL);

	CRT_INIT_LIST_HEAD(&submit_list);

	pthread_mutex_lock(&epi->epi_mutex);
	/* remove from inflight queue */
	crt_list_del_init(&rpc_priv->crp_epi_link);
	if (rpc_priv->crp_state == RPC_STATE_COMPLETED)
		epi->epi_reply_num++;
	else /* RPC_CANCELED or RPC_INITED or RPC_TIMEOUT */
		epi->epi_req_num--;
	C_ASSERT(epi->epi_req_num >= epi->epi_reply_num);

	if (!crt_req_timedout(req)) {
		pthread_mutex_lock(&crt_ctx->cc_mutex);
		crt_req_timeout_untrack(req);
		pthread_mutex_unlock(&crt_ctx->cc_mutex);
	}

	/* decref corresponding to addref in crt_context_req_track */
	crt_req_decref(req);

	/* process waitq */
	inflight = epi->epi_req_num - epi->epi_reply_num;
	C_ASSERT(inflight >= 0 && inflight <= CRT_MAX_INFLIGHT_PER_EP_CTX);
	credits = CRT_MAX_INFLIGHT_PER_EP_CTX - inflight;
	while (credits > 0 && !crt_list_empty(&epi->epi_req_waitq)) {
		C_ASSERT(epi->epi_req_wait_num > 0);
		rpc_priv = crt_list_entry(epi->epi_req_waitq.next,
					   struct crt_rpc_priv, crp_epi_link);
		rpc_priv->crp_state = RPC_STATE_INITED;
		rpc_priv->crp_timeout_ts = crt_get_timeout(rpc_priv);

		pthread_mutex_lock(&crt_ctx->cc_mutex);
		rc = crt_req_timeout_track(&rpc_priv->crp_pub);
		pthread_mutex_unlock(&crt_ctx->cc_mutex);
		if (rc != 0)
			C_ERROR("crt_req_timeout_track failed, rc: %d.\n", rc);

		/* remove from waitq and add to in-flight queue */
		crt_list_move_tail(&rpc_priv->crp_epi_link, &epi->epi_req_q);
		epi->epi_req_wait_num--;
		C_ASSERT(epi->epi_req_wait_num >= 0);
		epi->epi_req_num++;
		C_ASSERT(epi->epi_req_num >= epi->epi_reply_num);

		/* add to resend list */
		crt_list_add_tail(&rpc_priv->crp_tmp_link, &submit_list);
		credits--;
	}
	pthread_mutex_unlock(&epi->epi_mutex);

	/* re-submit the rpc req */
	crt_list_for_each_entry_safe(rpc_priv, next, &submit_list,
				      crp_tmp_link) {
		crt_list_del_init(&rpc_priv->crp_tmp_link);

		rc = crt_req_send_internal(rpc_priv);
		if (rc == 0)
			continue;

		crt_req_addref(&rpc_priv->crp_pub);
		C_ERROR("crt_req_send_internal failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->crp_pub.cr_opc);
		rpc_priv->crp_state = RPC_STATE_INITED;
		crt_context_req_untrack(&rpc_priv->crp_pub);
		/* for error case here */
		crt_rpc_complete(rpc_priv, rc);
		crt_req_decref(&rpc_priv->crp_pub);
	}
}

crt_context_t
crt_context_lookup(int ctx_idx)
{
	struct crt_context	*ctx;
	bool			found = false;

	pthread_rwlock_rdlock(&crt_gdata.cg_rwlock);
	crt_list_for_each_entry(ctx, &crt_gdata.cg_ctx_list, cc_link) {
		if (ctx->cc_idx == ctx_idx) {
			found = true;
			break;
		}
	}
	pthread_rwlock_unlock(&crt_gdata.cg_rwlock);

	return (found == true) ? ctx : NULL;
}

int
crt_context_idx(crt_context_t crt_ctx, int *ctx_idx)
{
	struct crt_context	*ctx;
	int			rc = 0;

	if (crt_ctx == CRT_CONTEXT_NULL || ctx_idx == NULL) {
		C_ERROR("invalid parameter, crt_ctx: %p, ctx_idx: %p.\n",
			crt_ctx, ctx_idx);
		C_GOTO(out, rc = -CER_INVAL);
	}

	ctx = (struct crt_context *)crt_ctx;
	*ctx_idx = ctx->cc_idx;

out:
	return rc;
}

int
crt_context_num(int *ctx_num)
{
	if (ctx_num == NULL) {
		C_ERROR("invalid parameter of NULL ctx_num.\n");
		return -CER_INVAL;
	}

	*ctx_num = crt_gdata.cg_ctx_num;
	return 0;
}

bool
crt_context_empty(int locked)
{
	bool rc = false;

	if (locked == 0)
		pthread_rwlock_rdlock(&crt_gdata.cg_rwlock);

	rc = crt_list_empty(&crt_gdata.cg_ctx_list);

	if (locked == 0)
		pthread_rwlock_unlock(&crt_gdata.cg_rwlock);

	return rc;
}

static void
crt_exec_progress_cb(crt_context_t ctx)
{
	struct crt_prog_cb_priv		*crt_prog_cb_priv;
	crt_list_t			*curr_node;
	crt_list_t			*tmp_node;

	if (crt_plugin_gdata.cpg_inited == 0)
		return;

	if (ctx == NULL) {
		C_ERROR("Invalid parameter.\n");
		return;
	}
	pthread_rwlock_rdlock(&crt_plugin_gdata.cpg_prog_rwlock);
	crt_list_for_each_safe(curr_node, tmp_node,
			       &crt_plugin_gdata.cpg_prog_cbs) {
		crt_prog_cb_priv =
			container_of(curr_node, struct crt_prog_cb_priv,
				     cpcp_link);
		pthread_rwlock_unlock(&crt_plugin_gdata.cpg_prog_rwlock);
		crt_prog_cb_priv->cpcp_func(ctx, crt_prog_cb_priv->cpcp_args);
		pthread_rwlock_rdlock(&crt_plugin_gdata.cpg_prog_rwlock);
	}
	pthread_rwlock_unlock(&crt_plugin_gdata.cpg_prog_rwlock);
}

int
crt_progress(crt_context_t crt_ctx, int64_t timeout,
	     crt_progress_cond_cb_t cond_cb, void *arg)
{
	struct crt_context	*ctx;
	int64_t			 hg_timeout;
	uint64_t		 now;
	uint64_t		 end = 0;
	int			 crt_ctx_idx;
	int			 rc = 0;

	/** validate input parameters */
	if (crt_ctx == CRT_CONTEXT_NULL) {
		C_ERROR("invalid parameter (NULL crt_ctx).\n");
		C_GOTO(out, rc = -CER_INVAL);
	}

	/**
	 * Invoke the callback once first, in case the condition is met before
	 * calling progress
	 */
	if (cond_cb) {
		/** execute callback */
		rc = cond_cb(arg);
		if (rc > 0)
			/** exit as per the callback request */
			C_GOTO(out, rc = 0);
		if (rc < 0)
			/**
			 * something wrong happened during the callback
			 * execution
			 */
			C_GOTO(out, rc);
	}

	rc = crt_context_idx(crt_ctx, &crt_ctx_idx);
	if (rc != 0) {
		C_ERROR("crt_context_idx() failed, rc: %d.\n", rc);
		C_GOTO(out, rc);
	}
	ctx = (struct crt_context *)crt_ctx;
	if (timeout == 0 || cond_cb == NULL) { /** fast path */
		crt_context_timeout_check(ctx);
		/* check for and execute progress callbacks here */
		if (crt_ctx_idx == 0) {
			crt_exec_progress_cb(crt_ctx);
			crt_drain_eviction_requests_kickoff(ctx);
		}

		rc = crt_hg_progress(&ctx->cc_hg_ctx, timeout);
		if (rc && rc != -CER_TIMEDOUT) {
			C_ERROR("crt_hg_progress failed, rc: %d.\n", rc);
			C_GOTO(out, rc);
		}

		if (cond_cb) {
			int ret;

			/**
			 * Don't clobber rc which might be set to
			 * -CER_TIMEDOUT
			 */
			ret = cond_cb(arg);
			/** be careful with return code */
			if (ret > 0)
				C_GOTO(out, rc = 0);
			if (ret < 0)
				C_GOTO(out, rc = ret);
		}

		C_GOTO(out, rc);
	}

	/** Progress with callback and non-null timeout */
	if (timeout <= 0) {
		C_ASSERT(timeout < 0);
		/**
		 * For infinite timeout, use a mercury timeout of 1s to avoid
		 * being blocked indefinitely if another thread has called
		 * crt_hg_progress() behind our back
		 */
		hg_timeout = 0;
	} else  {
		now = crt_timeus_secdiff(0);
		end = now + timeout;
		/** similiarly, probe more frequently if timeout is large */
		if (timeout > 1000 * 1000)
			hg_timeout = 1000 * 1000;
		else
			hg_timeout = timeout;
	}

	while (true) {
		crt_context_timeout_check(ctx);
		/* check for and execute progress callbacks here */
		if (crt_ctx_idx == 0) {
			crt_exec_progress_cb(ctx);
			crt_drain_eviction_requests_kickoff(ctx);
		}


		rc = crt_hg_progress(&ctx->cc_hg_ctx, hg_timeout);
		if (rc && rc != -CER_TIMEDOUT) {
			C_ERROR("crt_hg_progress failed with %d\n", rc);
			C_GOTO(out, rc = 0);
		}

		/** execute callback */
		rc = cond_cb(arg);
		if (rc > 0)
			C_GOTO(out, rc = 0);
		if (rc < 0)
			C_GOTO(out, rc);

		/** check for timeout, if not infinite */
		if (timeout > 0) {
			now = crt_timeus_secdiff(0);
			if (now >= end) {
				rc = -CER_TIMEDOUT;
				break;
			}
			if (end - now > 1000 * 1000)
				hg_timeout = 1000 * 1000;
			else
				hg_timeout = end - now;
		}
	}
out:
	return rc;
}

/**
 * to use this function, the user has to:
 * 1) define a callback function user_cb
 * 2) call crt_register_progress_cb(user_cb);
 */
int
crt_register_progress_cb(crt_progress_cb cb, void *args)
{
	/* save the function pointer and args to a global list */
	struct crt_prog_cb_priv		*crt_prog_cb_priv = NULL;
	int				 rc = 0;

	C_ALLOC_PTR(crt_prog_cb_priv);
	if (crt_prog_cb_priv == NULL)
		C_GOTO(out, rc = -CER_NOMEM);
	crt_prog_cb_priv->cpcp_func = cb;
	crt_prog_cb_priv->cpcp_args = args;
	pthread_rwlock_wrlock(&crt_plugin_gdata.cpg_prog_rwlock);
	crt_list_add_tail(&crt_prog_cb_priv->cpcp_link,
			  &crt_plugin_gdata.cpg_prog_cbs);
	pthread_rwlock_unlock(&crt_plugin_gdata.cpg_prog_rwlock);

out:
	return rc;
}

/**
 * to use this function, the user has to:
 * 1) define a callback function user_cb
 * 2) call crt_register_timeout_cb_core(user_cb);
 */
int
crt_register_timeout_cb(crt_timeout_cb cb, void *args)
{
	/* TODO: save the function pointer somewhere for retreival later on */
	struct crt_timeout_cb_priv	*timeout_cb_priv = NULL;
	int				 rc = 0;

	C_ALLOC_PTR(timeout_cb_priv);
	if (timeout_cb_priv == NULL)
		C_GOTO(out, rc = -CER_NOMEM);
	timeout_cb_priv->ctcp_func = cb;
	timeout_cb_priv->ctcp_args = args;
	pthread_rwlock_wrlock(&crt_plugin_gdata.cpg_timeout_rwlock);
	crt_list_add_tail(&timeout_cb_priv->ctcp_link,
			  &crt_plugin_gdata.cpg_timeout_cbs);
	pthread_rwlock_unlock(&crt_plugin_gdata.cpg_timeout_rwlock);

out:
	return rc;
}
