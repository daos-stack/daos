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
 */
/**
 * This file is part of CaRT. It gives out the internal data structure of group.
 */

#ifndef __CRT_GROUP_H__
#define __CRT_GROUP_H__

#include "crt_barrier.h"
#include "crt_pmix.h"

enum crt_grp_status {
	CRT_GRP_CREATING = 0x66,
	CRT_GRP_NORMAL,
	CRT_GRP_DESTROYING,
};

enum crt_rank_status {
	CRT_RANK_NOENT = 0x87, /* rank non-existed for this primary group */
	CRT_RANK_ALIVE, /* rank alive */
	CRT_RANK_DEAD, /* rank dead */
};

/* the index of crt_rank_map[] is the PMIx global rank */
struct crt_rank_map {
	d_rank_t		rm_rank; /* rank in primary group */
	enum crt_rank_status	rm_status; /* health status */
};

/* (1 << CRT_LOOKUP_CACHE_BITS) is the number of buckets of lookup hash table */
#define CRT_LOOKUP_CACHE_BITS	(4)

struct crt_grp_priv {
	d_list_t		 gp_link; /* link to crt_grp_list */
	crt_group_t		 gp_pub; /* public grp handle */
	/*
	 * member ranks, should be unique and sorted, each member is the rank
	 * number within the primary group.
	 */
	d_rank_list_t		*gp_membs;
	/*
	 * the version number of membership list gp_membs, also the version
	 * number of the failed rank list gp_pri_srv->ps_failed_ranks
	 */
	uint32_t		 gp_membs_ver;
	/*
	 * member ranks that are still alive, should be unique and sorted, each
	 * member is the rank number within the primary group. Only valid for
	 * the local primary service group.
	 */
	d_rank_list_t		*gp_live_ranks;
	/* failed ranks. a subgroup's list points to its parent's list */
	d_rank_list_t		*gp_failed_ranks;
	/*
	 * protects gp_membs_ver, gp_live_ranks, gp_failed_ranks. Only allocated
	 * for primary groups, a subgroup references its parent group's lock
	 */
	pthread_rwlock_t	*gp_rwlock_ft;
	/* the priv pointer user passed in for crt_group_create */
	void			*gp_priv;
	/* CaRT context only for sending sub-grp create/destroy RPCs */
	crt_context_t		 gp_ctx;

	/*
	 * internal group ID, it is (gp_self << 32) for primary group,
	 * and plus with the gp_subgrp_idx as the internal sub-group ID.
	 */
	uint64_t		 gp_int_grpid;
	/*
	 * subgrp index within the primary group, it bumps up one for every
	 * sub-group creation.
	 */
	uint32_t		 gp_subgrp_idx;
	/* size (number of membs) of group */
	uint32_t		 gp_size;
	/*
	 * logical self rank in this group, only valid for local group.
	 * the gp_membs->rl_ranks[gp_self] is its rank number in primary group.
	 * For primary group, gp_self == gp_membs->rl_ranks[gp_self].
	 */
	d_rank_t		 gp_self;
	/* PSR rank in attached group */
	d_rank_t		 gp_psr_rank;
	/* PSR phy addr address in attached group */
	crt_phy_addr_t		 gp_psr_phy_addr;
	/* address lookup cache, only valid for primary group */
	struct d_hash_table	 **gp_lookup_cache;
	enum crt_grp_status	 gp_status; /* group status */
	/* set of variables only valid in primary service groups */
	uint32_t		 gp_primary:1, /* flag of primary group */
	/* flag of local group, false means attached remote group */
				 gp_local:1,
	/* flag of service group */
				 gp_service:1,
	/* flag of finalizing/destroying */
				 gp_finalizing:1;
	/* group reference count */
	uint32_t		 gp_refcount;

	/* rank map array, only needed for local primary group */
	struct crt_rank_map	*gp_rank_map;
	/* pmix errhdlr ref, used for PMIx_Deregister_event_handler */
	size_t			 gp_errhdlr_ref;
	/* Barrier information.  Only used in local service groups */
	struct crt_barrier_info	 gp_barrier_info;

	/* temporary return code for group creation */
	int			 gp_rc;

	crt_grp_create_cb_t	 gp_create_cb; /* grp create completion cb */
	crt_grp_destroy_cb_t	 gp_destroy_cb; /* grp destroy completion cb */
	void			*gp_destroy_cb_arg;

	pthread_rwlock_t	 gp_rwlock; /* protect all fields above */
};

/* lookup cache item for one target */
struct crt_lookup_item {
	/* link to crt_grp_priv::gp_lookup_cache[ctx_idx] */
	d_list_t		 li_link;
	/* point back to grp_priv */
	struct crt_grp_priv	*li_grp_priv;
	/* rank of the target */
	d_rank_t		 li_rank;
	/* base phy addr published through PMIx */
	crt_phy_addr_t		 li_base_phy_addr;
	/* connected HG addr */
	hg_addr_t		 li_tag_addr[CRT_SRV_CONTEXT_NUM];
	/* reference count */
	uint32_t		 li_ref;
	uint32_t		 li_initialized:1,
				 li_evicted:1;
	pthread_mutex_t		 li_mutex;
};

/* structure of global group data */
struct crt_grp_gdata {
	/* PMIx related global data */
	struct crt_pmix_gdata	*gg_pmix;

	/* client-side primary group, only meaningful for client */
	struct crt_grp_priv	*gg_cli_pri_grp;
	/* server-side primary group */
	struct crt_grp_priv	*gg_srv_pri_grp;

	/* client side group list attached by, only meaningful for server */
	d_list_t		 gg_cli_grps_attached;
	/* server side group list attached to */
	d_list_t		 gg_srv_grps_attached;

	/* TODO: move crt_grp_list here */
	/* sub-grp list, only meaningful for server */
	d_list_t		 gg_sub_grps;
	/* some flags */
	uint32_t		 gg_inited:1, /* all initialized */
				 gg_pmix_inited:1; /* PMIx initialized */
	/* rwlock to protect above fields */
	pthread_rwlock_t	 gg_rwlock;
};

void crt_hdlr_grp_create(crt_rpc_t *rpc_req);
void crt_hdlr_grp_destroy(crt_rpc_t *rpc_req);
void crt_hdlr_uri_lookup(crt_rpc_t *rpc_req);
int crt_grp_attach(crt_group_id_t srv_grpid, crt_group_t **attached_grp);
int crt_grp_detach(crt_group_t *attached_grp);
char *crt_get_tag_uri(const char *base_uri, int tag);
int crt_grp_lc_lookup(struct crt_grp_priv *grp_priv, int ctx_idx,
		      d_rank_t rank, uint32_t tag, crt_phy_addr_t *base_addr,
		      hg_addr_t *hg_addr);
int crt_grp_lc_uri_insert(struct crt_grp_priv *grp_priv, int ctx_idx,
			  d_rank_t rank, const char *uri);
int crt_grp_lc_addr_insert(struct crt_grp_priv *grp_priv,
			   struct crt_context *ctx_idx,
			   d_rank_t rank, int tag, hg_addr_t *hg_addr);
int crt_grp_ctx_invalid(struct crt_context *ctx, bool locked);
struct crt_grp_priv *crt_grp_lookup_int_grpid(uint64_t int_grpid);
int crt_validate_grpid(const crt_group_id_t grpid);
int crt_grp_init(crt_group_id_t grpid);
int crt_grp_fini(void);
int crt_grp_failed_ranks_dup(crt_group_t *grp, d_rank_list_t **failed_ranks);
void crt_grp_priv_destroy(struct crt_grp_priv *grp_priv);

int crt_grp_config_load(struct crt_grp_priv *grp_priv);

static inline bool crt_grp_is_subgrp_id(uint64_t grp_id)
{
	/* Primary group has lower 32 bits set to 0x0 */
	return ((grp_id & 0xFFFFFFFF) == 0) ? false : true;
}

/* some simple helpers */

static inline bool
crt_ep_identical(crt_endpoint_t *ep1, crt_endpoint_t *ep2)
{
	D_ASSERT(ep1 != NULL);
	D_ASSERT(ep2 != NULL);
	/* TODO: check group */
	if (ep1->ep_rank == ep2->ep_rank)
		return true;
	else
		return false;
}

static inline void
crt_ep_copy(crt_endpoint_t *dst_ep, crt_endpoint_t *src_ep)
{
	D_ASSERT(dst_ep != NULL);
	D_ASSERT(src_ep != NULL);
	/* TODO: copy grp id */
	dst_ep->ep_rank = src_ep->ep_rank;
	dst_ep->ep_tag = src_ep->ep_tag;
}

void crt_li_destroy(struct crt_lookup_item *li);

struct crt_lookup_item *crt_li_link2ptr(d_list_t *rlink);

static inline uint64_t
crt_get_subgrp_id()
{
	struct crt_grp_priv	*grp_priv;
	uint64_t		 subgrp_id;

	D_ASSERT(crt_initialized());
	D_ASSERT(crt_is_service());
	grp_priv = crt_gdata.cg_grp->gg_srv_pri_grp;
	D_ASSERT(grp_priv != NULL);
	D_ASSERT(grp_priv->gp_primary && grp_priv->gp_local);

	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	subgrp_id = grp_priv->gp_int_grpid + grp_priv->gp_subgrp_idx;
	grp_priv->gp_subgrp_idx++;
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	D_DEBUG("crt_get_subgrp_id get subgrp_id: "DF_X64".\n", subgrp_id);

	return subgrp_id;
}

static inline void
crt_grp_priv_addref(struct crt_grp_priv *grp_priv)
{
	uint32_t	refcount;

	D_ASSERT(grp_priv != NULL);

	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	refcount = ++grp_priv->gp_refcount;
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	d_log(DD_FAC(grp) | DLOG_DBG,
	      "service group (%s), refcount increased to %d.\n",
	      grp_priv->gp_pub.cg_grpid, refcount);
}

/*
 * Decrease the attach refcount and return the result.
 * Returns negative value for error case.
 */
static inline int
crt_grp_priv_decref(struct crt_grp_priv *grp_priv)
{
	struct crt_grp_gdata	*grp_gdata;
	bool			 detach = false;
	int			 rc = 0;

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);
	D_ASSERT(grp_priv != NULL);

	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	if (grp_priv->gp_refcount == 0) {
		d_log(DD_FAC(grp) | DLOG_ERR,
		      "group (%s), refcount already dropped to 0.\n",
		      grp_priv->gp_pub.cg_grpid);
		D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
		D_GOTO(out, rc = -DER_ALREADY);
	} else {
		grp_priv->gp_refcount--;
		d_log(DD_FAC(grp) | DLOG_DBG,
		      "service group (%s), refcount decreased to %d.\n",
		      grp_priv->gp_pub.cg_grpid, grp_priv->gp_refcount);
		if (grp_priv->gp_refcount == 0)
			grp_priv->gp_finalizing = 1;
		if (grp_priv->gp_local == 0 && grp_priv->gp_refcount == 1) {
			detach = true;
			grp_priv->gp_finalizing = 1;
		}
	}
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	if (grp_priv->gp_finalizing == 0)
		D_GOTO(out, rc);

	if (detach) {
		D_ASSERT(grp_priv->gp_service == 1 &&
			 grp_priv->gp_primary == 1);
		rc = crt_grp_detach(&grp_priv->gp_pub);
		if (rc == 0 && !crt_is_service() &&
		    grp_gdata->gg_srv_pri_grp == grp_priv) {
			grp_gdata->gg_srv_pri_grp = NULL;
			d_log(DD_FAC(grp) | DLOG_DBG,
			      "reset grp_gdata->gg_srv_pri_grp as NULL.\n");
		}
	} else {
		crt_grp_priv_destroy(grp_priv);
	}

out:
	return rc;
}

static inline void
crt_grp_psr_set(struct crt_grp_priv *grp_priv, d_rank_t psr_rank,
		crt_phy_addr_t psr_addr)
{
	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	D_FREE(grp_priv->gp_psr_phy_addr);
	grp_priv->gp_psr_rank = psr_rank;
	grp_priv->gp_psr_phy_addr = psr_addr;
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
	d_log(DD_FAC(grp) | DLOG_DBG, "group %s, set psr rank %d, uri %s.\n",
		grp_priv->gp_pub.cg_grpid, psr_rank, psr_addr);
}

bool
crt_grp_id_identical(crt_group_id_t grp_id_1, crt_group_id_t grp_id_2);
bool crt_grp_is_local(crt_group_t *grp);
struct crt_grp_priv *crt_grp_pub2priv(crt_group_t *grp);
int crt_grp_lc_uri_insert_all(crt_group_t *grp, d_rank_t rank, const char *uri);
bool crt_rank_evicted(crt_group_t *grp, d_rank_t rank);
int crt_grp_config_psr_load(struct crt_grp_priv *grp_priv, d_rank_t psr_rank);
int crt_grp_psr_reload(struct crt_grp_priv *grp_priv);
int crt_grp_create_corpc_aggregate(crt_rpc_t *source, crt_rpc_t *result,
				   void *priv);
int crt_grp_destroy_corpc_aggregate(crt_rpc_t *source, crt_rpc_t *result,
				    void *priv);

#endif /* __CRT_GROUP_H__ */
