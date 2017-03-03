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
 *
 * This file is part of CaRT. It implements the main fault tolerance module
 * routines.
 */

#include <crt_internal.h>

struct crt_lm_evict_in {
	crt_rank_t		clei_rank;
};

struct crt_lm_evict_out {
	int cleo_succeeded;
	int cleo_rc;
};

/* global data for liveness map management of the primary service group */
struct lm_grp_srv_t {
	/* ptr to the public primary service group structure */
	crt_group_t		*lgs_grp;
	/* Minimal Viable Size (MVS) is the minimum number of live processes in
	 * a service process group for the service to function well. The service
	 * process group shuts down if the number of live processes falls below
	 * the MVS
	 */
	uint32_t		 lgs_mvs;
	/* flag for ranks subscribed to RAS events */
	uint32_t		 lgs_ras:1,
	/* flag for RAS bcast in progress */
				 lgs_bcast_in_prog:1;
	uint32_t		 lgs_bcast_idx;
	crt_rank_list_t		*lgs_bcast_list;
	/* ranks subscribed to RAS events*/
	crt_rank_list_t		*lgs_ras_ranks;
	pthread_rwlock_t	 lgs_rwlock;
};

struct crt_lm_gdata_t {
	struct lm_grp_srv_t	clg_lm_grp_srv;
	volatile unsigned int	clg_refcount;
	volatile unsigned int	clg_inited;
	pthread_rwlock_t	clg_rwlock;
};

struct crt_lm_gdata_t crt_lm_gdata;
static pthread_once_t lm_gdata_init_once = PTHREAD_ONCE_INIT;


static int
lm_bcast_eviction_event(crt_context_t crt_ctx, struct lm_grp_srv_t *lm_grp_srv,
			crt_rank_t crt_rank);


static inline bool
lm_am_i_ras_mgr(struct lm_grp_srv_t *lm_grp_srv)
{
	crt_rank_t	grp_self;
	bool		rc;

	crt_group_rank(lm_grp_srv->lgs_grp, &grp_self);
	C_ASSERT(lm_grp_srv->lgs_ras_ranks->rl_nr.num > 0);
	pthread_rwlock_rdlock(&lm_grp_srv->lgs_rwlock);
	rc = (grp_self == lm_grp_srv->lgs_ras_ranks->rl_ranks[0])
	      ? true : false;
	pthread_rwlock_unlock(&lm_grp_srv->lgs_rwlock);

	return rc;
}

/*
 * This function is called on completion of a broadcast on the broadcast
 * initiator node only.  It either resubmits the broadcast (possibly with an
 * updated exclusion list) on failure, or submits a new broadcast if there are
 * further pending updates or simply clears the broadcast in flight flag is
 * there is no more work to do.
 */
static int
evict_corpc_cb(const struct crt_cb_info *cb_info)
{
	uint32_t			 num;
	uint32_t			 crt_rank;
	uint32_t			 tmp_idx;
	struct crt_lm_evict_in		*evict_in;
	struct crt_lm_evict_out		*reply_result;
	crt_rank_t			 grp_self;
	uint32_t			 grp_size;
	struct lm_grp_srv_t		*lm_grp_srv;
	crt_context_t			 crt_ctx;
	int				 rc = 0;

	lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;
	rc = crt_group_rank(lm_grp_srv->lgs_grp, &grp_self);
	if (rc != 0) {
		C_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}
	rc = crt_group_size(lm_grp_srv->lgs_grp, &grp_size);
	if (rc != 0) {
		C_ERROR("crt_group_size() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}
	num = (uintptr_t) cb_info->cci_arg;
	crt_ctx = cb_info->cci_rpc->cr_ctx;
	rc = cb_info->cci_rc;
	if (rc != 0) {
		C_ERROR("RPC error, rc: %d.\n", rc);
		C_GOTO(out, rc = -CER_CORPC_INCOMPLETE);
	}
	reply_result = crt_reply_get(cb_info->cci_rpc);
	/* retry if the previous bcast has failed */
	if (reply_result->cleo_succeeded != grp_size - num) {
		C_ERROR("rank: %d eviction request broadcast failed. "
			"Sent to %d targets, succeeded on %d targets\n",
			grp_self, grp_size - num, reply_result->cleo_succeeded);
		evict_in = crt_req_get(cb_info->cci_rpc);
		crt_rank = evict_in->clei_rank;
		lm_bcast_eviction_event(crt_ctx, lm_grp_srv, crt_rank);
		C_GOTO(out, rc = -CER_CORPC_INCOMPLETE);
	}

	/* exit if no more entries to bcast */
	pthread_rwlock_wrlock(&lm_grp_srv->lgs_rwlock);
	/* advance the index for the last successful bcast */
	lm_grp_srv->lgs_bcast_idx++;
	C_ASSERT(lm_grp_srv->lgs_bcast_idx <=
			lm_grp_srv->lgs_bcast_list->rl_nr.num);
	if (lm_grp_srv->lgs_bcast_idx ==
	    lm_grp_srv->lgs_bcast_list->rl_nr.num) {
		lm_grp_srv->lgs_bcast_in_prog = 0;
		pthread_rwlock_unlock(&lm_grp_srv->lgs_rwlock);
		C_GOTO(out, rc);
	}
	/* bcast the next entry */
	tmp_idx = lm_grp_srv->lgs_bcast_idx;
	crt_rank = lm_grp_srv->lgs_bcast_list->rl_ranks[tmp_idx];
	pthread_rwlock_unlock(&lm_grp_srv->lgs_rwlock);
	rc = lm_bcast_eviction_event(crt_ctx, lm_grp_srv, crt_rank);
	if (rc != 0)
		C_ERROR("lm_bcast_eviction_event() failed, rc: %d\n", rc);

out:
	return rc;
}

/*
 * This function is called on the RAS leader to initiate an eviction
 * notification broadcast. It can be invoked either in the case of a new
 * eviction by crt_progress() or from the completion callback of a previous
 * broadcast.
 */
static int
lm_bcast_eviction_event(crt_context_t crt_ctx, struct lm_grp_srv_t *lm_grp_srv,
			crt_rank_t crt_rank)
{
	crt_rpc_t			*evict_corpc;
	struct crt_lm_evict_in		*evict_in;
	crt_rank_list_t			*excluded_ranks = NULL;
	uint32_t			 num;
	crt_rank_t			 grp_self;
	int				 rc = 0;

	rc = crt_group_rank(lm_grp_srv->lgs_grp, &grp_self);
	if (rc != 0) {
		C_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}
	rc = crt_grp_failed_ranks_dup(lm_grp_srv->lgs_grp, &excluded_ranks);
	if (rc != 0) {
		C_ERROR("crt_grp_failed_ranks_dup() failed. rc %d\n", rc);
		C_GOTO(out, rc);
	}
	C_ASSERT(excluded_ranks != NULL);
	rc = crt_rank_list_append(excluded_ranks, grp_self);
	if (rc != 0) {
		C_ERROR("crt_rank_list_append() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}
	rc = crt_corpc_req_create(crt_ctx,
				  lm_grp_srv->lgs_grp,
				  excluded_ranks,
				  CRT_OPC_RANK_EVICT, NULL, NULL, 0,
				  crt_tree_topo(CRT_TREE_KNOMIAL, 4),
				  &evict_corpc);
	if (rc != 0) {
		C_ERROR("crt_corpc_req_create() failed, rc: %d.\n", rc);
		crt_rank_list_free(excluded_ranks);
		C_GOTO(out, rc);
	}
	evict_in = crt_req_get(evict_corpc);
	evict_in->clei_rank = crt_rank;
	num = excluded_ranks->rl_nr.num;
	rc = crt_req_send(evict_corpc, evict_corpc_cb,
			  (void *) (uintptr_t) num);
	crt_rank_list_free(excluded_ranks);
	C_DEBUG("ras event broadcast sent, initiator rank %d, rc %d\n",
		grp_self, rc);

out:
	return rc;
}

/*
 * This function is called on all RAS subscribers. This routine appends the pmix
 * rank of the failed process to the tail of the list of failed processes. This
 * routine also modifies the liveness map to indicate the current liveness of
 * the pmix rank. This function is idempotent, i.e. when called multiple times
 * with the same pmix rank, only the first call takes effect.
 */
static void
lm_ras_event_hdlr_internal(crt_rank_t crt_rank)
{
	crt_rank_t			 grp_self;
	struct lm_grp_srv_t		*lm_grp_srv;
	int				 rc = 0;

	C_ASSERT(crt_initialized());
	C_ASSERT(crt_is_service());

	lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;
	rc = crt_group_rank(lm_grp_srv->lgs_grp, &grp_self);
	if (rc != 0) {
		C_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}
	C_DEBUG("ras rank %d got PMIx notification, cart rank: %d.\n",
		grp_self, crt_rank);

	rc = crt_rank_evict(lm_grp_srv->lgs_grp, crt_rank);
	if (rc != 0) {
		C_ERROR("crt_rank_evict() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}

	/*
	 * TODO: handle possible race condition when the RAS bcast msg arrives
	 * before the local RAS event notification
	 */
	pthread_rwlock_wrlock(&lm_grp_srv->lgs_rwlock);
	rc = crt_rank_list_append(lm_grp_srv->lgs_bcast_list, crt_rank);
	if (rc != 0) {
		pthread_rwlock_unlock(&lm_grp_srv->lgs_rwlock);
		C_ERROR("crt_rank_list_append() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}
	/* purge the RAS rank list */
	rc = crt_rank_list_del(lm_grp_srv->lgs_ras_ranks, crt_rank);
	pthread_rwlock_unlock(&lm_grp_srv->lgs_rwlock);
	if (rc != 0) {
		C_ERROR("rank %d, crt_rank_list_del() failed, rc: %d.\n",
			grp_self, rc);
		C_GOTO(out, rc);
	}

out:
	return;
}

static bool
lm_bcast_in_progress(struct lm_grp_srv_t *lm_grp_srv)
{

	return lm_grp_srv->lgs_bcast_in_prog;

}

static void
lm_drain_evict_req_start(crt_context_t crt_ctx)
{
	struct lm_grp_srv_t		*lm_grp_srv;
	uint32_t			 tmp_idx;
	uint32_t			 crt_rank;
	crt_rank_t			 grp_self;
	int				 rc = 0;

	C_ASSERT(crt_initialized());
	C_ASSERT(crt_is_service());
	lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;

	rc = crt_group_rank(lm_grp_srv->lgs_grp, &grp_self);
	if (rc != 0) {
		C_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}
	pthread_rwlock_rdlock(&lm_grp_srv->lgs_rwlock);
	/* return if bcast already in progress */
	if (lm_bcast_in_progress(lm_grp_srv)) {
		pthread_rwlock_unlock(&lm_grp_srv->lgs_rwlock);
		C_GOTO(out, rc);
	}
	pthread_rwlock_unlock(&lm_grp_srv->lgs_rwlock);
	pthread_rwlock_wrlock(&lm_grp_srv->lgs_rwlock);
	/* return if bcast already in progress */
	if (lm_bcast_in_progress(lm_grp_srv)) {
		pthread_rwlock_unlock(&lm_grp_srv->lgs_rwlock);
		C_GOTO(out, rc);
	}
	C_ASSERT(lm_grp_srv->lgs_bcast_idx <=
		 lm_grp_srv->lgs_bcast_list->rl_nr.num);
	/* return if no more pending entries */
	if (lm_grp_srv->lgs_bcast_idx ==
	    lm_grp_srv->lgs_bcast_list->rl_nr.num) {
		pthread_rwlock_unlock(&lm_grp_srv->lgs_rwlock);
		C_GOTO(out, rc);
	}
	tmp_idx = lm_grp_srv->lgs_bcast_idx;
	crt_rank = lm_grp_srv->lgs_bcast_list->rl_ranks[tmp_idx];
	lm_grp_srv->lgs_bcast_in_prog = 1;
	pthread_rwlock_unlock(&lm_grp_srv->lgs_rwlock);
	rc = lm_bcast_eviction_event(crt_ctx, lm_grp_srv, crt_rank);
	if (rc != 0)
		C_ERROR("lm_bcast_eviction_event() failed. rank %d\n",
			grp_self);
out:
	return;
}

/* this function is called by the fake event utility thread */
void
crt_lm_fake_event_notify_fn(crt_rank_t crt_rank)
{
	int				 rc = 0;

	if (!crt_initialized()) {
		C_ERROR("CRT not initialized.\n");
		C_GOTO(out, rc);
	}
	if (!crt_is_service()) {
		C_ERROR("Caller must be a service rocess.\n");
		C_GOTO(out, rc);
	}

	if (crt_lm_gdata.clg_lm_grp_srv.lgs_ras == 0)
		C_GOTO(out, rc);
	lm_ras_event_hdlr_internal(crt_rank);

out:
	return;
}

int
crt_hdlr_rank_evict(crt_rpc_t *rpc_req)
{
	struct crt_lm_evict_in		*in_data;
	struct crt_lm_evict_out		*out_data;
	crt_rank_t			 crt_rank;
	struct lm_grp_srv_t		*lm_grp_srv;
	crt_rank_t			 grp_self;
	uint32_t			 tmp_idx;
	int				 rc = 0;

	in_data = crt_req_get(rpc_req);
	out_data = crt_reply_get(rpc_req);
	crt_rank = in_data->clei_rank;

	C_ASSERT(crt_initialized());
	C_ASSERT(crt_is_service());

	lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;
	rc = crt_group_rank(lm_grp_srv->lgs_grp, &grp_self);
	if (rc != 0) {
		C_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}
	C_DEBUG("ras rank %d requests to evict rank %d",
		rpc_req->cr_ep.ep_rank, crt_rank);
	/*
	 * TODO: handle possible race condition when the RAS bcast msg arrives
	 * before the local RAS event notification
	 */
	if (lm_grp_srv->lgs_ras) {
		pthread_rwlock_wrlock(&lm_grp_srv->lgs_rwlock);
		tmp_idx = lm_grp_srv->lgs_bcast_idx;
		if (crt_rank == lm_grp_srv->lgs_bcast_list->rl_ranks[tmp_idx])
			lm_grp_srv->lgs_bcast_idx++;
		else
			C_ERROR("eviction requests received out of order.\n");
		pthread_rwlock_unlock(&lm_grp_srv->lgs_rwlock);
		C_GOTO(out, rc);
	}
	rc = crt_rank_evict(lm_grp_srv->lgs_grp, crt_rank);
	if (rc != 0) {
		C_ERROR("crt_rank_evict() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}

out:
	out_data->cleo_rc = rc;
	out_data->cleo_succeeded = 1;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		C_ERROR("crt_reply_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_req->cr_opc);
	return rc;
}

static void
lm_event_hdlr(crt_rank_t crt_rank, void *args)
{
	lm_ras_event_hdlr_internal(crt_rank);
}

/* compute list of subscribed ranks and sign up for RAS notifications */
static int
crt_lm_grp_init(crt_group_t *grp)
{
	int			 codes[1] = {0};
	int			 ncodes = 1;
	int			 i;
	crt_rank_t		 tmp_rank;
	uint32_t		 grp_size;
	crt_rank_t		 grp_self;
	uint32_t		 num_ras_ranks;
	uint32_t		 mvs;
	struct lm_grp_srv_t	*lm_grp_srv;
	int			 rc = 0;

	C_ASSERT(crt_is_service());
	rc = crt_group_size(grp, &grp_size);
	if (rc != 0) {
		C_ERROR("crt_group_size() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}
	rc = crt_group_rank(grp, &grp_self);
	if (rc != 0) {
		C_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}
	/*
	 * Compute the default MVS. Based on empirical evidence the MVS obtained
	 * through the following formula works reasonably well.
	 */
	lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;
	lm_grp_srv->lgs_grp = grp;
	lm_grp_srv->lgs_mvs =
		max((grp_size/2) + 1, min(grp_size - 5, grp_size*0.95));
	mvs = lm_grp_srv->lgs_mvs;
	/* If the failed ranks are all subscribed ranks and the number of live
	 * ranks equals the minimum viable size, there should be at least 1 rank
	 * subscribed to RAS
	 */
	num_ras_ranks = grp_size - mvs + 1;
	C_DEBUG("grp_size %d, mvs %d, num_ras_ranks %d\n",
		grp_size, mvs, num_ras_ranks);
	/* create a dummy list to simplify list management */
	lm_grp_srv->lgs_bcast_idx = 0;
	lm_grp_srv->lgs_ras_ranks = crt_rank_list_alloc(num_ras_ranks);
	if (lm_grp_srv->lgs_ras_ranks == NULL) {
		C_ERROR("crt_rank_list_alloc failed.\n");
		C_GOTO(out, rc = -CER_NOMEM);
	}
	lm_grp_srv->lgs_bcast_list = crt_rank_list_alloc(0);
	if (lm_grp_srv->lgs_bcast_list == NULL) {
		C_ERROR("crt_rank_list_alloc failed.\n");
		crt_rank_list_free(lm_grp_srv->lgs_ras_ranks);
		C_GOTO(out, rc = -CER_NOMEM);
	}
	for (i = 0; i < num_ras_ranks; i++) {
		/* select ras ranks as evenly distributed as possible */
		tmp_rank = (i*grp_size + num_ras_ranks - 1) / num_ras_ranks;
		C_ASSERTF(tmp_rank < grp_size, "tmp_rank %d, gp_size %d\n",
			  tmp_rank, grp_size);
		/* rank i should sign up for RAS notifications */
		lm_grp_srv->lgs_ras_ranks->rl_ranks[i] = tmp_rank;
		/* sign myself up for RAS notifications */
		if (grp_self != tmp_rank)
			continue;
		lm_grp_srv->lgs_ras = 1;
		crt_register_event_cb(codes, ncodes, lm_event_hdlr, NULL);
	}
	pthread_rwlock_init(&lm_grp_srv->lgs_rwlock, NULL);
	/* every ras rank prints out its list of subscribed ranks */
	if (CRT_DBG && lm_grp_srv->lgs_ras) {
		rc = crt_rank_list_dump(lm_grp_srv->lgs_ras_ranks,
					"subscribed_ranks: ");
		if (rc != 0) {
			C_ERROR("crt_rank_list_dump() failed, rc: %d\n", rc);
			crt_rank_list_free(lm_grp_srv->lgs_ras_ranks);
			crt_rank_list_free(lm_grp_srv->lgs_bcast_list);
		}
	}

out:
	return rc;
}

static void
crt_lm_grp_fini(struct lm_grp_srv_t *lm_grp_srv)
{
	crt_rank_list_free(lm_grp_srv->lgs_ras_ranks);
	crt_rank_list_free(lm_grp_srv->lgs_bcast_list);
}

static void
lm_prog_cb(crt_context_t crt_ctx, void *args)
{
	struct lm_grp_srv_t		*lm_grp_srv;
	int				 ctx_idx;
	int				 rc;

	C_ASSERT(crt_initialized());
	C_ASSERT(crt_is_service());

	lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;
	/* only the RAS manager can initiate the bcast */
	if (!lm_am_i_ras_mgr(lm_grp_srv))
		C_GOTO(out, rc);
	rc = crt_context_idx(crt_ctx, &ctx_idx);
	if (rc != 0) {
		C_ERROR("crt_context_idx() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}
	/* only crt_context 0 can initiate the bcast */
	if (ctx_idx != 0)
		C_GOTO(out, rc);
	lm_drain_evict_req_start(crt_ctx);

out:
	return;
}

int crt_rank_evict_corpc_aggregate(crt_rpc_t *source,
				   crt_rpc_t *result,
				   void *priv)
{
	crt_rank_t			 my_rank;
	struct crt_lm_evict_out		*reply_source;
	struct crt_lm_evict_out		*reply_result;
	int				 rc = 0;

	rc = crt_group_rank(NULL, &my_rank);
	if (rc != 0) {
		C_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		C_GOTO(out, rc);
	}
	reply_source = crt_reply_get(source);
	if (reply_source == NULL) {
		C_ERROR("crt_reply_get() failed.\n");
		C_GOTO(out, rc = -CER_INVAL);
	}
	reply_result = crt_reply_get(result);
	if (reply_result == NULL) {
		C_ERROR("crt_reply_get() failed.\n");
		C_GOTO(out, rc = -CER_INVAL);
	}
	C_DEBUG("reply_source->cleo_succeeded %d, reply_result->cleo_succeeded "
		"%d\n", reply_source->cleo_succeeded,
		reply_result->cleo_succeeded);
	reply_result->cleo_succeeded += reply_source->cleo_succeeded;

out:
	return rc;
}

struct crt_corpc_ops crt_rank_evict_co_ops = {
	.co_aggregate = crt_rank_evict_corpc_aggregate,
};

/*
 * initialize the global lm data
 */
static void
lm_gdata_init(void)
{
	crt_lm_gdata.clg_refcount = 0;
	crt_lm_gdata.clg_inited = 1;
	pthread_rwlock_init(&crt_lm_gdata.clg_rwlock, NULL);
}

/* destroy the global data for the lm module */
static void
lm_gdata_destroy(void)
{
	int	rc;

	rc = pthread_rwlock_destroy(&crt_lm_gdata.clg_rwlock);
	if (rc != 0) {
		C_ERROR("failed to destroy clg_rwlock, rc: %d.\n", rc);
		C_GOTO(out, rc);
	}

	/* allow the same program to re-initialize */
	crt_lm_gdata.clg_refcount = 0;
	crt_lm_gdata.clg_inited = 0;

out:
	return;
}

void
crt_lm_init(void)
{
	crt_group_t			*grp;
	struct	crt_grp_priv		*grp_priv;
	int				 rc = 0;

	if (!crt_initialized()) {
		C_ERROR("CRT not initialized.\n");
		C_GOTO(out, rc = -CER_UNINIT);
	}
	/* this is the only place we need a grp_priv pointer, since we need to
	 * retrieve the public struct pointer of the local primary group at
	 * initialization.
	 */
	grp_priv = crt_grp_pub2priv(NULL);
	C_ASSERT(grp_priv != NULL);
	grp = &grp_priv->gp_pub;

	pthread_once(&lm_gdata_init_once, lm_gdata_init);
	C_ASSERT(crt_lm_gdata.clg_inited == 1);
	pthread_rwlock_wrlock(&crt_lm_gdata.clg_rwlock);
	crt_lm_gdata.clg_refcount++;
	if (crt_lm_gdata.clg_refcount == 1 && crt_is_service()) {
		rc = crt_lm_grp_init(grp);
		if (rc != 0) {
			C_ERROR("crt_lm_grp_init() failed, rc %d.\n", rc);
			pthread_rwlock_unlock(&crt_lm_gdata.clg_rwlock);
			C_GOTO(out, rc);
		}
	}
	pthread_rwlock_unlock(&crt_lm_gdata.clg_rwlock);

	if (!crt_is_service()) {
		C_WARN("Called by a non-service rank.\n");
		return;
	}

	/* register callbacks to manage the liveness map here */
	crt_register_progress_cb(lm_prog_cb, grp);

out:
	return;
}

void
crt_lm_finalize(void)
{
	struct lm_grp_srv_t		*lm_grp_srv;
	int				 rc;

	if (crt_lm_gdata.clg_inited == 0) {
		C_ERROR("cannot finalize before crt_lm_init().\n");
		C_GOTO(out, rc);
	}
	pthread_rwlock_wrlock(&crt_lm_gdata.clg_rwlock);
	crt_lm_gdata.clg_refcount--;
	if (crt_lm_gdata.clg_refcount != 0) {
		pthread_rwlock_unlock(&crt_lm_gdata.clg_rwlock);
		C_GOTO(out, rc);
	}
	if (crt_is_service()) {
		lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;
		crt_lm_grp_fini(lm_grp_srv);
	}
	pthread_rwlock_unlock(&crt_lm_gdata.clg_rwlock);
	lm_gdata_destroy();

out:
	return;
}
