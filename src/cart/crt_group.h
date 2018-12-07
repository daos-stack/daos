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


#define RANK_LIST_REALLOC_SIZE 32
#define CRT_NO_RANK 0xFFFFFFFF

/* Node for keeping info about free index */
struct free_index {
	/* Index to store */
	uint32_t	fi_index;
	/* Link to next element */
	d_list_t	fi_link;
};

/* Structure for keeping track of group membership
 * This structure is temporary until dynamic group support is added
 * for secondary groups as well. When that happens structure could
 * get simplified
 */
struct crt_grp_membs {
	/* list of free indices unused yet. Only used when pmix is disabled */
	d_list_t	cgm_free_indices;
	/* list of members */
	d_rank_list_t	*cgm_list;
	/* linear list of members. Only used when pmix is disabled */
	d_rank_list_t	*cgm_linear_list;
};


struct crt_grp_priv {
	d_list_t		 gp_link; /* link to crt_grp_list */
	crt_group_t		 gp_pub; /* public grp handle */
	/*
	 * member ranks, should be unique and sorted, each member is the rank
	 * number within the primary group.
	 */
	struct crt_grp_membs	gp_membs;
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
	struct d_hash_table	 *gp_lookup_cache;
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
	struct crt_rank_map	*gp_pmix_rank_map;
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


#define CRT_PMIX_ENABLED() \
	(crt_gdata.cg_pmix_disabled == 0)

/* TODO: Once secondary group support is implemented we will converge on
 * using a single structure for membership lists
 */
static inline d_rank_list_t*
grp_priv_get_membs(struct crt_grp_priv *priv)
{
	if (CRT_PMIX_ENABLED())
		return priv->gp_membs.cgm_list;

	return priv->gp_membs.cgm_linear_list;
}

static inline d_rank_list_t*
grp_priv_get_live_ranks(struct crt_grp_priv *priv)
{
	/* When pmix is disabled, member list == live rank list  */
	if (CRT_PMIX_ENABLED())
		return priv->gp_live_ranks;

	return priv->gp_membs.cgm_linear_list;
}

static inline d_rank_list_t*
grp_priv_get_failed_ranks(struct crt_grp_priv *priv)
{
	return priv->gp_failed_ranks;
}

static inline d_rank_t
grp_priv_get_primary_rank(struct crt_grp_priv *priv, d_rank_t rank)
{
	if (priv->gp_primary)
		return rank;

	if (CRT_PMIX_ENABLED()) {
		D_ASSERT(rank < priv->gp_membs.cgm_list->rl_nr);

		return priv->gp_membs.cgm_list->rl_ranks[rank];
	}

	D_ASSERT(rank < priv->gp_membs.cgm_linear_list->rl_nr);

	/* NO PMIX, secondary group */
	return priv->gp_membs.cgm_linear_list->rl_ranks[rank];
}

/*
 * This call is currently called only when group is created.
 * For PMIX enabled case, a list of members is provided and needs
 * to be copied over to the membership list.
 *
 * For PMIX disabled case, the list should be NULL.
 */
static inline int
grp_priv_set_membs(struct crt_grp_priv *priv, d_rank_list_t *list)
{
	if (CRT_PMIX_ENABLED())
		return d_rank_list_dup_sort_uniq(&priv->gp_membs.cgm_list,
						list);

	/* PMIX disabled case */
	if (!priv->gp_primary) {
		/* For secondary groups we populate linear list */
		return d_rank_list_dup_sort_uniq(&priv->gp_membs.cgm_linear_list,
						list);
	}

	/* This case should be called with list == NULL */
	D_ASSERT(list == NULL);

	return 0;

}

static inline int
grp_priv_copy_live_ranks(struct crt_grp_priv *priv, d_rank_list_t *list)
{
	return d_rank_list_dup(&priv->gp_live_ranks, list);
}

static inline int
grp_priv_init_failed_ranks(struct crt_grp_priv *priv)
{
	int rc = 0;

	priv->gp_failed_ranks = d_rank_list_alloc(0);
	if (priv->gp_failed_ranks == NULL) {
		D_ERROR("d_rank_list_alloc failed.\n");
		rc = -DER_NOMEM;
	}

	return rc;
}

static inline int
grp_priv_init_membs(struct crt_grp_priv *priv, int size)
{
	priv->gp_membs.cgm_list = d_rank_list_alloc(size);

	if (!priv->gp_membs.cgm_list)
		return -DER_NOMEM;

	if (CRT_PMIX_ENABLED())
		return 0;

	D_INIT_LIST_HEAD(&priv->gp_membs.cgm_free_indices);
	priv->gp_membs.cgm_linear_list = d_rank_list_alloc(0);

	return 0;
}

static inline void
grp_priv_fini_live_ranks(struct crt_grp_priv *priv)
{
	d_rank_list_free(priv->gp_live_ranks);
}

static inline void
grp_priv_fini_failed_ranks(struct crt_grp_priv *priv)
{
	d_rank_list_free(priv->gp_failed_ranks);
}

static inline void
grp_priv_fini_membs(struct crt_grp_priv *priv)
{
	struct free_index	*index;

	if (priv->gp_membs.cgm_list != NULL)
		d_rank_list_free(priv->gp_membs.cgm_list);

	if (priv->gp_membs.cgm_linear_list != NULL)
		d_rank_list_free(priv->gp_membs.cgm_linear_list);

	if (CRT_PMIX_ENABLED())
		return;

	/* Secondary groups have no free indices list */
	if (!priv->gp_primary)
		return;

	/* With PMIX disabled free index list needs to be freed */
	while ((index = d_list_pop_entry(&priv->gp_membs.cgm_free_indices,
				struct free_index, fi_link)) != NULL) {
		D_FREE(index);
	}
}

static inline void
grp_priv_set_default_failed_ranks(struct crt_grp_priv *priv,
				struct crt_grp_priv *def)
{
	priv->gp_failed_ranks = def->gp_failed_ranks;
}

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
	crt_phy_addr_t		 li_uri[CRT_SRV_CONTEXT_NUM];
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
int crt_grp_lc_lookup(struct crt_grp_priv *grp_priv, int ctx_idx,
		      d_rank_t rank, uint32_t tag, crt_phy_addr_t *base_addr,
		      hg_addr_t *hg_addr);
int crt_grp_lc_uri_insert(struct crt_grp_priv *grp_priv, int ctx_idx,
			  d_rank_t rank, uint32_t tag, const char *uri);
int crt_grp_lc_addr_insert(struct crt_grp_priv *grp_priv,
			   struct crt_context *ctx_idx,
			   d_rank_t rank, uint32_t tag, hg_addr_t *hg_addr);
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

	D_DEBUG(DB_TRACE, "crt_get_subgrp_id get subgrp_id: "DF_X64".\n",
		subgrp_id);

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

	D_DEBUG(DB_TRACE, "service group (%s), refcount increased to %d.\n",
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
		D_DEBUG(DB_TRACE, "group (%s), refcount already dropped "
			"to 0.\n", grp_priv->gp_pub.cg_grpid);
		D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
		D_GOTO(out, rc = -DER_ALREADY);
	} else {
		grp_priv->gp_refcount--;
		D_DEBUG(DB_TRACE, "service group (%s), refcount decreased to "
			"%d.\n", grp_priv->gp_pub.cg_grpid,
			grp_priv->gp_refcount);
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
			D_DEBUG(DB_TRACE, "reset grp_gdata->gg_srv_pri_grp "
				"as NULL.\n");
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
	D_DEBUG(DB_TRACE, "group %s, set psr rank %d, uri %s.\n",
		grp_priv->gp_pub.cg_grpid, psr_rank, psr_addr);
}

bool
crt_grp_id_identical(crt_group_id_t grp_id_1, crt_group_id_t grp_id_2);
bool crt_grp_is_local(crt_group_t *grp);
struct crt_grp_priv *crt_grp_pub2priv(crt_group_t *grp);
int crt_grp_lc_uri_insert_all(crt_group_t *grp, d_rank_t rank, int tag,
			const char *uri);
bool crt_rank_evicted(crt_group_t *grp, d_rank_t rank);
int crt_grp_config_psr_load(struct crt_grp_priv *grp_priv, d_rank_t psr_rank);
int crt_grp_psr_reload(struct crt_grp_priv *grp_priv);
int crt_grp_create_corpc_aggregate(crt_rpc_t *source, crt_rpc_t *result,
				   void *priv);
int crt_grp_destroy_corpc_aggregate(crt_rpc_t *source, crt_rpc_t *result,
				    void *priv);

#endif /* __CRT_GROUP_H__ */
