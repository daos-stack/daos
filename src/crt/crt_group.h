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
 * This file is part of CaRT. It gives out the internal data structure of group.
 */

#ifndef __CRT_GROUP_H__
#define __CRT_GROUP_H__

#include <crt_pmix.h>

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
	crt_rank_t		rm_rank; /* rank in primary group */
	enum crt_rank_status	rm_status; /* health status */
};

/*
 * Fields only valid in primary service groups, protected by
 * crt_grp_priv::gp_rwlock.
 */
struct crt_grp_priv_pri_srv {
	/*
	 * Minimum Viable Size for the group. If the number of live ranks is
	 * less than MVS, the group should shut down
	 */
	uint32_t		 ps_mvs;
	/* flag for ranks subscribed to RAS events */
	uint32_t		 ps_ras:1,
	/* flag for RAS bcast in progress */
				 ps_ras_bcast_in_prog:1,
	/* flag for RAS related variables */
				 ps_ras_initialized:1;
	/*
	 * index of next failed rank to broadcast. only meaninful on ras nodes
	 * (nodes subscribed to RAS)
	 */
	uint32_t		 ps_ras_bcast_idx;
	/* ranks subscribed to RAS events*/
	crt_rank_list_t		*ps_ras_ranks;
	/* failed PMIx ranks */
	crt_rank_list_t		*ps_failed_ranks;
};

/* (1 << CRT_LOOKUP_CACHE_BITS) is the number of buckets of lookup hash table */
#define CRT_LOOKUP_CACHE_BITS	(4)

struct crt_grp_priv {
	crt_list_t		 gp_link; /* link to crt_grp_list */
	crt_group_t		 gp_pub; /* public grp handle */
	/*
	 * member ranks, should be unique and sorted, each member is the rank
	 * number within the primary group.
	 */
	crt_rank_list_t		*gp_membs;
	/*
	 * the version number of membership list gp_membs, also the version
	 * number of the failed rank list gp_pri_srv->ps_failed_ranks
	 */
	uint32_t		 gp_membs_ver;
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
	crt_rank_t		 gp_self;
	/* PSR rank in attached group */
	crt_rank_t		 gp_psr_rank;
	/* PSR phy addr address in attached group */
	crt_phy_addr_t		 gp_psr_phy_addr;
	/* address lookup cache, only valid for primary group */
	struct chash_table     **gp_lookup_cache;
	enum crt_grp_status	 gp_status; /* group status */
	/* set of variables only valid in primary service groups */
	struct crt_grp_priv_pri_srv
				*gp_pri_srv;
	uint32_t		 gp_primary:1, /* flag of primary group */
	/* flag of local group, false means attached remote group */
				 gp_local:1,
	/* flag of service group */
				 gp_service:1,
	/* flag of finalizing/destroying */
				 gp_finalizing:1;
	/* group reference count now only used for attach/detach. */
	uint32_t		 gp_refcount;

	/* rank map array, only needed for local primary group */
	struct crt_rank_map	*gp_rank_map;
	/* pmix errhdlr ref, used for PMIx_Deregister_event_handler */
	size_t			 gp_errhdlr_ref;

	/* TODO: reuse crt_corpc_info here */
	/*
	 * Some temporary info used for group creating/destroying, valid when
	 * gp_status is CRT_GRP_CREATING or CRT_GRP_DESTROYING.
	 */
	struct crt_rpc_priv	*gp_parent_rpc; /* parent RPC, NULL on root */
	crt_list_t		 gp_child_rpcs; /* child RPCs list */
	uint32_t		 gp_child_num;
	uint32_t		 gp_child_ack_num;
	int			 gp_rc; /* temporary recoded return code */

	crt_grp_create_cb_t	 gp_create_cb; /* grp create completion cb */
	crt_grp_destroy_cb_t	 gp_destroy_cb; /* grp destroy completion cb */
	void			*gp_destroy_cb_arg;

	pthread_rwlock_t	gp_rwlock; /* protect all fields above */
};

/* lookup cache item for one target */
struct crt_lookup_item {
	/* link to crt_grp_priv::gp_lookup_cache[ctx_idx] */
	crt_list_t		 li_link;
	/* point back to grp_priv */
	struct crt_grp_priv	*li_grp_priv;
	/* rank of the target */
	crt_rank_t		 li_rank;
	/* base phy addr published through PMIx */
	crt_phy_addr_t		 li_base_phy_addr;
	/* connected HG addr */
	na_addr_t		 li_tag_addr[CRT_SRV_CONTEXT_NUM];
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
	crt_list_t		 gg_cli_grps_attached;
	/* server side group list attached to */
	crt_list_t		 gg_srv_grps_attached;

	/* TODO: move crt_grp_list here */
	/* sub-grp list, only meaningful for server */
	crt_list_t		 gg_sub_grps;
	/* some flags */
	uint32_t		 gg_inited:1, /* all initialized */
				 gg_pmix_inited:1; /* PMIx initialized */
	/* rwlock to protect above fields */
	pthread_rwlock_t	 gg_rwlock;
};

int crt_hdlr_grp_create(crt_rpc_t *rpc_req);
int crt_hdlr_grp_destroy(crt_rpc_t *rpc_req);
int crt_hdlr_uri_lookup(crt_rpc_t *rpc_req);
int crt_grp_attach(crt_group_id_t srv_grpid, crt_group_t **attached_grp);
int crt_grp_detach(crt_group_t *attached_grp);
int crt_grp_uri_lookup(struct crt_grp_priv *grp_priv, crt_rank_t rank,
		       char **uri);
int crt_grp_lc_lookup(struct crt_grp_priv *grp_priv, int ctx_idx,
		      struct crt_hg_context *hg_ctx, crt_rank_t rank,
		      uint32_t tag, crt_phy_addr_t *base_addr,
		      na_addr_t *na_addr);
struct crt_grp_priv *crt_grp_lookup_int_grpid(uint64_t int_grpid);
int crt_validate_grpid(const crt_group_id_t grpid);
int crt_grp_init(crt_group_id_t grpid);
int crt_grp_fini(void);

#define CRT_ALLOW_SINGLETON_ENV		"CRT_ALLOW_SINGLETON"
int crt_grp_save_attach_info(struct crt_grp_priv *grp_priv);
int crt_grp_load_attach_info(struct crt_grp_priv *grp_priv);

/* some simple helpers */

static inline bool
crt_ep_identical(crt_endpoint_t *ep1, crt_endpoint_t *ep2)
{
	C_ASSERT(ep1 != NULL);
	C_ASSERT(ep2 != NULL);
	/* TODO: check group */
	if (ep1->ep_rank == ep2->ep_rank)
		return true;
	else
		return false;
}

static inline void
crt_ep_copy(crt_endpoint_t *dst_ep, crt_endpoint_t *src_ep)
{
	C_ASSERT(dst_ep != NULL);
	C_ASSERT(src_ep != NULL);
	/* TODO: copy grp id */
	dst_ep->ep_rank = src_ep->ep_rank;
	dst_ep->ep_tag = src_ep->ep_tag;
}

void crt_li_destroy(struct crt_lookup_item *li);

struct crt_lookup_item *crt_li_link2ptr(crt_list_t *rlink);

static inline uint64_t
crt_get_subgrp_id()
{
	struct crt_grp_priv	*grp_priv;
	uint64_t		 subgrp_id;

	C_ASSERT(crt_initialized());
	C_ASSERT(crt_is_service());
	grp_priv = crt_gdata.cg_grp->gg_srv_pri_grp;
	C_ASSERT(grp_priv != NULL);
	C_ASSERT(grp_priv->gp_primary && grp_priv->gp_local);

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	subgrp_id = grp_priv->gp_int_grpid + grp_priv->gp_subgrp_idx;
	grp_priv->gp_subgrp_idx++;
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

	C_DEBUG("crt_get_subgrp_id get subgrp_id: "CF_X64".\n", subgrp_id);

	return subgrp_id;
}

static inline void
crt_grp_priv_addref(struct crt_grp_priv *grp_priv)
{
	uint32_t	refcount;

	C_ASSERT(grp_priv != NULL);
	C_ASSERT(grp_priv->gp_primary == 1);

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	refcount = ++grp_priv->gp_refcount;
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

	C_DEBUG("service group (%s), refcount increased to %d.\n",
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
	int			 rc;

	grp_gdata = crt_gdata.cg_grp;
	C_ASSERT(grp_gdata != NULL);
	C_ASSERT(grp_priv != NULL);
	C_ASSERT(grp_priv->gp_primary == 1);

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	if (grp_priv->gp_refcount == 0) {
		rc = -CER_ALREADY;
	} else {
		grp_priv->gp_refcount--;
		rc = grp_priv->gp_refcount;
		if (rc == 0)
			grp_priv->gp_finalizing = 1;
	}
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

	if (rc >= 0)
		C_DEBUG("service group (%s), refcount decreased to %d.\n",
			grp_priv->gp_pub.cg_grpid, rc);
	else
		C_ERROR("service group (%s), refcount already dropped to 0.\n",
			grp_priv->gp_pub.cg_grpid);

	return rc;
}

#endif /* __CRT_GROUP_H__ */
