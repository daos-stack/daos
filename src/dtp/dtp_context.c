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
 * This file is part of daos_transport. It implements the dtp context related
 * APIs.
 */
#include <abt.h>
#include <dtp_internal.h>

static void dtp_epi_destroy(struct dtp_ep_inflight *epi);

static struct dtp_ep_inflight *
epi_link2ptr(daos_list_t *rlink)
{
	D_ASSERT(rlink != NULL);
	return container_of(rlink, struct dtp_ep_inflight, epi_link);
}

static int
epi_op_key_get(struct dhash_table *hhtab, daos_list_t *rlink, void **key_pp)
{
	struct dtp_ep_inflight *epi = epi_link2ptr(rlink);

	/* TODO: use global rank */
	*key_pp = (void *)&epi->epi_ep.ep_rank;
	return sizeof(epi->epi_ep.ep_rank);
}

static uint32_t
epi_op_key_hash(struct dhash_table *hhtab, const void *key, unsigned int ksize)
{
	D_ASSERT(ksize == sizeof(daos_rank_t));

	return (unsigned int)(*(const uint32_t *)key %
		(1U << DTP_EPI_TABLE_BITS));
}

static bool
epi_op_key_cmp(struct dhash_table *hhtab, daos_list_t *rlink,
	  const void *key, unsigned int ksize)
{
	struct dtp_ep_inflight *epi = epi_link2ptr(rlink);

	D_ASSERT(ksize == sizeof(daos_rank_t));
	/* TODO: use global rank */

	return epi->epi_ep.ep_rank == *(daos_rank_t *)key;
}

static void
epi_op_rec_addref(struct dhash_table *hhtab, daos_list_t *rlink)
{
	epi_link2ptr(rlink)->epi_ref++;
}

static bool
epi_op_rec_decref(struct dhash_table *hhtab, daos_list_t *rlink)
{
	struct dtp_ep_inflight *epi = epi_link2ptr(rlink);

	epi->epi_ref--;
	return epi->epi_ref == 0;
}

static void
epi_op_rec_free(struct dhash_table *hhtab, daos_list_t *rlink)
{
	dtp_epi_destroy(epi_link2ptr(rlink));
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
dtp_epi_destroy(struct dtp_ep_inflight *epi)
{
	D_ASSERT(epi != NULL);

	D_ASSERT(epi->epi_ref == 0);
	D_ASSERT(epi->epi_initialized == 1);

	D_ASSERT(daos_list_empty(&epi->epi_req_waitq));
	D_ASSERT(epi->epi_req_wait_num == 0);

	D_ASSERT(daos_list_empty(&epi->epi_req_q));
	D_ASSERT(epi->epi_req_num == epi->epi_reply_num);

	/* daos_list_del_init(&epi->epi_link); */
	pthread_mutex_destroy(&epi->epi_mutex);

	D_FREE_PTR(epi);
}

static int
dtp_context_init(dtp_context_t dtp_ctx)
{
	struct dtp_context	*ctx;
	int			rc;

	D_ASSERT(dtp_ctx != NULL);
	ctx = dtp_ctx;

	DAOS_INIT_LIST_HEAD(&ctx->dc_link);

	/* create epi table, use external lock */
	rc =  dhash_table_create_inplace(DHASH_FT_NOLOCK, DTP_EPI_TABLE_BITS,
			NULL, &epi_table_ops, &ctx->dc_epi_table);
	if (rc != 0) {
		D_ERROR("dhash_table_create_inplace failed, rc: %d.\n", rc);
		return rc;
	}

	pthread_mutex_init(&ctx->dc_mutex, NULL);
	return 0;
}

int
dtp_context_create(void *arg, dtp_context_t *dtp_ctx)
{
	struct dtp_context	*ctx = NULL;
	int			rc = 0;

	if (dtp_ctx == NULL) {
		D_ERROR("invalid parameter of NULL dtp_ctx.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = dtp_context_init(ctx);
	if (rc != 0) {
		D_ERROR("dtp_context_init failed, rc: %d.\n", rc);
		D_FREE_PTR(ctx);
		D_GOTO(out, rc);
	}

	pthread_rwlock_wrlock(&dtp_gdata.dg_rwlock);

	rc = dtp_hg_ctx_init(&ctx->dc_hg_ctx, dtp_gdata.dg_ctx_num);
	if (rc != 0) {
		D_ERROR("dtp_hg_ctx_init failed rc: %d.\n", rc);
		D_FREE_PTR(ctx);
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
		D_GOTO(out, rc);
	}

	ctx->dc_idx = dtp_gdata.dg_ctx_num;
	daos_list_add_tail(&ctx->dc_link, &dtp_gdata.dg_ctx_list);
	dtp_gdata.dg_ctx_num++;

	ctx->dc_pool = arg;
	pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);

	*dtp_ctx = (dtp_context_t)ctx;

out:
	return rc;
}

static inline void
dtp_rpc_complete(struct dtp_rpc_priv *rpc_priv, int rc)
{
	D_ASSERT(rpc_priv != NULL);

	if (rpc_priv->drp_complete_cb != NULL) {
		struct dtp_cb_info	cbinfo;

		cbinfo.dci_rpc = &rpc_priv->drp_pub;
		cbinfo.dci_arg = rpc_priv->drp_arg;
		cbinfo.dci_rc = rc;
		if (rc == -DER_CANCELED)
			rpc_priv->drp_state = RPC_CANCELED;
		else
			rpc_priv->drp_state = RPC_COMPLETED;
		rc = rpc_priv->drp_complete_cb(&cbinfo);
		if (rc != 0)
			D_ERROR("req_cbinfo->rsc_cb returned %d.\n", rc);
	}
}

/* abort the RPCs in inflight queue and waitq in the epi. */
static int
dtp_ctx_epi_abort(daos_list_t *rlink, void *args)
{
	struct dtp_ep_inflight	*epi;
	struct dtp_context	*ctx;
	struct dtp_rpc_priv	*rpc_priv, *rpc_next;
	bool			msg_logged;
	int			force;
	int			rc = 0;

	D_ASSERT(rlink != NULL);
	D_ASSERT(args != NULL);
	epi = epi_link2ptr(rlink);
	ctx = epi->epi_ctx;
	D_ASSERT(ctx != NULL);

	/* empty queue, nothing to do */
	if (daos_list_empty(&epi->epi_req_waitq) &&
	    daos_list_empty(&epi->epi_req_q))
		D_GOTO(out, rc = 0);

	force = *(int *)args;
	if (force != 0) {
		D_ERROR("cannot abort endpoint (idx %d, rank %d, req_wait_num "
			DF_U64", req_num "DF_U64", reply_num "DF_U64", "
			"inflight "DF_U64"\n", ctx->dc_idx, epi->epi_ep.ep_rank,
			epi->epi_req_wait_num, epi->epi_req_num,
			epi->epi_reply_num,
			epi->epi_req_num - epi->epi_reply_num);
		D_GOTO(out, rc = -DER_BUSY);
	}

	/* abort RPCs in waitq */
	msg_logged = false;
	daos_list_for_each_entry_safe(rpc_priv, rpc_next, &epi->epi_req_waitq,
				      drp_epi_link) {
		D_ASSERT(epi->epi_req_wait_num > 0);
		if (msg_logged == false) {
			D_DEBUG(DF_TP, "destroy context (idx %d, rank %d, "
				"req_wait_num "DF_U64").\n", ctx->dc_idx,
				epi->epi_ep.ep_rank, epi->epi_req_wait_num);
			msg_logged = true;
		}
		/* Just remove from wait_q, decrease the wait_num and destroy
		 * the request. Trigger the possible completion callback. */
		D_ASSERT(rpc_priv->drp_state == RPC_QUEUED);
		daos_list_del_init(&rpc_priv->drp_epi_link);
		epi->epi_req_wait_num--;
		dtp_rpc_complete(rpc_priv, -DER_CANCELED);
		/* corresponds to ref taken when adding to waitq */
		dtp_req_decref(&rpc_priv->drp_pub);
	}

	/* abort RPCs in inflight queue */
	msg_logged = false;
	daos_list_for_each_entry_safe(rpc_priv, rpc_next, &epi->epi_req_q,
				      drp_epi_link) {
		D_ASSERT(epi->epi_req_num > epi->epi_reply_num);
		if (msg_logged == false) {
			D_DEBUG(DF_TP, "destroy context (idx %d, rank %d, "
				"epi_req_num "DF_U64", epi_reply_num "
				""DF_U64", inflight "DF_U64").\n",
				ctx->dc_idx, epi->epi_ep.ep_rank,
				epi->epi_req_num, epi->epi_reply_num,
				epi->epi_req_num - epi->epi_reply_num);
			msg_logged = true;
		}

		rc = dtp_req_abort(&rpc_priv->drp_pub);
		if (rc != 0) {
			D_ERROR("dtp_req_abort(opc: 0x%x) failed, rc: %d.\n",
				rpc_priv->drp_pub.dr_opc, rc);
			break;
		}
	}

out:
	return rc;
}

int
dtp_context_destroy(dtp_context_t dtp_ctx, int force)
{
	struct dtp_context	*ctx;
	int			rc = 0;

	if (dtp_ctx == DTP_CONTEXT_NULL) {
		D_ERROR("invalid parameter (NULL dtp_ctx).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	ctx = (struct dtp_context *)dtp_ctx;

	pthread_mutex_lock(&ctx->dc_mutex);

	rc = dhash_table_traverse(&ctx->dc_epi_table, dtp_ctx_epi_abort,
				  &force);
	if (rc != 0) {
		D_DEBUG(DF_TP, "destroy context (idx %d, force %d), "
			"dhash_table_traverse failed rc: %d.\n",
			ctx->dc_idx, force, rc);
		pthread_mutex_unlock(&ctx->dc_mutex);
		D_GOTO(out, rc);
	}

	rc = dhash_table_destroy_inplace(&ctx->dc_epi_table,
					 true /* force */);
	if (rc != 0) {
		D_ERROR("destroy context (idx %d, force %d), "
			"dhash_table_destroy_inplace failed, rc: %d.\n",
			ctx->dc_idx, force, rc);
		pthread_mutex_unlock(&ctx->dc_mutex);
		D_GOTO(out, rc);
	}

	pthread_mutex_unlock(&ctx->dc_mutex);

	rc = dtp_hg_ctx_fini(&ctx->dc_hg_ctx);
	if (rc == 0) {
		pthread_rwlock_wrlock(&dtp_gdata.dg_rwlock);
		dtp_gdata.dg_ctx_num--;
		daos_list_del_init(&ctx->dc_link);
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);
		D_FREE_PTR(ctx);
	} else {
		D_ERROR("dtp_hg_ctx_fini failed rc: %d.\n", rc);
	}

out:
	return rc;
}

/*
 * Track the rpc request per context
 * return DTP_REQ_TRACK_IN_INFLIGHQ - tacked in dtp_ep_inflight::epi_req_q
 *        DTP_REQ_TRACK_IN_WAITQ    - queued in dtp_ep_inflight::epi_req_waitq
 *        negative value            - other error case such as -DER_NOMEM
 */
int
dtp_context_req_track(dtp_rpc_t *req)
{
	struct dtp_rpc_priv	*rpc_priv;
	struct dtp_context	*dtp_ctx;
	struct dtp_ep_inflight	*epi;
	daos_list_t		*rlink;
	daos_rank_t		ep_rank;
	int			rc = 0;

	D_ASSERT(req != NULL);
	dtp_ctx = (struct dtp_context *)req->dr_ctx;
	D_ASSERT(dtp_ctx != NULL);

	/* TODO use global rank */
	ep_rank = req->dr_ep.ep_rank;

	/* lookup the dtp_ep_inflight (create one if not found) */
	pthread_mutex_lock(&dtp_ctx->dc_mutex);
	rlink = dhash_rec_find(&dtp_ctx->dc_epi_table, (void *)&ep_rank,
			       sizeof(ep_rank));
	if (rlink == NULL) {
		D_ALLOC_PTR(epi);
		if (epi == NULL) {
			pthread_mutex_unlock(&dtp_ctx->dc_mutex);
			D_GOTO(out, rc = -DER_NOMEM);
		}

		/* init the epi fields */
		DAOS_INIT_LIST_HEAD(&epi->epi_link);
		epi->epi_ep.ep_rank = ep_rank;
		epi->epi_ctx = dtp_ctx;
		DAOS_INIT_LIST_HEAD(&epi->epi_req_q);
		epi->epi_req_num = 0;
		epi->epi_reply_num = 0;
		DAOS_INIT_LIST_HEAD(&epi->epi_req_waitq);
		epi->epi_req_wait_num = 0;
		/* epi_ref init as 1 to avoid other thread delete it but here
		 * still need to access it, decref before exit this routine. */
		epi->epi_ref = 1;
		epi->epi_initialized = 1;
		pthread_mutex_init(&epi->epi_mutex, NULL);

		rc = dhash_rec_insert(&dtp_ctx->dc_epi_table, &ep_rank,
				      sizeof(ep_rank), &epi->epi_link,
				      true /* exclusive */);
		if (rc != 0)
			D_ERROR("dhash_rec_insert failed, rc: %d.\n", rc);
	} else {
		epi = epi_link2ptr(rlink);
		D_ASSERT(epi->epi_ctx == dtp_ctx);
	}
	pthread_mutex_unlock(&dtp_ctx->dc_mutex);

	if (rc != 0)
		D_GOTO(out, rc);

	/* add the RPC req to dtp_ep_inflight */
	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	pthread_mutex_lock(&epi->epi_mutex);
	D_ASSERT(epi->epi_req_num >= epi->epi_reply_num);
	rpc_priv->drp_ts = dtp_time_usec(0);
	rpc_priv->drp_epi = epi;
	dtp_req_addref(req);
	if ((epi->epi_req_num - epi->epi_reply_num) >=
	    DTP_MAX_INFLIGHT_PER_EP_CTX) {
		daos_list_add_tail(&rpc_priv->drp_epi_link,
				   &epi->epi_req_waitq);
		epi->epi_req_wait_num++;
		rpc_priv->drp_state = RPC_QUEUED;
		rc = DTP_REQ_TRACK_IN_WAITQ;
	} else {
		daos_list_add_tail(&rpc_priv->drp_epi_link, &epi->epi_req_q);
		epi->epi_req_num++;
		rc = DTP_REQ_TRACK_IN_INFLIGHQ;
	}
	pthread_mutex_unlock(&epi->epi_mutex);

	dhash_rec_decref(&dtp_ctx->dc_epi_table, &epi->epi_link);

out:
	return rc;
}

void
dtp_context_req_untrack(dtp_rpc_t *req)
{
	struct dtp_rpc_priv	*rpc_priv, *next;
	struct dtp_ep_inflight	*epi;
	int64_t			credits, inflight;
	daos_list_t		resend_list;
	int			rc;

	D_ASSERT(req != NULL);
	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);

	D_ASSERT(rpc_priv->drp_state == RPC_INITED    ||
		 rpc_priv->drp_state == RPC_COMPLETED ||
		 rpc_priv->drp_state == RPC_CANCELED);
	epi = rpc_priv->drp_epi;
	D_ASSERT(epi != NULL);

	DAOS_INIT_LIST_HEAD(&resend_list);

	pthread_mutex_lock(&epi->epi_mutex);
	/* remove from inflight queue */
	daos_list_del_init(&rpc_priv->drp_epi_link);
	if (rpc_priv->drp_state == RPC_COMPLETED)
		epi->epi_reply_num++;
	else /* RPC_CANCELED or RPC_INITED */
		epi->epi_req_num--;
	D_ASSERT(epi->epi_req_num >= epi->epi_reply_num);

	/* decref corresponding to addref in dtp_context_req_track */
	dtp_req_decref(req);

	/* process waitq */
	inflight = epi->epi_req_num - epi->epi_reply_num;
	D_ASSERT(inflight >= 0 && inflight <= DTP_MAX_INFLIGHT_PER_EP_CTX);
	credits = DTP_MAX_INFLIGHT_PER_EP_CTX - inflight;
	while (credits > 0 && !daos_list_empty(&epi->epi_req_waitq)) {
		D_ASSERT(epi->epi_req_wait_num > 0);
		rpc_priv = daos_list_entry(epi->epi_req_waitq.next,
					   struct dtp_rpc_priv, drp_epi_link);
		rpc_priv->drp_state = RPC_INITED;
		rpc_priv->drp_ts = dtp_time_usec(0);
		/* remove from waitq and add to in-flight queue */
		daos_list_move_tail(&rpc_priv->drp_epi_link, &epi->epi_req_q);
		epi->epi_req_wait_num--;
		D_ASSERT(epi->epi_req_wait_num >= 0);
		epi->epi_req_num++;
		D_ASSERT(epi->epi_req_num >= epi->epi_reply_num);

		/* add to resend list */
		daos_list_add_tail(&rpc_priv->drp_tmp_link, &resend_list);
		credits--;
	}
	pthread_mutex_unlock(&epi->epi_mutex);

	/* re-submit the rpc req */
	daos_list_for_each_entry_safe(rpc_priv, next, &resend_list,
				      drp_tmp_link) {
		daos_list_del_init(&rpc_priv->drp_tmp_link);
		rpc_priv->drp_state = RPC_REQ_SENT;
		rc = dtp_hg_req_send(rpc_priv);
		if (rc == 0)
			continue;

		dtp_req_addref(&rpc_priv->drp_pub);
		D_ERROR("dtp_hg_req_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
		rpc_priv->drp_state = RPC_INITED;
		dtp_context_req_untrack(&rpc_priv->drp_pub);
		/* for error case here */
		dtp_rpc_complete(rpc_priv, rc);
		dtp_req_decref(&rpc_priv->drp_pub);
	}
}

int
dtp_context_idx(dtp_context_t dtp_ctx, int *ctx_idx)
{
	struct dtp_context	*ctx;
	int			rc = 0;

	if (dtp_ctx == DTP_CONTEXT_NULL || ctx_idx == NULL) {
		D_ERROR("invalid parameter, dtp_ctx: %p, ctx_idx: %p.\n",
			dtp_ctx, ctx_idx);
		D_GOTO(out, rc = -DER_INVAL);
	}

	ctx = (struct dtp_context *)dtp_ctx;
	*ctx_idx = ctx->dc_idx;

out:
	return rc;
}

int
dtp_context_num(int *ctx_num)
{
	if (ctx_num == NULL) {
		D_ERROR("invalid parameter of NULL ctx_num.\n");
		return -DER_INVAL;
	}

	*ctx_num = dtp_gdata.dg_ctx_num;
	return 0;
}

bool
dtp_context_empty(int locked)
{
	bool rc = false;

	if (locked == 0)
		pthread_rwlock_rdlock(&dtp_gdata.dg_rwlock);

	rc = daos_list_empty(&dtp_gdata.dg_ctx_list);

	if (locked == 0)
		pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);

	return rc;
}

int
dtp_progress(dtp_context_t dtp_ctx, int64_t timeout,
	     dtp_progress_cond_cb_t cond_cb, void *arg)
{
	struct dtp_context	*ctx;
	int64_t			 hg_timeout;
	uint64_t		 now;
	uint64_t		 end = 0;
	int			 rc = 0;

	/** validate input parameters */
	if (dtp_ctx == DTP_CONTEXT_NULL) {
		D_ERROR("invalid parameter (NULL dtp_ctx).\n");
		D_GOTO(out, rc = -DER_INVAL);
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
			D_GOTO(out, rc = 0);
		if (rc < 0)
			/**
			 * something wrong happened during the callback
			 * execution
			 */
			D_GOTO(out, rc);
	}

	ctx = (struct dtp_context *)dtp_ctx;
	if (timeout == 0 || cond_cb == NULL) {
		/** fast path */
		rc = dtp_hg_progress(&ctx->dc_hg_ctx, timeout);
		if (rc && rc != -DER_TIMEDOUT) {
			D_ERROR("dtp_hg_progress failed, rc: %d.\n", rc);
			D_GOTO(out, rc);
		}

		if (cond_cb) {
			int ret;

			/**
			 * Don't clobber rc which might be set to
			 * -DER_TIMEDOUT
			 */
			ret = cond_cb(arg);
			/** be careful with return code */
			if (ret > 0)
				D_GOTO(out, rc = 0);
			if (ret < 0)
				D_GOTO(out, rc = ret);
		}

		D_GOTO(out, rc);
	}

	/** Progress with callback and non-null timeout */
	if (timeout <= 0) {
		D_ASSERT(timeout < 0);
		/**
		 * For infinite timeout, use a mercury timeout of 1s to avoid
		 * being blocked indefinitely if another thread has called
		 * dtp_hg_progress() behind our back
		 */
		hg_timeout = 0;
	} else  {
		now = dtp_time_usec(0);
		end = now + timeout;
		/** similiarly, probe more frequently if timeout is large */
		if (timeout > 1000 * 1000)
			hg_timeout = 1000 * 1000;
		else
			hg_timeout = timeout;
	}

	while (true) {
		rc = dtp_hg_progress(&ctx->dc_hg_ctx, hg_timeout);
		if (rc && rc != -DER_TIMEDOUT) {
			D_ERROR("dtp_hg_progress failed with %d\n", rc);
			D_GOTO(out, rc = 0);
		}

		/** execute callback */
		rc = cond_cb(arg);
		if (rc > 0)
			D_GOTO(out, rc = 0);
		if (rc < 0)
			D_GOTO(out, rc);

		/** check for timeout, if not infinite */
		if (timeout > 0) {
			now = dtp_time_usec(0);
			if (now >= end) {
				rc = -DER_TIMEDOUT;
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
