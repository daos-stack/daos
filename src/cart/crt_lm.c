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
 * This file is part of CaRT. It implements the main fault tolerance module
 * routines.
 */
#define D_LOGFAC	DD_FAC(lm)

#include <semaphore.h>

#include "crt_internal.h"

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
	uint32_t		 lgs_lm_ver;
	uint32_t		 lgs_bcast_idx;
	d_rank_list_t		*lgs_bcast_list;
	/* ranks subscribed to RAS events*/
	d_rank_list_t		*lgs_ras_ranks;
	pthread_rwlock_t	 lgs_rwlock;
};

struct lm_psr_cand {
	d_rank_t	pc_rank;
	bool		pc_pending_sample;
};

/* data for remote groups */
struct lm_grp_priv_t {
	d_list_t		 lgp_link;
	crt_group_t		*lgp_grp;
	uint32_t		 lgp_mvs;
	uint32_t		 lgp_lm_ver;
	/* PSR phy addr address in attached group */
	crt_phy_addr_t		 lgp_psr_phy_addr;
	/* PSR rank in attached group */
	d_rank_t		 lgp_psr_rank;
	/* PSR candidates for PSR failures */
	uint32_t		 lgp_num_psr;
	struct lm_psr_cand	*lgp_psr_cand;
	/* the index of psr candidate who has a pending resample RPC */
	int32_t			 lgp_last_tried_index;
	/* the target rank of each sample RPC is in this hash table */
	pthread_rwlock_t	 lgp_rwlock;
	sem_t			 lgp_sem;
	bool			 lgp_sampling;
};

struct crt_lm_gdata_t {
	/* data for remote service groups */
	d_list_t		clg_grp_remotes;
	/* data for the local service group */
	struct lm_grp_srv_t	clg_lm_grp_srv;
	volatile unsigned int	clg_refcount;
	volatile unsigned int	clg_inited;
	pthread_rwlock_t	clg_rwlock;
};

struct crt_lm_gdata_t crt_lm_gdata;
static pthread_once_t lm_gdata_init_once = PTHREAD_ONCE_INIT;


static int
lm_bcast_eviction_event(crt_context_t crt_ctx, struct lm_grp_srv_t *lm_grp_srv,
			d_rank_t crt_rank);


static inline bool
lm_am_i_ras_mgr(struct lm_grp_srv_t *lm_grp_srv)
{
	d_rank_t	grp_self;
	bool		rc;

	crt_group_rank(lm_grp_srv->lgs_grp, &grp_self);
	D_ASSERT(lm_grp_srv->lgs_ras_ranks->rl_nr > 0);
	D_RWLOCK_RDLOCK(&lm_grp_srv->lgs_rwlock);
	rc = (grp_self == lm_grp_srv->lgs_ras_ranks->rl_ranks[0])
	      ? true : false;
	D_RWLOCK_UNLOCK(&lm_grp_srv->lgs_rwlock);

	return rc;
}

/*
 * This function is called on completion of a broadcast on the broadcast
 * initiator node only.  It either resubmits the broadcast (possibly with an
 * updated exclusion list) on failure, or submits a new broadcast if there are
 * further pending updates or simply clears the broadcast in flight flag is
 * there is no more work to do.
 */
static void
evict_corpc_cb(const struct crt_cb_info *cb_info)
{
	uint32_t			 num;
	uint32_t			 crt_rank;
	uint32_t			 tmp_idx;
	struct crt_lm_evict_in		*evict_in;
	struct crt_lm_evict_out		*reply_result;
	d_rank_t			 grp_self;
	uint32_t			 grp_size;
	struct lm_grp_srv_t		*lm_grp_srv;
	crt_context_t			 crt_ctx;
	int				 rc = 0;

	lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;
	rc = crt_group_rank(lm_grp_srv->lgs_grp, &grp_self);
	if (rc != 0) {
		D_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = crt_group_size(lm_grp_srv->lgs_grp, &grp_size);
	if (rc != 0) {
		D_ERROR("crt_group_size() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	num = (uintptr_t) cb_info->cci_arg;
	crt_ctx = cb_info->cci_rpc->cr_ctx;
	rc = cb_info->cci_rc;
	if (rc != 0) {
		D_ERROR("RPC error, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	reply_result = crt_reply_get(cb_info->cci_rpc);
	/* retry if the previous bcast has failed */
	if (reply_result->cleo_succeeded != grp_size - num) {
		D_ERROR("rank: %d eviction request broadcast failed. "
			"Sent to %d targets, succeeded on %d targets\n",
			grp_self, grp_size - num, reply_result->cleo_succeeded);
		evict_in = crt_req_get(cb_info->cci_rpc);
		crt_rank = evict_in->clei_rank;
		lm_bcast_eviction_event(crt_ctx, lm_grp_srv, crt_rank);
		D_GOTO(out, rc);
	}

	/* exit if no more entries to bcast */
	D_RWLOCK_WRLOCK(&lm_grp_srv->lgs_rwlock);
	/* advance the index for the last successful bcast */
	lm_grp_srv->lgs_bcast_idx++;
	D_ASSERT(lm_grp_srv->lgs_bcast_idx <=
		 lm_grp_srv->lgs_bcast_list->rl_nr);
	if (lm_grp_srv->lgs_bcast_idx ==
	    lm_grp_srv->lgs_bcast_list->rl_nr) {
		lm_grp_srv->lgs_bcast_in_prog = 0;
		D_RWLOCK_UNLOCK(&lm_grp_srv->lgs_rwlock);
		D_GOTO(out, rc);
	}
	/* bcast the next entry */
	tmp_idx = lm_grp_srv->lgs_bcast_idx;
	crt_rank = lm_grp_srv->lgs_bcast_list->rl_ranks[tmp_idx];
	D_RWLOCK_UNLOCK(&lm_grp_srv->lgs_rwlock);
	rc = lm_bcast_eviction_event(crt_ctx, lm_grp_srv, crt_rank);
	if (rc != 0)
		D_ERROR("lm_bcast_eviction_event() failed, rc: %d\n", rc);

out:
	return;
}

/*
 * This function is called on the RAS leader to initiate an eviction
 * notification broadcast. It can be invoked either in the case of a new
 * eviction by crt_progress() or from the completion callback of a previous
 * broadcast.
 */
static int
lm_bcast_eviction_event(crt_context_t crt_ctx, struct lm_grp_srv_t *lm_grp_srv,
			d_rank_t crt_rank)
{
	crt_rpc_t			*evict_corpc;
	struct crt_lm_evict_in		*evict_in;
	d_rank_list_t			*excluded_ranks = NULL;
	uint32_t			 num;
	d_rank_t			 grp_self;
	int				 rc = 0;

	rc = crt_group_rank(lm_grp_srv->lgs_grp, &grp_self);
	if (rc != 0) {
		D_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = crt_grp_failed_ranks_dup(lm_grp_srv->lgs_grp, &excluded_ranks);
	if (rc != 0) {
		D_ERROR("crt_grp_failed_ranks_dup() failed. rc %d\n", rc);
		D_GOTO(out, rc);
	}
	D_ASSERT(excluded_ranks != NULL);
	rc = d_rank_list_append(excluded_ranks, grp_self);
	if (rc != 0) {
		D_ERROR("d_rank_list_append() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = crt_corpc_req_create(crt_ctx,
				  lm_grp_srv->lgs_grp,
				  excluded_ranks,
				  CRT_OPC_RANK_EVICT, NULL, NULL, 0,
				  crt_tree_topo(CRT_TREE_KNOMIAL, 4),
				  &evict_corpc);
	if (rc != 0) {
		D_ERROR("crt_corpc_req_create() failed, rc: %d.\n", rc);
		d_rank_list_free(excluded_ranks);
		D_GOTO(out, rc);
	}
	evict_in = crt_req_get(evict_corpc);
	evict_in->clei_rank = crt_rank;
	evict_in->clei_ver = lm_grp_srv->lgs_lm_ver;
	num = excluded_ranks->rl_nr;
	rc = crt_req_send(evict_corpc, evict_corpc_cb,
			  (void *) (uintptr_t) num);
	d_rank_list_free(excluded_ranks);
	D_DEBUG(DB_TRACE, "ras event broadcast sent, initiator rank %d, "
		"rc %d\n", grp_self, rc);

out:
	return rc;
}

/*
 * This function is called on all RAS subscribers. This function appends the
 * cart rank of the failed process to the tail of the list of failed processes.
 * This routine also modifies the liveness map to indicate the current liveness
 * of the pmix rank. This function is idempotent, i.e. when called multiple
 * times with the same pmix rank, only the first call takes effect.
 */
static void
lm_ras_event_hdlr_internal(d_rank_t crt_rank)
{
	d_rank_t			 grp_self;
	struct lm_grp_srv_t		*lm_grp_srv;
	int				 rc = 0;

	D_ASSERT(crt_initialized());
	D_ASSERT(crt_is_service());

	lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;
	rc = crt_group_rank(lm_grp_srv->lgs_grp, &grp_self);
	if (rc != 0) {
		D_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	D_DEBUG(DB_TRACE, "ras rank %d got PMIx notification, cart rank: %d.\n",
		grp_self, crt_rank);

	rc = crt_rank_evict(lm_grp_srv->lgs_grp, crt_rank);
	if (rc == -DER_EVICTED)
		D_GOTO(out, rc);
	if (rc != 0) {
		D_ERROR("crt_rank_evict() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_RWLOCK_WRLOCK(&lm_grp_srv->lgs_rwlock);
	lm_grp_srv->lgs_lm_ver++;
	rc = d_rank_list_append(lm_grp_srv->lgs_bcast_list, crt_rank);
	if (rc != 0) {
		D_RWLOCK_UNLOCK(&lm_grp_srv->lgs_rwlock);
		D_ERROR("d_rank_list_append() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	/* purge the RAS rank list */
	rc = d_rank_list_del(lm_grp_srv->lgs_ras_ranks, crt_rank);
	D_RWLOCK_UNLOCK(&lm_grp_srv->lgs_rwlock);
	if (rc != 0) {
		D_ERROR("rank %d, d_rank_list_del() failed, rc: %d.\n",
			grp_self, rc);
		D_GOTO(out, rc);
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
	d_rank_t			 grp_self;
	int				 rc = 0;

	D_ASSERT(crt_initialized());
	D_ASSERT(crt_is_service());
	lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;

	rc = crt_group_rank(lm_grp_srv->lgs_grp, &grp_self);
	if (rc != 0) {
		D_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	D_RWLOCK_RDLOCK(&lm_grp_srv->lgs_rwlock);
	/* return if bcast already in progress */
	if (lm_bcast_in_progress(lm_grp_srv)) {
		D_RWLOCK_UNLOCK(&lm_grp_srv->lgs_rwlock);
		D_GOTO(out, rc);
	}
	D_RWLOCK_UNLOCK(&lm_grp_srv->lgs_rwlock);
	D_RWLOCK_WRLOCK(&lm_grp_srv->lgs_rwlock);
	/* return if bcast already in progress */
	if (lm_bcast_in_progress(lm_grp_srv)) {
		D_RWLOCK_UNLOCK(&lm_grp_srv->lgs_rwlock);
		D_GOTO(out, rc);
	}
	D_ASSERT(lm_grp_srv->lgs_bcast_idx <=
		 lm_grp_srv->lgs_bcast_list->rl_nr);
	/* return if no more pending entries */
	if (lm_grp_srv->lgs_bcast_idx ==
	    lm_grp_srv->lgs_bcast_list->rl_nr) {
		D_RWLOCK_UNLOCK(&lm_grp_srv->lgs_rwlock);
		D_GOTO(out, rc);
	}
	tmp_idx = lm_grp_srv->lgs_bcast_idx;
	crt_rank = lm_grp_srv->lgs_bcast_list->rl_ranks[tmp_idx];
	lm_grp_srv->lgs_bcast_in_prog = 1;
	D_RWLOCK_UNLOCK(&lm_grp_srv->lgs_rwlock);
	rc = lm_bcast_eviction_event(crt_ctx, lm_grp_srv, crt_rank);
	if (rc != 0)
		D_ERROR("lm_bcast_eviction_event() failed. rank %d\n",
			grp_self);
out:
	return;
}

/* this function is called by the fake event utility thread */
void
crt_lm_fake_event_notify_fn(d_rank_t crt_rank, bool *dead)
{
	d_rank_t		grp_self;
	int			rc = 0;

	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc);
	}
	if (!crt_is_service()) {
		D_ERROR("Caller must be a service rocess.\n");
		D_GOTO(out, rc);
	}

	rc = crt_group_rank(NULL, &grp_self);
	if (rc != 0) {
		D_ERROR("crt_group_rank() failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	if (dead != NULL && crt_rank == grp_self)
		*dead = true;
	if (crt_lm_gdata.clg_lm_grp_srv.lgs_ras == 0)
		D_GOTO(out, rc);
	lm_ras_event_hdlr_internal(crt_rank);

out:
	return;
}

void
crt_hdlr_rank_evict(crt_rpc_t *rpc_req)
{
	struct crt_lm_evict_in		*in_data;
	struct crt_lm_evict_out		*out_data;
	d_rank_t			 crt_rank;
	uint32_t			 remote_version;
	struct lm_grp_srv_t		*lm_grp_srv;
	d_rank_t			 grp_self;
	int				 rc = 0;

	in_data = crt_req_get(rpc_req);
	out_data = crt_reply_get(rpc_req);
	crt_rank = in_data->clei_rank;
	remote_version = in_data->clei_ver;

	D_ASSERT(crt_initialized());
	D_ASSERT(crt_is_service());

	lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;
	rc = crt_group_rank(lm_grp_srv->lgs_grp, &grp_self);
	if (rc != 0) {
		D_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	D_DEBUG(DB_TRACE, "ras rank %d requests to evict rank %d",
		rpc_req->cr_ep.ep_rank, crt_rank);
	if (lm_grp_srv->lgs_ras) {
		D_RWLOCK_WRLOCK(&lm_grp_srv->lgs_rwlock);
		if (remote_version > lm_grp_srv->lgs_bcast_idx)
			lm_grp_srv->lgs_bcast_idx = remote_version;
		D_RWLOCK_UNLOCK(&lm_grp_srv->lgs_rwlock);
		D_GOTO(out, rc);
	}
	rc = crt_rank_evict(lm_grp_srv->lgs_grp, crt_rank);
	if (rc != 0) {
		D_ERROR("crt_rank_evict() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	D_RWLOCK_WRLOCK(&lm_grp_srv->lgs_rwlock);
	lm_grp_srv->lgs_lm_ver++;
	D_RWLOCK_UNLOCK(&lm_grp_srv->lgs_rwlock);

out:
	out_data->cleo_rc = rc;
	out_data->cleo_succeeded = 1;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d, opc: %#x.\n",
			rc, rpc_req->cr_opc);
}

static void
lm_event_hdlr(d_rank_t crt_rank, void *arg)
{
	lm_ras_event_hdlr_internal(crt_rank);
}

/* compute list of subscribed ranks and sign up for RAS notifications */
static int
crt_lm_grp_init(crt_group_t *grp)
{
	int			 i;
	d_rank_t		 tmp_rank;
	uint32_t		 grp_size;
	d_rank_t		 grp_self;
	uint32_t		 num_ras_ranks;
	uint32_t		 mvs;
	struct lm_grp_srv_t	*lm_grp_srv;
	int			 rc = 0;

	D_ASSERT(crt_is_service());
	rc = crt_group_size(grp, &grp_size);
	if (rc != 0) {
		D_ERROR("crt_group_size() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = crt_group_rank(grp, &grp_self);
	if (rc != 0) {
		D_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
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
	D_DEBUG(DB_TRACE, "grp_size %d, mvs %d, num_ras_ranks %d\n",
		grp_size, mvs, num_ras_ranks);
	lm_grp_srv->lgs_lm_ver = 0;
	/* create a dummy list to simplify list management */
	lm_grp_srv->lgs_bcast_idx = 0;
	lm_grp_srv->lgs_ras_ranks = d_rank_list_alloc(num_ras_ranks);
	if (lm_grp_srv->lgs_ras_ranks == NULL) {
		D_ERROR("d_rank_list_alloc failed.\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}
	lm_grp_srv->lgs_bcast_list = d_rank_list_alloc(0);
	if (lm_grp_srv->lgs_bcast_list == NULL) {
		D_ERROR("d_rank_list_alloc failed.\n");
		d_rank_list_free(lm_grp_srv->lgs_ras_ranks);
		D_GOTO(out, rc = -DER_NOMEM);
	}
	for (i = 0; i < num_ras_ranks; i++) {
		/* select ras ranks as evenly distributed as possible */
		tmp_rank = (i*grp_size + num_ras_ranks - 1) / num_ras_ranks;
		D_ASSERTF(tmp_rank < grp_size, "tmp_rank %d, gp_size %d\n",
			  tmp_rank, grp_size);
		/* rank i should sign up for RAS notifications */
		lm_grp_srv->lgs_ras_ranks->rl_ranks[i] = tmp_rank;
		/* sign myself up for RAS notifications */
		if (grp_self != tmp_rank)
			continue;
		lm_grp_srv->lgs_ras = 1;
		crt_register_event_cb(lm_event_hdlr, NULL);
	}

	rc = D_RWLOCK_INIT(&lm_grp_srv->lgs_rwlock, NULL);
	if (rc != 0) {
		d_rank_list_free(lm_grp_srv->lgs_ras_ranks);
		d_rank_list_free(lm_grp_srv->lgs_bcast_list);
		D_GOTO(out, rc);
	}

	/* every ras rank prints out its list of subscribed ranks */
	if ((D_LOGFAC | DLOG_DBG) && lm_grp_srv->lgs_ras) {
		rc = d_rank_list_dump(lm_grp_srv->lgs_ras_ranks,
				     "subscribed_ranks: ",
				     CRT_GROUP_ID_MAX_LEN);
		if (rc != 0) {
			D_ERROR("d_rank_list_dump() failed, rc: %d\n", rc);
			d_rank_list_free(lm_grp_srv->lgs_ras_ranks);
			d_rank_list_free(lm_grp_srv->lgs_bcast_list);
			D_RWLOCK_DESTROY(&lm_grp_srv->lgs_rwlock);
		}
	}

out:
	return rc;
}

static void
crt_lm_grp_fini(struct lm_grp_srv_t *lm_grp_srv)
{
	d_rank_list_free(lm_grp_srv->lgs_ras_ranks);
	d_rank_list_free(lm_grp_srv->lgs_bcast_list);
}

static void
lm_prog_cb(crt_context_t crt_ctx, void *arg)
{
	struct lm_grp_srv_t		*lm_grp_srv;
	int				 ctx_idx;
	int				 rc = 0;

	D_ASSERT(crt_initialized());
	D_ASSERT(crt_is_service());

	lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;
	/* only the RAS manager can initiate the bcast */
	if (!lm_am_i_ras_mgr(lm_grp_srv))
		D_GOTO(out, rc);
	rc = crt_context_idx(crt_ctx, &ctx_idx);
	if (rc != 0) {
		D_ERROR("crt_context_idx() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	/* only crt_context 0 can initiate the bcast */
	if (ctx_idx != 0)
		D_GOTO(out, rc);
	lm_drain_evict_req_start(crt_ctx);

out:
	return;
}

int crt_rank_evict_corpc_aggregate(crt_rpc_t *source,
				   crt_rpc_t *result,
				   void *arg)
{
	d_rank_t			 my_rank;
	struct crt_lm_evict_out		*reply_source;
	struct crt_lm_evict_out		*reply_result;
	int				 rc = 0;

	rc = crt_group_rank(NULL, &my_rank);
	if (rc != 0) {
		D_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	reply_source = crt_reply_get(source);
	if (reply_source == NULL) {
		D_ERROR("crt_reply_get() failed.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	reply_result = crt_reply_get(result);
	if (reply_result == NULL) {
		D_ERROR("crt_reply_get() failed.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	D_DEBUG(DB_TRACE, "reply_source->cleo_succeeded %d,"
		"reply_result->cleo_succeeded %d\n",
		reply_source->cleo_succeeded, reply_result->cleo_succeeded);
	reply_result->cleo_succeeded += reply_source->cleo_succeeded;

out:
	return rc;
}

struct crt_corpc_ops crt_rank_evict_co_ops = {
	.co_aggregate = crt_rank_evict_corpc_aggregate,
	.co_pre_forward = NULL,
};

static struct lm_grp_priv_t *
lm_grp_priv_find(crt_group_t *grp)
{
	struct lm_grp_priv_t	*lm_grp_priv;

	d_list_for_each_entry(lm_grp_priv, &crt_lm_gdata.clg_grp_remotes,
			      lgp_link) {
		if (lm_grp_priv->lgp_grp == grp)
			return lm_grp_priv;
	}

	return NULL;
}

struct sample_item {
	d_list_t	ri_link;
	d_rank_t	ri_rank;
};

/* unmark the pending sample flag for all entries in the PSR candidate list */
static void
lm_sample_flag_unmark(struct lm_grp_priv_t *lm_grp_priv)
{
	struct lm_psr_cand	*psr_cand;
	int			 i;

	D_ASSERT(lm_grp_priv != NULL);
	psr_cand = lm_grp_priv->lgp_psr_cand;
	/* unmark all PSRs */
	D_RWLOCK_WRLOCK(&lm_grp_priv->lgp_rwlock);
	for (i = 0; i < lm_grp_priv->lgp_num_psr; i++) {
		psr_cand[i].pc_pending_sample = false;
	}
	lm_grp_priv->lgp_last_tried_index = -1;
	lm_grp_priv->lgp_sampling = false;
	D_RWLOCK_UNLOCK(&lm_grp_priv->lgp_rwlock);
}

/* unmark the pending sample flag for rank in the PSR candidate list */
static void
lm_sample_flag_unmark_rank(struct lm_grp_priv_t *lm_grp_priv, d_rank_t rank)
{
	struct lm_psr_cand	*psr_cand;
	int			 i;
	bool			sampling = false;

	D_ASSERT(lm_grp_priv != NULL);
	psr_cand = lm_grp_priv->lgp_psr_cand;
	/* unmark provided rank only */
	D_RWLOCK_WRLOCK(&lm_grp_priv->lgp_rwlock);
	for (i = 0; i < lm_grp_priv->lgp_num_psr; i++) {
		if (psr_cand[i].pc_rank == rank) {
			psr_cand[i].pc_pending_sample = false;
		}
		if (psr_cand[i].pc_pending_sample) {
			sampling = true;
		}
	}
	if (!sampling) {
		lm_grp_priv->lgp_sampling = false;
	}
	D_RWLOCK_UNLOCK(&lm_grp_priv->lgp_rwlock);
}

static int
lm_update_active_psr(struct lm_grp_priv_t *lm_grp_priv)
{
	struct lm_psr_cand	*psr_cand;
	int			 i;
	bool			 evicted;
	int			 rc = -DER_MISC;

	D_ASSERT(lm_grp_priv != NULL);
	psr_cand = lm_grp_priv->lgp_psr_cand;
	D_RWLOCK_WRLOCK(&lm_grp_priv->lgp_rwlock);
	for (i = 0; i < lm_grp_priv->lgp_num_psr; i++) {
		evicted = crt_rank_evicted(lm_grp_priv->lgp_grp,
					   psr_cand[i].pc_rank);
		if (evicted)
			continue;
		lm_grp_priv->lgp_psr_rank = psr_cand[i].pc_rank;
		D_GOTO(out, rc = 0);
	}

out:
	D_RWLOCK_UNLOCK(&lm_grp_priv->lgp_rwlock);

	return rc;
}

/**
 * the callback for the sample RPC. Executed by the origin of the sample RPC
 * after the RPC reply is received.
 */
static void
lm_sample_rpc_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t				*rpc_req;
	struct crt_lm_memb_sample_out		*out_data;
	crt_group_t				*tgt_grp;
	struct crt_grp_priv			*grp_priv;
	d_rank_t				*delta;
	uint32_t				 num_delta;
	int					 i;
	uint32_t				 curr_ver;
	struct lm_grp_priv_t			*lm_grp_priv;
	int					 rc = 0;

	lm_grp_priv = cb_info->cci_arg;
	D_ASSERT(lm_grp_priv != NULL);
	tgt_grp = lm_grp_priv->lgp_grp;

	rpc_req = cb_info->cci_rpc;
	if (cb_info->cci_rc != 0) {
		D_ERROR("rpc failed. opc: %#x, cci_rc: %d.\n",
			rpc_req->cr_opc, cb_info->cci_rc);
		D_GOTO(out, rc = cb_info->cci_rc);
	}
	out_data = crt_reply_get(rpc_req);
	if (out_data->mso_rc != 0) {
		D_ERROR("sample RPC failed. rc %d\n", out_data->mso_rc);
		D_GOTO(out, rc = out_data->mso_rc);
	}

	/* compare the local version with the remote version */
	D_RWLOCK_RDLOCK(&lm_grp_priv->lgp_rwlock);
	curr_ver = lm_grp_priv->lgp_lm_ver;
	D_RWLOCK_UNLOCK(&lm_grp_priv->lgp_rwlock);
	D_DEBUG(DB_TRACE, "group name: %s, local version: %d, "
		"remote version %d.\n", tgt_grp->cg_grpid, curr_ver,
		out_data->mso_ver);
	if (out_data->mso_ver == curr_ver) {
		D_DEBUG(DB_TRACE, "Local version up to date.\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	/* remote version is newer, apply the delta locally */
	D_ASSERT(out_data->mso_ver > curr_ver);
	num_delta = out_data->mso_delta.iov_len/sizeof(d_rank_t);
	if (num_delta == 0) {
		D_ERROR("buffer empty.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	D_ASSERT(num_delta == out_data->mso_ver - curr_ver);
	delta = out_data->mso_delta.iov_buf;
	for (i = 0; i < num_delta; i++) {
		rc = crt_rank_evict(tgt_grp, delta[i]);
		if (rc != 0) {
			D_ERROR("crt_rank_evict() failed, rc: %d\n", rc);
			D_GOTO(out, rc);
		}
		D_RWLOCK_WRLOCK(&lm_grp_priv->lgp_rwlock);
		lm_grp_priv->lgp_lm_ver++;
		D_RWLOCK_UNLOCK(&lm_grp_priv->lgp_rwlock);
	}

	rc = lm_update_active_psr(lm_grp_priv);
	if (rc != 0)
		D_ERROR("lm_update_active_psr() failed. rc: %d\n", rc);

out:

	grp_priv = container_of(tgt_grp, struct crt_grp_priv, gp_pub);
	/* addref in lm_sample_rpc() */
	crt_grp_priv_decref(grp_priv);

	/* Update the sampling list with the result so that new RPCs can be sent
	 * correctly.  Note that on success all pending flags are cleared,
	 * however for any other case only the target rank.
	 */
	if (rc == 0) {
		lm_sample_flag_unmark(lm_grp_priv);
	} else {
		lm_sample_flag_unmark_rank(lm_grp_priv,
					   rpc_req->cr_ep.ep_rank);
	}

	return;
}

/**
 * To be called by a client process. This function sends the local version
 * number to the PSR, gets back the remote version number and the delta between
 * the local membership list and the remote membershp list. If the remote
 * version is higher than the local version, apply the delta locally.
 */
static int
lm_sample_rpc(crt_context_t ctx, struct lm_grp_priv_t *lm_grp_priv,
	      d_rank_t tgt_rank)
{
	struct crt_lm_memb_sample_in	*in_data;
	crt_endpoint_t			 tgt_ep;
	d_rank_t			 grp_self;
	crt_rpc_t			*rpc_req;
	crt_group_t			*tgt_grp;
	struct crt_grp_priv		*grp_priv;
	uint32_t			 curr_ver;
	int				 rc = 0;

	D_ASSERT(lm_grp_priv != NULL);

	tgt_grp = lm_grp_priv->lgp_grp;
	D_RWLOCK_RDLOCK(&lm_grp_priv->lgp_rwlock);
	curr_ver = lm_grp_priv->lgp_lm_ver;
	D_RWLOCK_UNLOCK(&lm_grp_priv->lgp_rwlock);
	rc = crt_group_rank(NULL, &grp_self);
	if (rc != 0) {
		D_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	tgt_ep.ep_grp = tgt_grp;
	tgt_ep.ep_rank = tgt_rank;
	tgt_ep.ep_tag = 0;
	rc = crt_req_create(ctx, &tgt_ep, CRT_OPC_MEMB_SAMPLE, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	grp_priv = container_of(tgt_grp, struct crt_grp_priv, gp_pub);
	/* decref in lm_sample_rpc_cb() */
	crt_grp_priv_addref(grp_priv);

	in_data = crt_req_get(rpc_req);
	in_data->msi_ver = curr_ver;
	rc = crt_req_send(rpc_req, lm_sample_rpc_cb, lm_grp_priv);
	if (rc != 0) {
		D_ERROR("crt_req_send() failed, rc: %d\n", rc);
		crt_grp_priv_decref(grp_priv);
		D_GOTO(out, rc);
	}
	D_DEBUG(DB_TRACE, "sample RPC sent to rank %d in group %s.\n",
		tgt_rank, tgt_grp->cg_grpid);

out:
	return rc;
}

struct lm_uri_lookup_psr_cb_info {
	struct lm_grp_priv_t	*lul_lm_grp_priv;
	int			 lul_count;
	crt_lm_attach_cb_t	 lul_completion_cb;
	void			*lul_arg;
	pthread_rwlock_t	 lul_rwlock;
};

static void
lm_uri_lookup_psr_cb(const struct crt_cb_info *cb_info)
{
	crt_phy_addr_t			 psr_phy_addr = NULL;
	struct lm_grp_priv_t		*lm_grp_priv = NULL;
	struct crt_uri_lookup_out	*ul_out;
	struct crt_uri_lookup_in	*ul_in;
	struct lm_uri_lookup_psr_cb_info
					*lookup_cb_info;
	struct crt_lm_attach_cb_info	 tmp_cb_info;
	bool				 all_done = false;
	int				 rc;

	lookup_cb_info = cb_info->cci_arg;
	lm_grp_priv = lookup_cb_info->lul_lm_grp_priv;
	D_ASSERT(lm_grp_priv != NULL);
	ul_in = crt_req_get(cb_info->cci_rpc);
	ul_out = crt_reply_get(cb_info->cci_rpc);
	psr_phy_addr = ul_out->ul_uri;
	ul_out->ul_uri = NULL;
	rc = cb_info->cci_rc;
	if (rc != 0) {
		D_ERROR("RPC error, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	/* insert the uri into the address cache */
	rc = crt_grp_lc_uri_insert_all(lm_grp_priv->lgp_grp, ul_in->ul_rank, 0,
				       psr_phy_addr);
	if (rc != 0) {
		D_ERROR("crt_grp_lc_uri_insert failed, grp: %p, "
			"rank: %d, URI: %s, rc %d\n",
			lm_grp_priv->lgp_grp, ul_in->ul_rank, psr_phy_addr, rc);
		D_GOTO(out, rc);
	}
out:
	if (psr_phy_addr != NULL)
		free(psr_phy_addr);

	D_RWLOCK_WRLOCK(&lookup_cb_info->lul_rwlock);
	lookup_cb_info->lul_count++;
	if (lookup_cb_info->lul_count == lm_grp_priv->lgp_num_psr)
		all_done = true;
	D_RWLOCK_UNLOCK(&lookup_cb_info->lul_rwlock);
	if (!all_done)
		return;

	if (lookup_cb_info->lul_completion_cb != NULL) {
		tmp_cb_info.lac_arg = lookup_cb_info->lul_arg;
		tmp_cb_info.lac_rc = rc;
		lookup_cb_info->lul_completion_cb(&tmp_cb_info);
	}
	D_RWLOCK_DESTROY(&lookup_cb_info->lul_rwlock);
	D_FREE_PTR(lookup_cb_info);
}

/**
 * ask the active PSR for URIs of PSR candidates
 */
static int
lm_uri_lookup_psr(struct lm_grp_priv_t *lm_grp_priv,
		  crt_lm_attach_cb_t completion_cb, void *arg)
{
	crt_rpc_t				*ul_req;
	crt_endpoint_t				 psr_ep = {0};
	struct crt_uri_lookup_in		*ul_in;
	crt_context_t				 crt_ctx;
	struct lm_uri_lookup_psr_cb_info	*cb_info;
	struct crt_lm_attach_cb_info		 tmp_cb_info;
	int					 i;
	int					 rpc_count;
	int					 rc = 0;

	D_ASSERT(lm_grp_priv != NULL);

	D_ALLOC_PTR(cb_info);
	if (cb_info == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	cb_info->lul_lm_grp_priv	= lm_grp_priv;
	cb_info->lul_completion_cb	= completion_cb;
	cb_info->lul_arg		= arg;
	/* URI of default PSR is looked up through PMIx */
	cb_info->lul_count		= 1;

	rc = D_RWLOCK_INIT(&cb_info->lul_rwlock, NULL);
	if (rc != 0) {
		D_FREE(cb_info);
		D_GOTO(out, rc);
	}

	crt_ctx = crt_context_lookup(0);
	if (crt_ctx == NULL) {
		D_ERROR("crt_context 0 doesn't exist.\n");
		D_GOTO(cleanup, rc = -DER_INVAL);
	}

	rpc_count = 0;
	for (i = 1; i < lm_grp_priv->lgp_num_psr; i++) {
		psr_ep.ep_grp = lm_grp_priv->lgp_grp;
		psr_ep.ep_rank = lm_grp_priv->lgp_psr_rank;
		rc = crt_req_create(crt_ctx, &psr_ep, CRT_OPC_URI_LOOKUP,
				    &ul_req);
		if (rc != 0) {
			D_ERROR("crt_req_create URI_LOOKUP failed, "
				"rc: %d opc: %#x.\n", rc, CRT_OPC_URI_LOOKUP);
			D_GOTO(cleanup, rc);
		}

		ul_in = crt_req_get(ul_req);
		ul_in->ul_grp_id = lm_grp_priv->lgp_grp->cg_grpid;
		ul_in->ul_rank = lm_grp_priv->lgp_psr_cand[i].pc_rank;
		rc = crt_req_send(ul_req, lm_uri_lookup_psr_cb, cb_info);
		if (rc != 0) {
			D_ERROR("URI_LOOKUP (to group %s rank %d "
				"through PSR %d) "
				"request send failed, rc: %d.\n",
				ul_in->ul_grp_id, ul_in->ul_rank,
				psr_ep.ep_rank, rc);
			D_GOTO(cleanup, rc);
		}
		rpc_count++;
	}

	if (rpc_count != 0)
		D_GOTO(out, rc);

	if (completion_cb != NULL) {
		tmp_cb_info.lac_arg = arg;
		tmp_cb_info.lac_rc = rc;
		completion_cb(&tmp_cb_info);
		rc = 0;
	}

cleanup:
	D_RWLOCK_DESTROY(&cb_info->lul_rwlock);
	D_FREE(cb_info);

out:
	return rc;
}
/*
 * create, initialize a bookkeeping struct for the remote group _grp_, then
 * append this struct to a global list
 */
static struct lm_grp_priv_t *
lm_grp_priv_init(crt_group_t *grp, crt_lm_attach_cb_t completion_cb, void *arg)
{
	int			 i;
	uint32_t		 remote_grp_size;
	uint32_t		 local_rank;
	int			 num_psr;
	struct lm_grp_priv_t	*lm_grp_priv = NULL;
	struct lm_psr_cand	*psr_cand;
	int			 rc = 0;

	rc = crt_group_rank(NULL, &local_rank);
	if (rc != 0) {
		D_ERROR("crt_group_rank() failed, rc: %d\n", rc);
		return NULL;
	}
	rc = crt_group_size(grp, &remote_grp_size);
	if (rc != 0) {
		D_ERROR("crt_group_size() failed, rc: %d\n", rc);
		return NULL;
	}
	D_ALLOC_PTR(lm_grp_priv);
	if (lm_grp_priv == NULL)
		return NULL;

	lm_grp_priv->lgp_grp = grp;
	sem_init(&lm_grp_priv->lgp_sem, 0, 0);

	/*
	 * Compute the default MVS. Based on empirical evidence the MVS obtained
	 * through the following formula works reasonably well.
	 */
	lm_grp_priv->lgp_mvs =
		max((remote_grp_size/2) + 1,
		    min(remote_grp_size - 5, remote_grp_size*0.95));
	lm_grp_priv->lgp_lm_ver = 0;
	/**************
	* fields used only by remote groups
	***************/
	lm_grp_priv->lgp_psr_rank = local_rank % remote_grp_size;

	/* populate the PSR candidate list and insert their addresses into the
	 * address cache
	 */
	num_psr = remote_grp_size - lm_grp_priv->lgp_mvs + 1;
	lm_grp_priv->lgp_num_psr = num_psr;
	D_ALLOC_ARRAY(psr_cand, num_psr);
	if (psr_cand == NULL)
		D_GOTO(error_out, rc = -DER_NOMEM);

	D_DEBUG(DB_TRACE, "num_psr %d, list of PSRs: ", num_psr);
	psr_cand[0].pc_rank = lm_grp_priv->lgp_psr_rank;
	D_DEBUG(DB_TRACE, "%d ", psr_cand[0].pc_rank);
	for (i = 1; i < num_psr; i++) {
		/* same formula for picking ranks subscribed to RAS, with a
		 * shift
		 */
		psr_cand[i].pc_rank = ((i*remote_grp_size + num_psr - 1) /
				       num_psr + local_rank) %
				      remote_grp_size;
		D_DEBUG(DB_TRACE, "%d ", psr_cand[i].pc_rank);

	}
	lm_grp_priv->lgp_psr_cand = psr_cand;
	rc = D_RWLOCK_INIT(&lm_grp_priv->lgp_rwlock, NULL);
	if (rc != 0)
		D_GOTO(error_out, rc);

	rc = lm_uri_lookup_psr(lm_grp_priv, completion_cb, arg);
	if (rc != 0) {
		D_ERROR("lm_uri_lookup_psr failed, rc: %d\n", rc);
		D_GOTO(error_out, rc);
	}

	lm_grp_priv->lgp_last_tried_index = -1;

	return lm_grp_priv;

error_out:
	D_RWLOCK_DESTROY(&lm_grp_priv->lgp_rwlock);
	sem_destroy(&lm_grp_priv->lgp_sem);
	D_FREE(lm_grp_priv);
	D_FREE(psr_cand);
	return NULL;
}
static void
lm_grp_priv_destroy(struct lm_grp_priv_t *lm_grp_priv)
{

	D_ASSERT(lm_grp_priv != NULL);

	D_FREE(lm_grp_priv->lgp_psr_cand);
	D_RWLOCK_DESTROY(&lm_grp_priv->lgp_rwlock);
	sem_destroy(&lm_grp_priv->lgp_sem);
	D_FREE_PTR(lm_grp_priv);
}

/**
 * determine if should send sample RPC. if true, *tgt_psr will contain the
 * target of the sample RPC
 * The decision logic is:
 *	if there is sampling RPC in progress, and the caller's opcode is not
 *		CRT_OPC_MEMB_SAMPLE, return false
 *	if there are live PSRs haven't been tried, try them
 *	else if every live PSR has a pending sampling RPC, try them in
 *		round-robin manner
 *	else if no more live PSRs, return false
 *
 */
static bool
should_sample(struct lm_grp_priv_t *lm_grp_priv,
	      struct crt_rpc_priv *rpc_priv,
	      d_rank_t *tgt_psr)
{
	int			 i;
	int			 pending_count = 0;
	int			 live_count;
	struct lm_psr_cand	*psr_cand;
	int			 picked_index = -1;
	bool			 evicted;
	int			 index_first_live = -1;
	int			 index_next_psr = -1;
	bool			 ret = false;


	D_ASSERT(lm_grp_priv != NULL);
	D_ASSERT(tgt_psr != NULL);
	psr_cand = lm_grp_priv->lgp_psr_cand;
	live_count = lm_grp_priv->lgp_num_psr;
	D_RWLOCK_WRLOCK(&lm_grp_priv->lgp_rwlock);
	/* if sampling is in progress, only RPCs with opcode CRT_OPC_MEMB_SAMPLE
	 * should send new sampling RPCs
	 */
	if (rpc_priv->crp_pub.cr_opc != CRT_OPC_MEMB_SAMPLE &&
	    lm_grp_priv->lgp_sampling) {
		RPC_TRACE(DB_TRACE, rpc_priv,
			  "should not resample.\n");
		D_GOTO(out, ret);
	}
	lm_grp_priv->lgp_sampling = true;
	for (i = 0; i < lm_grp_priv->lgp_num_psr; i++) {
		evicted = crt_rank_evicted(lm_grp_priv->lgp_grp,
					   psr_cand[i].pc_rank);
		if (evicted) {
			live_count--;
			continue;
		}
		/* record the smallest live PSR candidate */
		if (index_first_live == -1)
			index_first_live = i;

		/* record the next smallest live PSR after the last tried PSR */
		if (lm_grp_priv->lgp_last_tried_index != -1 &&
		    index_next_psr == -1 &&
		    i > lm_grp_priv->lgp_last_tried_index)
			index_next_psr = i;
		/* pick the first free PSR as sample target */
		if (!psr_cand[i].pc_pending_sample) {
			if (picked_index == -1) {
				*tgt_psr = psr_cand[i].pc_rank;
				picked_index = i;
			}
			continue;
		}
		pending_count++;
	}

	/* picked a live PSRs that hasn't been contacted */
	if (pending_count < live_count) {
		D_ASSERTF(picked_index != -1, "picked_index is -1.\n");
		psr_cand[picked_index].pc_pending_sample = true;
		lm_grp_priv->lgp_last_tried_index = picked_index;
		D_DEBUG(DB_TRACE, "psr rank %d is selected.\n",
			psr_cand[picked_index].pc_rank);
		D_GOTO(out, ret = true);
	}

	D_ASSERT(pending_count == live_count);
	if (live_count == 0)
		D_GOTO(out, ret = false);

	/*
	 * all live PSRs candidates have been contacted, pick the next
	 * live PSR candidate in round-robin manner
	 */
	ret = true;
	picked_index = MAX(index_first_live, index_next_psr);
	D_ASSERTF(picked_index != -1, "picked_index is -1.\n");

	lm_grp_priv->lgp_last_tried_index = picked_index;
	*tgt_psr = psr_cand[picked_index].pc_rank;
	D_DEBUG(DB_TRACE, "psr rank %d is selected.\n", *tgt_psr);

out:
	D_RWLOCK_UNLOCK(&lm_grp_priv->lgp_rwlock);

	return ret;
}

/**
 * To be called whenever an RPC encounters a timeout.
 */
static void
lm_membs_sample(crt_context_t ctx, crt_rpc_t *rpc, void *arg)
{
	crt_group_t			*tgt_grp;
	d_rank_t			 tgt_psr = 0;
	struct lm_grp_priv_t		*lm_grp_priv;
	int				 rc = 0;
	struct crt_rpc_priv		*rpc_priv;

	D_ASSERT(rpc != NULL);
	tgt_grp = rpc->cr_ep.ep_grp;

	/* return if the rpc target is the local primary service group */
	if (tgt_grp == crt_lm_gdata.clg_lm_grp_srv.lgs_grp && crt_is_service())
		return;
	/* In a service process NULL means the local group */
	if (tgt_grp == NULL && crt_is_service())
		return;
	/* retrieve the sample hash table.  */
	D_RWLOCK_RDLOCK(&crt_lm_gdata.clg_rwlock);
	lm_grp_priv = lm_grp_priv_find(tgt_grp);
	D_RWLOCK_UNLOCK(&crt_lm_gdata.clg_rwlock);
	D_ASSERT(lm_grp_priv != NULL);

	rpc_priv = container_of(rpc, struct crt_rpc_priv, crp_pub);
	RPC_TRACE(DB_TRACE, rpc_priv, "\n");
	if (!should_sample(lm_grp_priv, rpc_priv, &tgt_psr))
		return;
	/* start a sample RPC */
	rc = lm_sample_rpc(ctx, lm_grp_priv, tgt_psr);
	if (rc != 0)
		D_ERROR("lm_sample() failed.\n");
}

/**
 * To be called by a service process. This is the RPC handler for requests sent
 * by crt_sample_rpc(). It compares the version number from the client
 * and its own version number. If the client version number is out of date,
 * include the membership list delta in the reply to the client.
 */
void
crt_hdlr_memb_sample(crt_rpc_t *rpc_req)
{
	struct crt_lm_memb_sample_in		*in_data;
	struct crt_lm_memb_sample_out		*out_data;
	d_rank_t				*delta;
	d_rank_list_t				*failed_ranks = NULL;
	uint32_t				 num_delta;
	uint32_t				 curr_ver;
	struct lm_grp_srv_t			*lm_grp_srv;
	int					 rc = 0;

	D_ASSERT(crt_lm_gdata.clg_inited != 0);
	lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;
	D_RWLOCK_RDLOCK(&lm_grp_srv->lgs_rwlock);
	curr_ver = lm_grp_srv->lgs_lm_ver;
	D_RWLOCK_UNLOCK(&lm_grp_srv->lgs_rwlock);

	in_data = crt_req_get(rpc_req);
	out_data = crt_reply_get(rpc_req);
	D_DEBUG(DB_TRACE, "client version: %d, server version: %d\n",
		in_data->msi_ver, curr_ver);
	D_ASSERT(in_data->msi_ver <= curr_ver);
	out_data->mso_ver = curr_ver;
	if (in_data->msi_ver == curr_ver) {
		D_DEBUG(DB_TRACE, "client membership list is up-to-date.\n");
		crt_reply_send(rpc_req);
		D_GOTO(out, rc);
	}
	rc = crt_grp_failed_ranks_dup(NULL, &failed_ranks);
	if (rc != 0 || failed_ranks == NULL) {
		D_ERROR("crt_grp_failed_ranks_dup() failed. rc %d\n",
			rc);
		out_data->mso_rc = rc;
		crt_reply_send(rpc_req);
		D_GOTO(out, rc);
	}
	num_delta = failed_ranks->rl_nr - in_data->msi_ver;
	delta = &failed_ranks->rl_ranks[in_data->msi_ver];
	d_iov_set(&out_data->mso_delta, delta, sizeof(d_rank_t) * (num_delta));
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d, opc: %#x.\n",
			rc, rpc_req->cr_opc);
out:
	if (failed_ranks)
		d_rank_list_free(failed_ranks);
}

/*
 * initialize the global lm data
 */
static void
lm_gdata_init(void)
{
	D_INIT_LIST_HEAD(&crt_lm_gdata.clg_grp_remotes);
	crt_lm_gdata.clg_refcount = 0;
	crt_lm_gdata.clg_inited = 1;
	D_RWLOCK_INIT(&crt_lm_gdata.clg_rwlock, NULL);
}

/* destroy the global data for the lm module */
static void
lm_gdata_destroy(void)
{
	struct lm_grp_priv_t	*lm_grp_priv;

	while ((lm_grp_priv = d_list_pop_entry(&crt_lm_gdata.clg_grp_remotes,
					       struct lm_grp_priv_t,
					       lgp_link))) {
		lm_grp_priv_destroy(lm_grp_priv);
	}
	D_RWLOCK_DESTROY(&crt_lm_gdata.clg_rwlock);

	/* allow the same program to re-initialize */
	crt_lm_gdata.clg_refcount = 0;
	crt_lm_gdata.clg_inited = 0;

	return;
}

int
crt_lm_init(void)
{
	crt_group_t			*grp;
	struct	crt_grp_priv		*grp_priv;
	int				 rc = 0;

	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		return -DER_INVAL;
	}
	/* this is the only place we need a grp_priv pointer, since we need to
	 * retrieve the public struct pointer of the local primary group at
	 * initialization.
	 */
	grp_priv = crt_grp_pub2priv(NULL);
	D_ASSERT(grp_priv != NULL);
	grp = &grp_priv->gp_pub;

	pthread_once(&lm_gdata_init_once, lm_gdata_init);
	D_ASSERT(crt_lm_gdata.clg_inited == 1);
	D_RWLOCK_WRLOCK(&crt_lm_gdata.clg_rwlock);
	crt_lm_gdata.clg_refcount++;
	if (crt_lm_gdata.clg_refcount > 1) {
		D_RWLOCK_UNLOCK(&crt_lm_gdata.clg_rwlock);
		return 0;
	}
	/* from here up to the unlock is only executed once per process */
	if (crt_is_service()) {
		rc = crt_lm_grp_init(grp);
		if (rc != 0) {
			D_ERROR("crt_lm_grp_init() failed, rc %d.\n", rc);
			D_GOTO(err_out, rc);
		}
		/* servers register callbacks to manage the liveness map */
		crt_register_progress_cb(lm_prog_cb, grp);
	}
	D_GOTO(out, rc);

err_out:
	crt_lm_gdata.clg_refcount--;
	D_RWLOCK_UNLOCK(&crt_lm_gdata.clg_rwlock);
	lm_gdata_destroy();
	lm_gdata_init_once = PTHREAD_ONCE_INIT;
	return rc;
out:
	D_RWLOCK_UNLOCK(&crt_lm_gdata.clg_rwlock);
	return rc;
}

int
crt_lm_finalize(void)
{
	struct lm_grp_srv_t		*lm_grp_srv;
	int				 rc = 0;

	if (crt_lm_gdata.clg_inited == 0) {
		D_DEBUG(DB_TRACE, "cannot finalize before crt_lm_init().\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	D_RWLOCK_WRLOCK(&crt_lm_gdata.clg_rwlock);
	crt_lm_gdata.clg_refcount--;
	if (crt_lm_gdata.clg_refcount != 0) {
		D_RWLOCK_UNLOCK(&crt_lm_gdata.clg_rwlock);
		D_GOTO(out, rc = 0);
	}
	if (crt_is_service()) {
		lm_grp_srv = &crt_lm_gdata.clg_lm_grp_srv;
		crt_lm_grp_fini(lm_grp_srv);
	}
	D_RWLOCK_UNLOCK(&crt_lm_gdata.clg_rwlock);
	lm_gdata_destroy();
	/**
	 * Reset to allow for crt_lm_gdata to be re-initialized upon multiple
	 * crt_lm_init() calls.
	 */
	lm_gdata_init_once = PTHREAD_ONCE_INIT;

out:
	return rc;
}

int
crt_lm_attach(crt_group_t *tgt_grp, crt_lm_attach_cb_t completion_cb,
	      void *arg)
{
	struct lm_grp_priv_t	*lm_grp_priv_new;
	struct lm_grp_priv_t	*lm_grp_priv;
	int			 rc = DER_SUCCESS;


	D_RWLOCK_RDLOCK(&crt_lm_gdata.clg_rwlock);
	lm_grp_priv = lm_grp_priv_find(tgt_grp);
	D_RWLOCK_UNLOCK(&crt_lm_gdata.clg_rwlock);
	if (lm_grp_priv == NULL) {
		lm_grp_priv_new =
			lm_grp_priv_init(tgt_grp, completion_cb, arg);
		if (lm_grp_priv_new == NULL) {
			D_ERROR("lm_grp_priv_init() failed.\n");
			D_GOTO(out, rc = -DER_NOMEM);
		}
		D_RWLOCK_WRLOCK(&crt_lm_gdata.clg_rwlock);
		lm_grp_priv = lm_grp_priv_find(tgt_grp);
		if (lm_grp_priv == NULL) {
			d_list_add_tail(&lm_grp_priv_new->lgp_link,
				       &crt_lm_gdata.clg_grp_remotes);
			lm_grp_priv = lm_grp_priv_new;
		} else {
			lm_grp_priv_destroy(lm_grp_priv_new);
		}
		D_RWLOCK_UNLOCK(&crt_lm_gdata.clg_rwlock);
	}

out:
	if (rc == DER_SUCCESS) {
		crt_register_timeout_cb(lm_membs_sample, NULL);
	} else {
		struct crt_lm_attach_cb_info cb_info;

		cb_info.lac_arg = arg;
		cb_info.lac_rc = rc;
		completion_cb(&cb_info);
		D_ERROR("crt_lm_attach(%p) failed. rc: %d\n", tgt_grp, rc);
	}

	return rc;
}

int
crt_lm_group_psr(crt_group_t *tgt_grp, d_rank_list_t **psr_cand)
{
	struct lm_grp_priv_t	*lm_grp_priv;
	d_rank_list_t		*new_list = NULL;
	bool			 evicted;
	int			 i;
	int			 rc;

	if (tgt_grp == NULL) {
		D_ERROR("tgt_grp can't be NULL.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (psr_cand == NULL) {
		D_ERROR("psr_cand can't be NULL.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	*psr_cand = NULL;
	if (crt_grp_is_local(tgt_grp)) {
		D_ERROR("tgt_grp can't be a local group.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_RWLOCK_RDLOCK(&crt_lm_gdata.clg_rwlock);
	lm_grp_priv = lm_grp_priv_find(tgt_grp);
	D_RWLOCK_UNLOCK(&crt_lm_gdata.clg_rwlock);
	D_ASSERT(lm_grp_priv != NULL);

	new_list = d_rank_list_alloc(0);
	if (new_list == NULL) {
		D_ERROR("d_rank_list_alloc(0) failed\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	D_RWLOCK_RDLOCK(&lm_grp_priv->lgp_rwlock);
	for (i = 0; i < lm_grp_priv->lgp_num_psr; i++) {
		evicted = crt_rank_evicted(lm_grp_priv->lgp_grp,
				lm_grp_priv->lgp_psr_cand[i].pc_rank);
		if (evicted)
			continue;
		rc = d_rank_list_append(new_list,
					lm_grp_priv->lgp_psr_cand[i].pc_rank);
		if (rc != 0) {
			D_ERROR("d_rank_list_append() failed, rc: %d\n", rc);
			D_RWLOCK_UNLOCK(&lm_grp_priv->lgp_rwlock);
			D_GOTO(out, rc);
		}
	}
	D_RWLOCK_UNLOCK(&lm_grp_priv->lgp_rwlock);
	if (new_list->rl_nr == 0)
		D_GOTO(out, rc = -DER_NONEXIST);

	*psr_cand = new_list;

	return 0;
out:
	if (new_list)
		d_rank_list_free(new_list);

	return rc;
}
