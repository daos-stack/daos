/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It gives out the internal data structure of group.
 */

#ifndef __CRT_GROUP_H__
#define __CRT_GROUP_H__

#include <gurt/atomic.h>
#include "crt_swim.h"

/* (1 << CRT_LOOKUP_CACHE_BITS) is the number of buckets of lookup hash table */
#define CRT_LOOKUP_CACHE_BITS	(4)

#define RANK_LIST_REALLOC_SIZE 32

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

struct crt_grp_priv_sec {
	struct crt_grp_priv	*gps_priv;
	d_list_t		gps_link;
};

struct crt_grp_priv;

struct crt_grp_priv {
	d_list_t		 gp_link; /* link to crt_grp_list */
	crt_group_t		 gp_pub; /* public grp handle */

	/* Link to a primary group; only set for secondary groups  */
	struct crt_grp_priv	*gp_priv_prim;

	/* List of secondary groups associated with this group */
	d_list_t		gp_sec_list;

	/*
	 * member ranks, should be unique and sorted, each member is the rank
	 * number within the primary group.
	 */
	struct crt_grp_membs	gp_membs;
	/*
	 * the version number of the group. Set my crt_group_version_set or
	 * crt_group_mod APIs.
	 */
	uint32_t		 gp_membs_ver;
	/*
	 * The minimum version of the group. This is set by crt_rank_self_set
	 * to the version in which we join system.
	 */
	uint32_t		 gp_membs_ver_min;
	/*
	 * this structure contains the circular list of member ranks.
	 * It's used to store SWIM related information and should strictly
	 * correspond to members in gp_membs.
	 */
	struct crt_swim_membs	 gp_membs_swim;

	/* size (number of membs) of group */
	uint32_t		 gp_size;
	/*
	 * logical self rank in this group, only valid for local group.
	 * the gp_membs->rl_ranks[gp_self] is its rank number in primary group.
	 * For primary group, gp_self == gp_membs->rl_ranks[gp_self].
	 * If gp_self is CRT_NO_RANK, it usually means the group version is not
	 * up to date.
	 */
	d_rank_t		 gp_self;
	/* List of PSR ranks */
	d_rank_list_t		 *gp_psr_ranks;
	/* PSR rank in attached group */
	d_rank_t		 gp_psr_rank;
	/* PSR phy addr address in attached group */
	crt_phy_addr_t		 gp_psr_phy_addr;
	/* address lookup cache, only valid for primary group */
	struct d_hash_table	 *gp_lookup_cache;

	/* uri lookup cache, only valid for primary group */
	struct d_hash_table	 gp_uri_lookup_cache;

	/* Primary to secondary rank mapping table */
	struct d_hash_table	 gp_p2s_table;

	/* Secondary to primary rank mapping table */
	struct d_hash_table	 gp_s2p_table;

	/* set of variables only valid in primary service groups */
	uint32_t		 gp_primary:1, /* flag of primary group */
				 gp_view:1, /* flag to indicate it is a view */
				/* Auto remove rank from secondary group */
				 gp_auto_remove:1;

	/* group reference count */
	uint32_t		 gp_refcount;

	pthread_rwlock_t	 gp_rwlock; /* protect all fields above */
};

static inline d_rank_list_t*
grp_priv_get_membs(struct crt_grp_priv *priv)
{
	return priv->gp_membs.cgm_linear_list;
}

d_rank_t
crt_grp_priv_get_primary_rank(struct crt_grp_priv *priv, d_rank_t rank);

/*
 * This call is currently called only when group is created.
 */
static inline int
grp_priv_set_membs(struct crt_grp_priv *priv, d_rank_list_t *list)
{
	if (!priv->gp_primary) {
		/* For secondary groups we populate linear list */
		return d_rank_list_dup_sort_uniq(
					&priv->gp_membs.cgm_linear_list,
					list);
	}

	/* This case should be called with list == NULL */
	D_ASSERT(list == NULL);

	return 0;
}

static inline int
grp_priv_init_membs(struct crt_grp_priv *priv, int size)
{
	D_INIT_LIST_HEAD(&priv->gp_membs.cgm_free_indices);
	priv->gp_membs.cgm_list = d_rank_list_alloc(size);

	if (!priv->gp_membs.cgm_list)
		return -DER_NOMEM;

	priv->gp_membs.cgm_linear_list = d_rank_list_alloc(0);
	if (!priv->gp_membs.cgm_linear_list)
		return -DER_NOMEM;

	return 0;
}

static inline void
grp_priv_fini_membs(struct crt_grp_priv *priv)
{
	struct free_index	*index;

	if (priv->gp_membs.cgm_list != NULL)
		d_rank_list_free(priv->gp_membs.cgm_list);

	if (priv->gp_membs.cgm_linear_list != NULL)
		d_rank_list_free(priv->gp_membs.cgm_linear_list);

	/* With PMIX disabled free index list needs to be freed */
	while ((index = d_list_pop_entry(&priv->gp_membs.cgm_free_indices,
				struct free_index, fi_link)) != NULL) {
		D_FREE(index);
	}
}

struct crt_rank_mapping {
	d_list_t	rm_link;
	d_rank_t	rm_key;
	d_rank_t	rm_value;

	ATOMIC uint32_t	rm_ref;
	uint32_t	rm_initialized:1;
};

/* uri info for each remote rank */
struct crt_uri_item {
	/* link to crt_grp_priv::gp_uri_lookup_cache */
	d_list_t	ui_link;

	/* URI string for each remote tag */
	/* TODO: in phase2 change this to hash table */
	ATOMIC crt_phy_addr_t ui_uri[CRT_SRV_CONTEXT_NUM];

	/* Primary rank; for secondary groups only  */
	d_rank_t	ui_pri_rank;

	/* remote rank; used as a hash key */
	d_rank_t	ui_rank;

	/* reference count */
	ATOMIC uint32_t	ui_ref;

	/* flag indicating whether initialized */
	uint32_t	ui_initialized:1;
};

/* lookup cache item for one target */
struct crt_lookup_item {
	/* link to crt_grp_priv::gp_lookup_cache[ctx_idx] */
	d_list_t		 li_link;
	/* point back to grp_priv */
	struct crt_grp_priv	*li_grp_priv;
	/* rank of the target */
	d_rank_t		 li_rank;
	/* connected HG addr */
	hg_addr_t		 li_tag_addr[CRT_SRV_CONTEXT_NUM];

	/* reference count */
	ATOMIC uint32_t		 li_ref;
	uint32_t		 li_initialized:1;
	pthread_mutex_t		 li_mutex;
};

/* structure of global group data */
struct crt_grp_gdata {
	struct crt_grp_priv	*gg_primary_grp;

	/* rwlock to protect above fields */
	pthread_rwlock_t	 gg_rwlock;
};

void crt_hdlr_uri_lookup(crt_rpc_t *rpc_req);
int crt_grp_detach(crt_group_t *attached_grp);
void crt_grp_lc_lookup(struct crt_grp_priv *grp_priv, int ctx_idx,
		      d_rank_t rank, uint32_t tag, crt_phy_addr_t *base_addr,
		      hg_addr_t *hg_addr);
int crt_grp_lc_uri_insert(struct crt_grp_priv *grp_priv,
			  d_rank_t rank, uint32_t tag, const char *uri);
int crt_grp_lc_addr_insert(struct crt_grp_priv *grp_priv,
			   struct crt_context *ctx_idx,
			   d_rank_t rank, uint32_t tag, hg_addr_t *hg_addr);
int crt_grp_ctx_invalid(struct crt_context *ctx, bool locked);
struct crt_grp_priv *crt_grp_lookup_int_grpid(uint64_t int_grpid);
struct crt_grp_priv *crt_grp_lookup_grpid(crt_group_id_t grp_id);
int crt_validate_grpid(const crt_group_id_t grpid);
int crt_grp_init(crt_group_id_t grpid);
void crt_grp_fini(void);
void crt_grp_priv_destroy(struct crt_grp_priv *grp_priv);

int crt_grp_config_load(struct crt_grp_priv *grp_priv);

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

struct crt_uri_item *crt_ui_link2ptr(d_list_t *rlink);

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
static inline void
crt_grp_priv_decref(struct crt_grp_priv *grp_priv)
{
	bool	destroy = false;

	D_ASSERT(grp_priv != NULL);

	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	D_ASSERT(grp_priv->gp_refcount >= 1);
	grp_priv->gp_refcount--;
	D_DEBUG(DB_TRACE, "group (%s), decref to %d.\n",
		grp_priv->gp_pub.cg_grpid, grp_priv->gp_refcount);
	if (grp_priv->gp_refcount == 0)
		destroy = true;

	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	if (destroy)
		crt_grp_priv_destroy(grp_priv);
}

static inline int
crt_grp_psr_set(struct crt_grp_priv *grp_priv, d_rank_t psr_rank,
		crt_phy_addr_t psr_addr, bool steal)
{
	int rc = 0;

	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	D_FREE(grp_priv->gp_psr_phy_addr);
	grp_priv->gp_psr_rank = psr_rank;
	if (steal) {
		grp_priv->gp_psr_phy_addr = psr_addr;
	} else {
		D_STRNDUP(grp_priv->gp_psr_phy_addr, psr_addr,
			CRT_ADDR_STR_MAX_LEN);
		if (grp_priv->gp_psr_phy_addr == NULL)
			rc = -DER_NOMEM;
	}
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
	D_DEBUG(DB_TRACE, "group %s, set psr rank %d, uri %s.\n",
		grp_priv->gp_pub.cg_grpid, psr_rank, psr_addr);
	return rc;
}

struct crt_grp_priv *crt_grp_pub2priv(crt_group_t *grp);

static inline bool
crt_rank_present(crt_group_t *grp, d_rank_t rank)
{
	struct crt_grp_priv	*priv = NULL;
	d_rank_list_t		*membs;
	bool			ret = false;

	priv = crt_grp_pub2priv(grp);

	D_ASSERTF(priv != NULL, "group priv is NULL\n");

	D_RWLOCK_RDLOCK(&priv->gp_rwlock);
	membs = grp_priv_get_membs(priv);
	if (membs)
		ret = d_rank_in_rank_list(membs, rank);
	D_RWLOCK_UNLOCK(&priv->gp_rwlock);

	return ret;
}

#define CRT_RANK_PRESENT(grp, rank) \
	crt_rank_present(grp, rank)

bool
crt_grp_id_identical(crt_group_id_t grp_id_1, crt_group_id_t grp_id_2);
int crt_grp_config_psr_load(struct crt_grp_priv *grp_priv, d_rank_t psr_rank);
int crt_grp_psr_reload(struct crt_grp_priv *grp_priv);

int
grp_add_to_membs_list(struct crt_grp_priv *grp_priv, d_rank_t rank, uint64_t incarnation);

#endif /* __CRT_GROUP_H__ */
