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
epi_op_key_get(struct dhash_table *hhtab, crt_list_t *rlink, void **key_pp)
{
	struct crt_ep_inflight *epi = epi_link2ptr(rlink);

	/* TODO: use global rank */
	*key_pp = (void *)&epi->epi_ep.ep_rank;
	return sizeof(epi->epi_ep.ep_rank);
}

static uint32_t
epi_op_key_hash(struct dhash_table *hhtab, const void *key, unsigned int ksize)
{
	C_ASSERT(ksize == sizeof(crt_rank_t));

	return (unsigned int)(*(const uint32_t *)key %
		(1U << CRT_EPI_TABLE_BITS));
}

static bool
epi_op_key_cmp(struct dhash_table *hhtab, crt_list_t *rlink,
	  const void *key, unsigned int ksize)
{
	struct crt_ep_inflight *epi = epi_link2ptr(rlink);

	C_ASSERT(ksize == sizeof(crt_rank_t));
	/* TODO: use global rank */

	return epi->epi_ep.ep_rank == *(crt_rank_t *)key;
}

static void
epi_op_rec_addref(struct dhash_table *hhtab, crt_list_t *rlink)
{
	epi_link2ptr(rlink)->epi_ref++;
}

static bool
epi_op_rec_decref(struct dhash_table *hhtab, crt_list_t *rlink)
{
	struct crt_ep_inflight *epi = epi_link2ptr(rlink);

	epi->epi_ref--;
	return epi->epi_ref == 0;
}

static void
epi_op_rec_free(struct dhash_table *hhtab, crt_list_t *rlink)
{
	crt_epi_destroy(epi_link2ptr(rlink));
}

static dhash_table_ops_t epi_table_ops = {
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
	C_ASSERT(epi->epi_req_num == epi->epi_reply_num);

	/* crt_list_del_init(&epi->epi_link); */
	pthread_mutex_destroy(&epi->epi_mutex);

	C_FREE_PTR(epi);
}

static int
crt_context_init(crt_context_t crt_ctx)
{
	struct crt_context	*ctx;
	int			rc;

	C_ASSERT(crt_ctx != NULL);
	ctx = crt_ctx;

	CRT_INIT_LIST_HEAD(&ctx->dc_link);

	/* create epi table, use external lock */
	rc =  dhash_table_create_inplace(DHASH_FT_NOLOCK, CRT_EPI_TABLE_BITS,
			NULL, &epi_table_ops, &ctx->dc_epi_table);
	if (rc != 0) {
		C_ERROR("dhash_table_create_inplace failed, rc: %d.\n", rc);
		return rc;
	}

	pthread_mutex_init(&ctx->dc_mutex, NULL);
	return 0;
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

	pthread_rwlock_wrlock(&crt_gdata.dg_rwlock);

	rc = crt_hg_ctx_init(&ctx->dc_hg_ctx, crt_gdata.dg_ctx_num);
	if (rc != 0) {
		C_ERROR("crt_hg_ctx_init failed rc: %d.\n", rc);
		C_FREE_PTR(ctx);
		pthread_rwlock_unlock(&crt_gdata.dg_rwlock);
		C_GOTO(out, rc);
	}

	ctx->dc_idx = crt_gdata.dg_ctx_num;
	crt_list_add_tail(&ctx->dc_link, &crt_gdata.dg_ctx_list);
	crt_gdata.dg_ctx_num++;

	ctx->dc_pool = arg;
	pthread_rwlock_unlock(&crt_gdata.dg_rwlock);

	*crt_ctx = (crt_context_t)ctx;

out:
	return rc;
}

void
crt_rpc_complete(struct crt_rpc_priv *rpc_priv, int rc)
{
	C_ASSERT(rpc_priv != NULL);

	if (rpc_priv->drp_complete_cb != NULL) {
		struct crt_cb_info	cbinfo;

		cbinfo.dci_rpc = &rpc_priv->drp_pub;
		cbinfo.dci_arg = rpc_priv->drp_arg;
		cbinfo.dci_rc = rc;
		if (rc == -CER_CANCELED)
			rpc_priv->drp_state = RPC_CANCELED;
		else
			rpc_priv->drp_state = RPC_COMPLETED;
		rc = rpc_priv->drp_complete_cb(&cbinfo);
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
	if (force != 0) {
		C_ERROR("cannot abort endpoint (idx %d, rank %d, req_wait_num "
			CF_U64", req_num "CF_U64", reply_num "CF_U64", "
			"inflight "CF_U64"\n", ctx->dc_idx, epi->epi_ep.ep_rank,
			epi->epi_req_wait_num, epi->epi_req_num,
			epi->epi_reply_num,
			epi->epi_req_num - epi->epi_reply_num);
		C_GOTO(out, rc = -CER_BUSY);
	}

	/* abort RPCs in waitq */
	msg_logged = false;
	crt_list_for_each_entry_safe(rpc_priv, rpc_next, &epi->epi_req_waitq,
				      drp_epi_link) {
		C_ASSERT(epi->epi_req_wait_num > 0);
		if (msg_logged == false) {
			C_DEBUG(CF_TP, "destroy context (idx %d, rank %d, "
				"req_wait_num "CF_U64").\n", ctx->dc_idx,
				epi->epi_ep.ep_rank, epi->epi_req_wait_num);
			msg_logged = true;
		}
		/* Just remove from wait_q, decrease the wait_num and destroy
		 * the request. Trigger the possible completion callback. */
		C_ASSERT(rpc_priv->drp_state == RPC_QUEUED);
		crt_list_del_init(&rpc_priv->drp_epi_link);
		epi->epi_req_wait_num--;
		crt_rpc_complete(rpc_priv, -CER_CANCELED);
		/* corresponds to ref taken when adding to waitq */
		crt_req_decref(&rpc_priv->drp_pub);
	}

	/* abort RPCs in inflight queue */
	msg_logged = false;
	crt_list_for_each_entry_safe(rpc_priv, rpc_next, &epi->epi_req_q,
				      drp_epi_link) {
		C_ASSERT(epi->epi_req_num > epi->epi_reply_num);
		if (msg_logged == false) {
			C_DEBUG(CF_TP, "destroy context (idx %d, rank %d, "
				"epi_req_num "CF_U64", epi_reply_num "
				""CF_U64", inflight "CF_U64").\n",
				ctx->dc_idx, epi->epi_ep.ep_rank,
				epi->epi_req_num, epi->epi_reply_num,
				epi->epi_req_num - epi->epi_reply_num);
			msg_logged = true;
		}

		rc = crt_req_abort(&rpc_priv->drp_pub);
		if (rc != 0) {
			C_ERROR("crt_req_abort(opc: 0x%x) failed, rc: %d.\n",
				rpc_priv->drp_pub.dr_opc, rc);
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

	pthread_mutex_lock(&ctx->dc_mutex);

	rc = dhash_table_traverse(&ctx->dc_epi_table, crt_ctx_epi_abort,
				  &force);
	if (rc != 0) {
		C_DEBUG(CF_TP, "destroy context (idx %d, force %d), "
			"dhash_table_traverse failed rc: %d.\n",
			ctx->dc_idx, force, rc);
		pthread_mutex_unlock(&ctx->dc_mutex);
		C_GOTO(out, rc);
	}

	rc = dhash_table_destroy_inplace(&ctx->dc_epi_table,
					 true /* force */);
	if (rc != 0) {
		C_ERROR("destroy context (idx %d, force %d), "
			"dhash_table_destroy_inplace failed, rc: %d.\n",
			ctx->dc_idx, force, rc);
		pthread_mutex_unlock(&ctx->dc_mutex);
		C_GOTO(out, rc);
	}

	pthread_mutex_unlock(&ctx->dc_mutex);

	rc = crt_hg_ctx_fini(&ctx->dc_hg_ctx);
	if (rc == 0) {
		pthread_rwlock_wrlock(&crt_gdata.dg_rwlock);
		crt_gdata.dg_ctx_num--;
		crt_list_del_init(&ctx->dc_link);
		pthread_rwlock_unlock(&crt_gdata.dg_rwlock);
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

	pthread_rwlock_rdlock(&crt_gdata.dg_rwlock);

	crt_list_for_each_entry(ctx, &crt_gdata.dg_ctx_list, dc_link) {
		rc = 0;
		pthread_mutex_lock(&ctx->dc_mutex);
		rlink = dhash_rec_find(&ctx->dc_epi_table, (void *)&ep.ep_rank,
				       sizeof(ep.ep_rank));
		if (rlink != NULL) {
			force = true;
			rc = crt_ctx_epi_abort(rlink, &force);
			dhash_rec_decref(&ctx->dc_epi_table, rlink);
		}
		pthread_mutex_unlock(&ctx->dc_mutex);
		if (rc != 0) {
			C_ERROR("context (idx %d), ep_abort (rank %d), "
				"failed rc: %d.\n",
				ctx->dc_idx, ep.ep_rank, rc);
			break;
		}
	}

	pthread_rwlock_unlock(&crt_gdata.dg_rwlock);

	return rc;
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
	crt_ctx = (struct crt_context *)req->dr_ctx;
	C_ASSERT(crt_ctx != NULL);

	/* TODO use global rank */
	ep_rank = req->dr_ep.ep_rank;

	/* lookup the crt_ep_inflight (create one if not found) */
	pthread_mutex_lock(&crt_ctx->dc_mutex);
	rlink = dhash_rec_find(&crt_ctx->dc_epi_table, (void *)&ep_rank,
			       sizeof(ep_rank));
	if (rlink == NULL) {
		C_ALLOC_PTR(epi);
		if (epi == NULL) {
			pthread_mutex_unlock(&crt_ctx->dc_mutex);
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

		rc = dhash_rec_insert(&crt_ctx->dc_epi_table, &ep_rank,
				      sizeof(ep_rank), &epi->epi_link,
				      true /* exclusive */);
		if (rc != 0)
			C_ERROR("dhash_rec_insert failed, rc: %d.\n", rc);
	} else {
		epi = epi_link2ptr(rlink);
		C_ASSERT(epi->epi_ctx == crt_ctx);
	}
	pthread_mutex_unlock(&crt_ctx->dc_mutex);

	if (rc != 0)
		C_GOTO(out, rc);

	/* add the RPC req to crt_ep_inflight */
	rpc_priv = container_of(req, struct crt_rpc_priv, drp_pub);
	pthread_mutex_lock(&epi->epi_mutex);
	C_ASSERT(epi->epi_req_num >= epi->epi_reply_num);
	rpc_priv->drp_ts = crt_time_usec(0);
	rpc_priv->drp_epi = epi;
	crt_req_addref(req);
	if ((epi->epi_req_num - epi->epi_reply_num) >=
	    CRT_MAX_INFLIGHT_PER_EP_CTX) {
		crt_list_add_tail(&rpc_priv->drp_epi_link,
				   &epi->epi_req_waitq);
		epi->epi_req_wait_num++;
		rpc_priv->drp_state = RPC_QUEUED;
		rc = CRT_REQ_TRACK_IN_WAITQ;
	} else {
		crt_list_add_tail(&rpc_priv->drp_epi_link, &epi->epi_req_q);
		epi->epi_req_num++;
		rc = CRT_REQ_TRACK_IN_INFLIGHQ;
	}
	pthread_mutex_unlock(&epi->epi_mutex);

	dhash_rec_decref(&crt_ctx->dc_epi_table, &epi->epi_link);

out:
	return rc;
}

void
crt_context_req_untrack(crt_rpc_t *req)
{
	struct crt_rpc_priv	*rpc_priv, *next;
	struct crt_ep_inflight	*epi;
	int64_t			credits, inflight;
	crt_list_t		resend_list;
	int			rc;

	C_ASSERT(req != NULL);
	rpc_priv = container_of(req, struct crt_rpc_priv, drp_pub);

	C_ASSERT(rpc_priv->drp_state == RPC_INITED    ||
		 rpc_priv->drp_state == RPC_COMPLETED ||
		 rpc_priv->drp_state == RPC_CANCELED);
	epi = rpc_priv->drp_epi;
	C_ASSERT(epi != NULL);

	CRT_INIT_LIST_HEAD(&resend_list);

	pthread_mutex_lock(&epi->epi_mutex);
	/* remove from inflight queue */
	crt_list_del_init(&rpc_priv->drp_epi_link);
	if (rpc_priv->drp_state == RPC_COMPLETED)
		epi->epi_reply_num++;
	else /* RPC_CANCELED or RPC_INITED */
		epi->epi_req_num--;
	C_ASSERT(epi->epi_req_num >= epi->epi_reply_num);

	/* decref corresponding to addref in crt_context_req_track */
	crt_req_decref(req);

	/* process waitq */
	inflight = epi->epi_req_num - epi->epi_reply_num;
	C_ASSERT(inflight >= 0 && inflight <= CRT_MAX_INFLIGHT_PER_EP_CTX);
	credits = CRT_MAX_INFLIGHT_PER_EP_CTX - inflight;
	while (credits > 0 && !crt_list_empty(&epi->epi_req_waitq)) {
		C_ASSERT(epi->epi_req_wait_num > 0);
		rpc_priv = crt_list_entry(epi->epi_req_waitq.next,
					   struct crt_rpc_priv, drp_epi_link);
		rpc_priv->drp_state = RPC_INITED;
		rpc_priv->drp_ts = crt_time_usec(0);
		/* remove from waitq and add to in-flight queue */
		crt_list_move_tail(&rpc_priv->drp_epi_link, &epi->epi_req_q);
		epi->epi_req_wait_num--;
		C_ASSERT(epi->epi_req_wait_num >= 0);
		epi->epi_req_num++;
		C_ASSERT(epi->epi_req_num >= epi->epi_reply_num);

		/* add to resend list */
		crt_list_add_tail(&rpc_priv->drp_tmp_link, &resend_list);
		credits--;
	}
	pthread_mutex_unlock(&epi->epi_mutex);

	/* re-submit the rpc req */
	crt_list_for_each_entry_safe(rpc_priv, next, &resend_list,
				      drp_tmp_link) {
		crt_list_del_init(&rpc_priv->drp_tmp_link);
		rpc_priv->drp_state = RPC_REQ_SENT;
		rc = crt_hg_req_send(rpc_priv);
		if (rc == 0)
			continue;

		crt_req_addref(&rpc_priv->drp_pub);
		C_ERROR("crt_hg_req_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
		rpc_priv->drp_state = RPC_INITED;
		crt_context_req_untrack(&rpc_priv->drp_pub);
		/* for error case here */
		crt_rpc_complete(rpc_priv, rc);
		crt_req_decref(&rpc_priv->drp_pub);
	}
}

crt_context_t
crt_context_lookup(int ctx_idx)
{
	struct crt_context	*ctx;
	bool			found = false;

	pthread_rwlock_rdlock(&crt_gdata.dg_rwlock);
	crt_list_for_each_entry(ctx, &crt_gdata.dg_ctx_list, dc_link) {
		if (ctx->dc_idx == ctx_idx) {
			found = true;
			break;
		}
	}
	pthread_rwlock_unlock(&crt_gdata.dg_rwlock);

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
	*ctx_idx = ctx->dc_idx;

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

	*ctx_num = crt_gdata.dg_ctx_num;
	return 0;
}

bool
crt_context_empty(int locked)
{
	bool rc = false;

	if (locked == 0)
		pthread_rwlock_rdlock(&crt_gdata.dg_rwlock);

	rc = crt_list_empty(&crt_gdata.dg_ctx_list);

	if (locked == 0)
		pthread_rwlock_unlock(&crt_gdata.dg_rwlock);

	return rc;
}

int
crt_progress(crt_context_t crt_ctx, int64_t timeout,
	     crt_progress_cond_cb_t cond_cb, void *arg)
{
	struct crt_context	*ctx;
	int64_t			 hg_timeout;
	uint64_t		 now;
	uint64_t		 end = 0;
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

	ctx = (struct crt_context *)crt_ctx;
	if (timeout == 0 || cond_cb == NULL) {
		/** fast path */
		rc = crt_hg_progress(&ctx->dc_hg_ctx, timeout);
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
		now = crt_time_usec(0);
		end = now + timeout;
		/** similiarly, probe more frequently if timeout is large */
		if (timeout > 1000 * 1000)
			hg_timeout = 1000 * 1000;
		else
			hg_timeout = timeout;
	}

	while (true) {
		rc = crt_hg_progress(&ctx->dc_hg_ctx, hg_timeout);
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
			now = crt_time_usec(0);
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
