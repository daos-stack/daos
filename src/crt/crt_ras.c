/* Copyright (C) 2016-2017 Intel Corporation
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
 * This file is part of CaRT. It implements the interface with system RAS.
 */

#include <crt_internal.h>

static int
ras_bcast_eviction_event(crt_context_t crt_ctx, struct crt_grp_priv *grp_priv,
			 crt_rank_t pmix_rank);

/*
 * Maintain this processe's copy of the the list of RAS subscribers. This
 * function is called on subscribed RAS nodes only for all eviction events and
 * checks to see if the newly evicted node was a RAS subscriber, removing it
 * from the list if it was.
 */
static int
ras_update_sbscbd_rank_list(struct crt_grp_priv *grp_priv, crt_rank_t rank)
{
	uint32_t		 old_num;
	int			 rc = 0;

	if (grp_priv->gp_pri_srv->ps_ras != 1)
		return rc;
	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	old_num = grp_priv->gp_pri_srv->ps_ras_ranks->rl_nr.num;
	if (old_num == 0) {
		C_ERROR("Rank %d. There should be at least one rank subscribed"
			" to RAS.\n", grp_priv->gp_self);
		C_GOTO(out, rc = -CER_NO_RAS_RANK);
	}
	rc = crt_rank_list_del(grp_priv->gp_pri_srv->ps_ras_ranks, rank);
	if (rc != 0) {
		C_ERROR("crt_rank_list_del() failed, rc: %d.\n", rc);
		C_GOTO(out, rc);
	}
out:
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);
	return rc;
}

/* insert the entry to the failed rank list */
static inline int
ras_add_rank_entry(struct crt_grp_priv *grp_priv, crt_rank_t rank)
{
	uint32_t		 old_num;
	crt_rank_list_t		*new_rank_list;
	int			 rc = 0;

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	old_num = grp_priv->gp_pri_srv->ps_failed_ranks->rl_nr.num;
	new_rank_list =
		crt_rank_list_realloc(grp_priv->gp_pri_srv->ps_failed_ranks,
				      old_num + 1);
	if (new_rank_list == NULL) {
		C_ERROR("crt_rank_list_realloc() failed.\n");
		C_GOTO(out, rc = -CER_NOMEM);
	}
	grp_priv->gp_pri_srv->ps_failed_ranks = new_rank_list;
	grp_priv->gp_pri_srv->ps_failed_ranks->rl_ranks[old_num] = rank;
	C_ASSERT(grp_priv->gp_pri_srv->ps_failed_ranks->rl_nr.num <
		 grp_priv->gp_size);
out:
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

	crt_barrier_handle_eviction(grp_priv);

	return rc;
}

static inline bool
ras_am_i_ras_mgr(struct crt_grp_priv *grp_priv)
{
	bool	 rc = false;

	C_ASSERT(grp_priv->gp_pri_srv->ps_ras_ranks->rl_nr.num > 0);
	pthread_rwlock_rdlock(&grp_priv->gp_rwlock);
	rc = (grp_priv->gp_self
	      == grp_priv->gp_pri_srv->ps_ras_ranks->rl_ranks[0])
		? true : false;
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

	return rc;
}

/**
 * Mark rank as evicted in the lookup hash table. If the rank is not in the hash
 * table yet, insert it then mark evicted. This function is idempotent, meaning
 * if this function is called with the same rank for multiple times, effectively
 * only the first call takes effect.
 *
 * \param rank [in]	cart rank
 */
static int
ras_mark_evicted_in_ht(struct crt_grp_priv *grp_priv, crt_rank_t rank)
{
	int			 ctx_idx;
	crt_list_t		*rlink;
	struct crt_lookup_item	*li;
	struct chash_table	*htable;
	int			 rc = 0;

	for (ctx_idx = 0; ctx_idx < CRT_SRV_CONTEXT_NUM; ctx_idx++) {
		htable = grp_priv->gp_lookup_cache[ctx_idx];
		rlink = chash_rec_find(htable,
				       (void *) &rank, sizeof(rank));
		if (rlink == NULL) {
			C_ALLOC_PTR(li);
			if (li == NULL)
				C_GOTO(out, rc = -CER_NOMEM);
			CRT_INIT_LIST_HEAD(&li->li_link);
			li->li_grp_priv = grp_priv;
			li->li_rank = rank;
			li->li_base_phy_addr =
				strndup("evicted", sizeof("evicted"));
			li->li_initialized = 1;
			li->li_evicted = 1;
			pthread_mutex_init(&li->li_mutex, NULL);
			rc = chash_rec_insert(htable, &rank, sizeof(rank),
					      &li->li_link, true);
			if (rc != 0) {
				crt_li_destroy(li);
				C_ERROR("chash_rec_insert() failed, rc: %d.\n",
					rc);
				C_GOTO(out, rc);
			}
			rlink = &li->li_link;
		} else {
			li = crt_li_link2ptr(rlink);
			C_ASSERT(li->li_grp_priv == grp_priv);
			C_ASSERT(li->li_rank == rank);
			pthread_mutex_lock(&li->li_mutex);
			li->li_evicted = 1;
			pthread_mutex_unlock(&li->li_mutex);
			chash_rec_decref(htable, rlink);
		}
	}

out:
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
	uint32_t			 pmix_rank;
	uint32_t			 tmp_idx;
	struct crt_rank_evict_in	*evict_in;
	struct crt_rank_evict_out	*reply_result;
	struct crt_grp_gdata		*grp_gdata;
	struct crt_grp_priv		*grp_priv;
	struct crt_grp_priv_pri_srv	*pri_srv;
	crt_context_t			 crt_ctx;
	int				 rc = 0;

	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL);
	grp_priv = grp_gdata->gg_srv_pri_grp;
	C_ASSERT(grp_priv != NULL);

	num = (uintptr_t) cb_info->cci_arg;
	crt_ctx = cb_info->cci_rpc->cr_ctx;
	rc = cb_info->cci_rc;
	if (rc != 0) {
		C_ERROR("RPC error, rc: %d.\n", rc);
		C_GOTO(out, rc = -CER_CORPC_INCOMPLETE);
	}
	reply_result = crt_reply_get(cb_info->cci_rpc);
	if (reply_result->creo_succeeded != grp_priv->gp_size - num) {
		C_ERROR("rank: %d eviction request broadcast failed. "
			"Sent to %d targets, succeeded on %d targets\n",
			grp_priv->gp_self, grp_priv->gp_size - num,
			reply_result->creo_succeeded);
		evict_in = crt_req_get(cb_info->cci_rpc);
		pmix_rank = evict_in->crei_rank;
		ras_bcast_eviction_event(crt_ctx, grp_priv, pmix_rank);
		C_GOTO(out, rc = -CER_CORPC_INCOMPLETE);
	} else {
		pri_srv = grp_priv->gp_pri_srv;
		pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
		C_ASSERT(pri_srv->ps_ras_bcast_idx <=
			 pri_srv->ps_failed_ranks->rl_nr.num);
		if (pri_srv->ps_ras_bcast_idx <
				pri_srv->ps_failed_ranks->rl_nr.num) {
			tmp_idx = pri_srv->ps_ras_bcast_idx++;
			pmix_rank = pri_srv->ps_failed_ranks->rl_ranks[tmp_idx];
			pri_srv->ps_ras_bcast_in_prog = 1;
			pthread_rwlock_unlock(&grp_priv->gp_rwlock);
			rc = ras_bcast_eviction_event(crt_ctx, grp_priv,
						      pmix_rank);
			if (rc != 0)
				C_ERROR("ras_bcast_eviction_event() failed. "
					"rank %d\n", grp_priv->gp_self);
		} else {
			pri_srv->ps_ras_bcast_in_prog = 0;
			pthread_rwlock_unlock(&grp_priv->gp_rwlock);
		}
	}

out:
	return rc;
}

static int
pmix2logical(struct crt_rank_map *rank_map, crt_rank_list_t *ranklist)
{
	uint32_t	i;
	int		rc = 0;

	if (!rank_map || !ranklist)
		C_GOTO(out, rc = -CER_INVAL);

	for (i = 0; i < ranklist->rl_nr.num; i++)
		ranklist->rl_ranks[i] = rank_map[ranklist->rl_ranks[i]].rm_rank;

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
ras_bcast_eviction_event(crt_context_t crt_ctx, struct crt_grp_priv *grp_priv,
			 crt_rank_t pmix_rank)
{
	crt_rpc_t			*evict_corpc;
	struct crt_rank_evict_in	*evict_in;
	crt_rank_list_t			*excluded_ranks;
	uint32_t			 num;
	int				 rc = 0;

	pthread_rwlock_rdlock(&grp_priv->gp_rwlock);
	rc = crt_rank_list_dup(&excluded_ranks,
			       grp_priv->gp_pri_srv->ps_failed_ranks, true);
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);
	if (rc != 0) {
		C_ERROR("crt_rank_list_dup() failed. rank %d\n",
				grp_priv->gp_self);
		C_GOTO(out, rc);
	}
	rc = pmix2logical(grp_priv->gp_rank_map, excluded_ranks);
	if (rc != 0) {
		C_ERROR("pmix2logical() failed. rank %d\n", grp_priv->gp_self);
		C_GOTO(out, rc);
	}
	excluded_ranks = crt_rank_list_realloc(excluded_ranks,
					       excluded_ranks->rl_nr.num + 1);
	if (excluded_ranks == NULL) {
		C_ERROR("crt_rank_list_realloc() failed.\n");
		C_GOTO(out, rc = -CER_NOMEM);
	}
	excluded_ranks->rl_ranks[excluded_ranks->rl_nr.num - 1] =
		grp_priv->gp_self;
	rc = crt_corpc_req_create(crt_ctx,
			&grp_priv->gp_pub,
			excluded_ranks,
			CRT_OPC_RANK_EVICT, NULL, NULL, 0,
			crt_tree_topo(CRT_TREE_KNOMIAL, 4),
			&evict_corpc);
	if (rc != 0) {
		C_ERROR("crt_corpc_req_create() failed, rc: %d.\n", rc);
		C_GOTO(out, rc);
	}
	evict_in = crt_req_get(evict_corpc);
	evict_in->crei_rank = pmix_rank;
	num = excluded_ranks->rl_nr.num;
	rc = crt_req_send(evict_corpc, evict_corpc_cb,
			  (void *) (uintptr_t) num);
	crt_rank_list_free(excluded_ranks);
	C_DEBUG("ras event broadcast sent, initiator rank %d\n",
			grp_priv->gp_self);
out:
	return rc;
}

/*
 * update the membership list
 *
 * \param rank [IN]     the cart rank of the failed process
 *
 * \return              0 on success, error codes on failure
 */
static inline int
ras_update_membs(struct crt_grp_priv *grp_priv, crt_rank_t rank)
{
	int              rc = 0;

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	grp_priv->gp_membs_ver++;

	pthread_rwlock_unlock(&grp_priv->gp_rwlock);
	C_DEBUG("rank %d, membership list generation number changed from %d to "
		"%d.\n", grp_priv->gp_self,
		grp_priv->gp_membs_ver - 1, grp_priv->gp_membs_ver);

	return rc;
}

/*
 * This function is called on all RAS subscribers. This routine appends the pmix
 * rank of the failed process to the tail of the list of failed processes. This
 * routine also modifies the liveness map to indicate the current liveness of
 * the pmix rank. This function is idempotent, i.e. when called multiple times
 * with the same pmix rank, only the first call takes effect.
 */
void
crt_ras_event_hdlr_internal(crt_rank_t pmix_rank)
{
	struct crt_grp_gdata		*grp_gdata;
	struct crt_pmix_gdata		*pmix_gdata;
	struct crt_grp_priv		*grp_priv;
	struct crt_rank_map		*rank_map;
	int				 rc = 0;

	if (!crt_initialized()) {
		C_ERROR("CRT not initialized.\n");
		C_GOTO(out, rc);
	}
	if (!crt_is_service()) {
		C_ERROR("Should only be called by a service process.\n");
		C_GOTO(out, rc);
	}
	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL && grp_gdata->gg_pmix_inited == 1);
	pmix_gdata = grp_gdata->gg_pmix;
	C_ASSERT(pmix_gdata != NULL && pmix_gdata->pg_univ_size > 0);
	grp_priv = grp_gdata->gg_srv_pri_grp;
	C_ASSERT(grp_priv != NULL);

	C_DEBUG("internal handler: "
		"rank %d got one PMIx notification, source->rank: %d.\n",
		grp_priv->gp_self, pmix_rank);
	if (pmix_rank >= pmix_gdata->pg_univ_size) {
		C_ERROR("pmix rank %d out of range [0, %d].\n",
			pmix_rank, pmix_gdata->pg_univ_size - 1);
		C_GOTO(out, rc);
	}

	rank_map = &grp_priv->gp_rank_map[pmix_rank];
	if (rank_map->rm_status != CRT_RANK_NOENT) {
		if (rank_map->rm_status == CRT_RANK_ALIVE) {
			rank_map->rm_status = CRT_RANK_DEAD;
			C_WARN("group %s, mark rank %d as dead",
			       grp_priv->gp_pub.cg_grpid, rank_map->rm_rank);
			rc = ras_update_sbscbd_rank_list(grp_priv,
							 rank_map->rm_rank);
			if (rc != 0) {
				C_ERROR("rank %d, ras_update_subscbd_rank_list "
					"failed.\n", grp_priv->gp_self);
				C_GOTO(out, rc);
			}
			rc = ras_add_rank_entry(grp_priv, pmix_rank);
			if (rc != 0) {
				C_ERROR("rank %d, ras_add_rank_entry failed\n",
					grp_priv->gp_self);
				C_GOTO(out, rc);
			}
			ras_mark_evicted_in_ht(grp_priv, rank_map->rm_rank);
			ras_update_membs(grp_priv, rank_map->rm_rank);
		} else {
			C_ASSERT(rank_map->rm_status == CRT_RANK_DEAD);
			C_ERROR("group %s, rank %d already dead.\n",
				grp_priv->gp_pub.cg_grpid, rank_map->rm_rank);
		}
	} else {
		C_DEBUG("PMIx rank %d not belong to group %s, ignore it.\n",
			pmix_rank, grp_priv->gp_pub.cg_grpid);
	}

out:
	return;
}

static bool
crt_ras_bcast_in_progress(void)
{
	struct crt_grp_gdata		*grp_gdata;
	struct crt_grp_priv		*grp_priv;
	int				 rc;

	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL && grp_gdata->gg_pmix_inited == 1);
	grp_priv = grp_gdata->gg_srv_pri_grp;
	C_ASSERT(grp_priv != NULL);

	rc = grp_priv->gp_pri_srv->ps_ras_bcast_in_prog;

	return rc;
}

void
crt_drain_eviction_requests_kickoff(crt_context_t crt_ctx)
{
	struct crt_grp_gdata		*grp_gdata;
	struct crt_grp_priv		*grp_priv;
	struct crt_grp_priv_pri_srv	*pri_srv;
	uint32_t			 tmp_idx;
	uint32_t			 pmix_rank;
	int				 rc = 0;

	if (!crt_initialized()) {
		C_ERROR("CRT not initialized.\n");
		C_GOTO(out, rc);
	}
	if (!crt_is_service())
		C_GOTO(out, rc);
	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL && grp_gdata->gg_pmix_inited == 1);
	grp_priv = grp_gdata->gg_srv_pri_grp;
	C_ASSERT(grp_priv != NULL);
	pri_srv = grp_priv->gp_pri_srv;

	if (!ras_am_i_ras_mgr(grp_priv))
		C_GOTO(out, rc);
	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	/* return if bcast already in progress */
	if (crt_ras_bcast_in_progress()) {
		pthread_rwlock_unlock(&grp_priv->gp_rwlock);
		C_GOTO(out, rc);
	}
	/* start the bcast if there are pending requests */
	C_ASSERT(pri_srv->ps_ras_bcast_idx <=
		 pri_srv->ps_failed_ranks->rl_nr.num);
	if (pri_srv->ps_ras_bcast_idx <
	    pri_srv->ps_failed_ranks->rl_nr.num) {
		tmp_idx = pri_srv->ps_ras_bcast_idx++;
		pmix_rank = pri_srv->ps_failed_ranks->rl_ranks[tmp_idx];
		pri_srv->ps_ras_bcast_in_prog = 1;
		pthread_rwlock_unlock(&grp_priv->gp_rwlock);
		rc = ras_bcast_eviction_event(crt_ctx, grp_priv, pmix_rank);
		if (rc != 0)
			C_ERROR("ras_bcast_eviction_event() failed. rank %d\n",
				grp_priv->gp_self);
	} else {
		pthread_rwlock_unlock(&grp_priv->gp_rwlock);
	}
out:
	return;
}

/* this function is called by the fake event utility thread */
void
crt_fake_event_notify_fn(crt_rank_t pmix_rank)
{
	struct crt_grp_gdata		*grp_gdata;
	struct crt_grp_priv		*grp_priv;
	int				 rc = 0;

	if (!crt_initialized()) {
		C_ERROR("CRT not initialized.\n");
		C_GOTO(out, rc);
		return;
	}
	if (!crt_is_service())
		C_GOTO(out, rc);
	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL && grp_gdata->gg_pmix_inited == 1);
	grp_priv = grp_gdata->gg_srv_pri_grp;
	C_ASSERT(grp_priv != NULL);

	if (grp_priv->gp_pri_srv->ps_ras == 0)
		C_GOTO(out, rc);
	crt_ras_event_hdlr_internal(pmix_rank);

out:
	return;
}

int
crt_hdlr_rank_evict(crt_rpc_t *rpc_req)
{
	struct crt_rank_evict_in	*in_data;
	struct crt_rank_evict_out	*out_data;
	struct crt_grp_gdata		*grp_gdata;
	struct crt_pmix_gdata		*pmix_gdata;
	struct crt_grp_priv		*grp_priv;
	struct crt_rank_map		*rank_map;
	crt_rank_t			 pmix_rank;
	int				 rc = 0;

	in_data = crt_req_get(rpc_req);
	out_data = crt_reply_get(rpc_req);
	pmix_rank = in_data->crei_rank;

	if (!crt_initialized()) {
		C_ERROR("CRT not initialized.\n");
		C_GOTO(out, rc = -CER_UNINIT);
	}
	if (!crt_is_service()) {
		C_ERROR("Should only be called by a service process.\n");
		C_GOTO(out, rc = -CER_OOG);
	}
	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL);
	pmix_gdata = grp_gdata->gg_pmix;
	C_ASSERT(grp_gdata != NULL);
	grp_priv = grp_gdata->gg_srv_pri_grp;
	C_ASSERT(grp_priv != NULL);
	if (pmix_rank >= pmix_gdata->pg_univ_size) {
		C_ERROR("pmix rank %d out of range [0, %d].\n",
			pmix_rank, pmix_gdata->pg_univ_size - 1);
		rc = -CER_OOG;
		goto out;
	}
	C_DEBUG("Rank %d received relayed RAS notification "
		"regarding to pmix rank %d\n", grp_priv->gp_self, pmix_rank);
/*
 * TODO: handle possible race condition when the RAS bcast msg arrives before
 * the local RAS event notification
 */
	if (grp_priv->gp_pri_srv->ps_ras) {
		uint32_t tmp_idx = grp_priv->gp_pri_srv->ps_ras_bcast_idx;

		if (pmix_rank ==
		    grp_priv->gp_pri_srv->ps_failed_ranks->rl_ranks[tmp_idx])
			grp_priv->gp_pri_srv->ps_ras_bcast_idx++;
		else
			C_ERROR("eviction requests received out of order.\n");
		goto out;
	}
	rank_map = &grp_priv->gp_rank_map[pmix_rank];
	if (rank_map->rm_status != CRT_RANK_NOENT) {
		if (rank_map->rm_status == CRT_RANK_ALIVE) {
			rank_map->rm_status = CRT_RANK_DEAD;
			C_WARN("group %s, mark rank %d as dead",
					grp_priv->gp_pub.cg_grpid,
					rank_map->rm_rank);
			rc = ras_update_sbscbd_rank_list(grp_priv,
							 rank_map->rm_rank);
			if (rc != 0) {
				C_ERROR("rank %d, ras_update_subscbd_rank_list "
					"failed.\n", grp_priv->gp_self);
				goto out;
			}
			rc = ras_add_rank_entry(grp_priv, pmix_rank);
			if (rc != 0) {
				C_ERROR("rank %d, ras_add_rank_entry failed\n",
					grp_priv->gp_self);
				goto out;
			}
			ras_mark_evicted_in_ht(grp_priv, rank_map->rm_rank);
			ras_update_membs(grp_priv, rank_map->rm_rank);
		} else {
			C_DEBUG("group %s, rank %d already dead.\n",
				grp_priv->gp_pub.cg_grpid, rank_map->rm_rank);
			goto out;
		}
	} else {
		C_DEBUG("PMIx rank %d not belong to group %s, ignore it.\n",
				pmix_rank, grp_priv->gp_pub.cg_grpid);
		rc = -CER_OOG;
	}

out:
	out_data->creo_rc = rc;
	out_data->creo_succeeded = 1;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		C_ERROR("crt_reply_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_req->cr_opc);
	return rc;
}

int
crt_evict_rank(crt_group_t *grp, int version, crt_rank_t rank)
{
	uint32_t		 old_num;
	crt_rank_list_t		*new_rank_list;
	struct crt_grp_priv	*grp_priv = NULL;
	int			 rc = 0;

	if (grp == NULL) {
		C_ERROR("Invalid argument: group pointer is NULL\n");
		return -CER_INVAL;
	}

	grp_priv = container_of(grp, struct crt_grp_priv, gp_pub);

	if (rank < 0 || rank >= grp_priv->gp_size) {
		C_ERROR("Rank out of range. Attempted rank: %d, "
			"valid range [0, %d].\n", rank, grp_priv->gp_size);
		return -CER_OOG;
	}

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	if (version <= grp_priv->gp_membs_ver) {
		C_ERROR("Attemped version should be larger than actual Version."
			" Actual version: %d, attempted version: "
			"%d", grp_priv->gp_membs_ver, version);
		C_GOTO(out, rc = -CER_MISMATCH);
	}

	old_num = grp_priv->gp_pri_srv->ps_failed_ranks->rl_nr.num;
	new_rank_list =
		crt_rank_list_realloc(grp_priv->gp_pri_srv->ps_failed_ranks,
				      old_num + 1);
	if (new_rank_list == NULL) {
		C_ERROR("crt_rank_list_realloc() failed.\n");
		C_GOTO(out, rc = -CER_NOMEM);
	}
	grp_priv->gp_pri_srv->ps_failed_ranks = new_rank_list;
	grp_priv->gp_pri_srv->ps_failed_ranks->rl_ranks[old_num] = rank;
	C_ASSERT(grp_priv->gp_pri_srv->ps_failed_ranks->rl_nr.num <
		 grp_priv->gp_size);
	grp_priv->gp_membs_ver = version;

	pthread_rwlock_unlock(&grp_priv->gp_rwlock);
	C_DEBUG("rank %d, membership list generation number changed from %d to "
		"%d.\n", grp_priv->gp_self,
		grp_priv->gp_membs_ver - 1, grp_priv->gp_membs_ver);

	return rc;
out:
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

	return rc;
}
