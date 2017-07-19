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
 * This file is part of CaRT. It implements the main group APIs.
 */
#define D_LOGFAC	DD_FAC(grp)

#include "crt_internal.h"
#include <sys/stat.h>

/* global CRT group list */
D_LIST_HEAD(crt_grp_list);
/* protect global group list */
pthread_rwlock_t crt_grp_list_rwlock = PTHREAD_RWLOCK_INITIALIZER;

struct crt_lookup_item *
crt_li_link2ptr(d_list_t *rlink)
{
	D_ASSERT(rlink != NULL);
	return container_of(rlink, struct crt_lookup_item, li_link);
}

static int
li_op_key_get(struct d_chash_table *hhtab, d_list_t *rlink, void **key_pp)
{
	struct crt_lookup_item *li = crt_li_link2ptr(rlink);

	*key_pp = (void *)&li->li_rank;
	return sizeof(li->li_rank);
}

static uint32_t
li_op_key_hash(struct d_chash_table *hhtab, const void *key, unsigned int ksize)
{
	D_ASSERT(ksize == sizeof(d_rank_t));

	return (unsigned int)(*(const uint32_t *)key %
		(1U << CRT_LOOKUP_CACHE_BITS));
}

static bool
li_op_key_cmp(struct d_chash_table *hhtab, d_list_t *rlink,
	      const void *key, unsigned int ksize)
{
	struct crt_lookup_item *li = crt_li_link2ptr(rlink);

	D_ASSERT(ksize == sizeof(d_rank_t));

	return li->li_rank == *(d_rank_t *)key;
}

static void
li_op_rec_addref(struct d_chash_table *hhtab, d_list_t *rlink)
{
	struct crt_lookup_item *li = crt_li_link2ptr(rlink);

	D_ASSERT(li->li_initialized);
	pthread_mutex_lock(&li->li_mutex);
	li->li_ref++;
	pthread_mutex_unlock(&li->li_mutex);
}

static bool
li_op_rec_decref(struct d_chash_table *hhtab, d_list_t *rlink)
{
	uint32_t			 ref;
	struct crt_lookup_item		*li = crt_li_link2ptr(rlink);

	D_ASSERT(li->li_initialized);
	pthread_mutex_lock(&li->li_mutex);
	li->li_ref--;
	ref = li->li_ref;
	pthread_mutex_unlock(&li->li_mutex);

	return ref == 0;
}

static void
li_op_rec_free(struct d_chash_table *hhtab, d_list_t *rlink)
{
	crt_li_destroy(crt_li_link2ptr(rlink));
}

static d_chash_table_ops_t lookup_table_ops = {
	.hop_key_get		= li_op_key_get,
	.hop_key_hash		= li_op_key_hash,
	.hop_key_cmp		= li_op_key_cmp,
	.hop_rec_addref		= li_op_rec_addref,
	.hop_rec_decref		= li_op_rec_decref,
	.hop_rec_free		= li_op_rec_free,
};

void
crt_li_destroy(struct crt_lookup_item *li)
{
	int	i;

	D_ASSERT(li != NULL);
	D_ASSERT(li->li_ref == 0);
	D_ASSERT(li->li_initialized == 1);

	if (li->li_base_phy_addr != NULL) {
		free(li->li_base_phy_addr);
		li->li_base_phy_addr = NULL;
	}

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		if (li->li_tag_addr[i] != NULL)
			D_ERROR("tag %d, li_tag_addr not freed.\n", i);
	}

	pthread_mutex_destroy(&li->li_mutex);

	D_FREE_PTR(li);
}

static int
crt_grp_lc_create(struct crt_grp_priv *grp_priv)
{
	struct d_chash_table	**htables;
	int			  rc, i;

	D_ASSERT(grp_priv != NULL);
	if (grp_priv->gp_primary == 0) {
		D_ERROR("need not create lookup cache for sub-group.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	D_ALLOC_ARRAY(htables, CRT_SRV_CONTEXT_NUM);
	if (htables == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		rc = d_chash_table_create(D_HASH_FT_NOLOCK,
					  CRT_LOOKUP_CACHE_BITS,
					  NULL, &lookup_table_ops, &htables[i]);
		if (rc != 0) {
			D_ERROR("d_chash_table_create failed, rc: %d.\n", rc);
			D_GOTO(out, rc);
		}
		D_ASSERT(htables[i] != NULL);
	}
	grp_priv->gp_lookup_cache = htables;

out:
	if (rc != 0)
		D_ERROR("crt_grp_lc_create failed, rc: %d.\n", rc);
	return rc;
}

static int
crt_grp_lc_destroy(struct crt_grp_priv *grp_priv)
{
	int	rc, i;

	D_ASSERT(grp_priv != NULL);

	if (grp_priv->gp_lookup_cache == NULL)
		return 0;

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		rc = d_chash_table_destroy(grp_priv->gp_lookup_cache[i],
					 true /* force */);
		if (rc != 0) {
			D_ERROR("d_chash_table_destroy_inplace failed, "
				"rc: %d.\n", rc);
			D_GOTO(out, rc);
		}
	}
	D_FREE(grp_priv->gp_lookup_cache);

out:
	return rc;
}

/*
 * Fill in the base URI of rank in the lookup cache of the crt_ctx.
 */
int
crt_grp_lc_uri_insert(struct crt_grp_priv *grp_priv, int ctx_idx,
		      d_rank_t rank, const char *uri)
{
	d_list_t		*rlink;
	struct crt_lookup_item	*li;
	int			 rc = 0;

	D_ASSERT(ctx_idx >= 0 && ctx_idx < CRT_SRV_CONTEXT_NUM);
	pthread_rwlock_rdlock(&grp_priv->gp_rwlock);
	rlink = d_chash_rec_find(grp_priv->gp_lookup_cache[ctx_idx],
			(void *)&rank, sizeof(rank));
	if (rlink == NULL) {
		pthread_rwlock_unlock(&grp_priv->gp_rwlock);
		/* target rank not in cache */
		D_ALLOC_PTR(li);
		if (li == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		D_INIT_LIST_HEAD(&li->li_link);
		li->li_grp_priv = grp_priv;
		li->li_rank = rank;
		li->li_base_phy_addr = strndup(uri, CRT_ADDR_STR_MAX_LEN);
		if (li->li_base_phy_addr == NULL) {
			D_ERROR("strndup() failed.\n");
			D_GOTO(out, rc = -DER_NOMEM);
		}
		li->li_initialized = 1;
		li->li_evicted = 0;
		pthread_mutex_init(&li->li_mutex, NULL);

		pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
		rc = d_chash_rec_insert(grp_priv->gp_lookup_cache[ctx_idx],
					&rank, sizeof(rank), &li->li_link,
					true /* exclusive */);
		if (rc != 0) {
			D_DEBUG("entry already exists in lookup table, "
				"grp_priv %p ctx_idx %d, rank: %d.\n",
				grp_priv, ctx_idx, rank);
			crt_li_destroy(li);
			rc = 0;
		} else {
			D_DEBUG("Filling in URI in lookup table. "
				" grp_priv %p ctx_idx %d, rank: %d, rlink %p\n",
				grp_priv, ctx_idx, rank, &li->li_link);
		}
		D_GOTO(unlock, rc);
	}
	li = crt_li_link2ptr(rlink);
	D_ASSERT(li->li_grp_priv == grp_priv);
	D_ASSERT(li->li_rank == rank);
	D_ASSERT(li->li_initialized != 0);
	pthread_mutex_lock(&li->li_mutex);
	if (li->li_base_phy_addr == NULL) {
		li->li_base_phy_addr = strndup(uri, CRT_ADDR_STR_MAX_LEN);
		if (li->li_base_phy_addr == NULL) {
			D_ERROR("strndup() failed.\n");
			rc = -DER_NOMEM;
		}
		D_DEBUG("Filling in URI in lookup table. "
			" grp_priv %p ctx_idx %d, rank: %d, rlink %p\n",
			grp_priv, ctx_idx, rank, &li->li_link);
	} else {
		D_WARN("URI already exists. "
			" grp_priv %p ctx_idx %d, rank: %d, rlink %p\n",
			grp_priv, ctx_idx, rank, &li->li_link);
	}
	pthread_mutex_unlock(&li->li_mutex);
	d_chash_rec_decref(grp_priv->gp_lookup_cache[ctx_idx], rlink);

unlock:
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);
out:
	return rc;
}

/**
 * Fill in the base URI of rank in the lookup cache of all crt_ctx. grp can be
 * NULL
 */
int
crt_grp_lc_uri_insert_all(crt_group_t *grp, d_rank_t rank, const char *uri)
{
	struct crt_grp_priv	*grp_priv;
	int			 i;
	int			 rc = 0;

	grp_priv = crt_grp_pub2priv(grp);

	for (i = 0; i < crt_gdata.cg_ctx_num; i++) {
		rc = crt_grp_lc_uri_insert(grp_priv, i, rank, uri);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_uri_insert(%p, %d, %d, %s) failed."
				" rc: %d\n", grp_priv, i, rank, uri, rc);
			return rc;
		}
	}

	return rc;
}

static int
crt_grp_lc_addr_invalid(d_list_t *rlink, void *arg)
{
	struct crt_lookup_item	*li;
	struct crt_context	*ctx;
	int			 i;
	int			 rc = 0;

	D_ASSERT(rlink != NULL);
	D_ASSERT(arg != NULL);
	li = crt_li_link2ptr(rlink);
	ctx = (struct crt_context *)arg;

	pthread_mutex_lock(&li->li_mutex);
	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		if (li->li_tag_addr[i] == NULL)
			continue;
		rc = crt_hg_addr_free(&ctx->cc_hg_ctx, li->li_tag_addr[i]);
		if (rc != 0) {
			D_ERROR("crt_hg_addr_free failed, ctx_idx %d, tag %d, "
				"rc: %d.\n", ctx->cc_idx, i, rc);
			D_GOTO(out, rc);
		}
		li->li_tag_addr[i] = NULL;
	}

	if (li->li_base_phy_addr != NULL) {
		free(li->li_base_phy_addr);
		li->li_base_phy_addr = NULL;
	}

out:
	pthread_mutex_unlock(&li->li_mutex);
	return rc;
}

/*
 * Invalid all cached hg_addr in group of one context.
 * It should only be called by crt_context_destroy.
 */
static int
crt_grp_lc_ctx_invalid(struct crt_grp_priv *grp_priv, struct crt_context *ctx)
{
	struct d_chash_table	*lc_cache;
	int			 ctx_idx;
	int			 rc = 0;

	D_ASSERT(grp_priv != NULL && grp_priv->gp_primary == 1);
	D_ASSERT(ctx != NULL);
	ctx_idx = ctx->cc_idx;
	D_ASSERT(ctx_idx >= 0 && ctx_idx < CRT_SRV_CONTEXT_NUM);

	lc_cache = grp_priv->gp_lookup_cache[ctx_idx];
	D_ASSERT(lc_cache != NULL);
	rc = d_chash_table_traverse(lc_cache, crt_grp_lc_addr_invalid, ctx);
	if (rc != 0)
		D_ERROR("d_chash_table_traverse failed, ctx_idx %d, rc: %d.\n",
			ctx_idx, rc);

	return rc;
}

/*
 * Invalid context for all groups.
 */
int
crt_grp_ctx_invalid(struct crt_context *ctx, bool locked)
{
	struct crt_grp_priv	*grp_priv = NULL;
	struct crt_grp_gdata	*grp_gdata;
	int			 rc = 0;

	D_ASSERT(crt_initialized());
	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);
	D_ASSERT(ctx != NULL);

	if (!locked)
		pthread_rwlock_rdlock(&grp_gdata->gg_rwlock);
	grp_priv = grp_gdata->gg_srv_pri_grp;
	if (grp_priv != NULL) {
		rc = crt_grp_lc_ctx_invalid(grp_priv, ctx);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_ctx_invalid failed, group %s, "
				"ctx_idx: %d, rc: %d.\n",
				grp_priv->gp_pub.cg_grpid, ctx->cc_idx, rc);
			D_GOTO(out, rc);
		}
	}

	d_list_for_each_entry(grp_priv, &grp_gdata->gg_srv_grps_attached,
			      gp_link) {
		rc = crt_grp_lc_ctx_invalid(grp_priv, ctx);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_ctx_invalid failed, group %s, "
				"ctx_idx: %d, rc: %d.\n",
				grp_priv->gp_pub.cg_grpid, ctx->cc_idx, rc);
			break;
		}
	}

out:
	if (!locked)
		pthread_rwlock_unlock(&grp_gdata->gg_rwlock);
	return rc;
}

/*
 * Fill in the hg address  of a tag in the lookup cache of crt_ctx. The host
 * rank where the tag resides in must exist in the cache before calling this
 * routine.
 */
int
crt_grp_lc_addr_insert(struct crt_grp_priv *grp_priv,
		       struct crt_context *crt_ctx,
		       d_rank_t rank, int tag, hg_addr_t *hg_addr)
{
	d_list_t		*rlink;
	struct crt_lookup_item	*li;
	int			 ctx_idx;
	int			 rc = 0;

	D_ASSERT(crt_ctx != NULL);
	ctx_idx = crt_ctx->cc_idx;
	pthread_rwlock_rdlock(&grp_priv->gp_rwlock);
	rlink = d_chash_rec_find(grp_priv->gp_lookup_cache[ctx_idx],
				 (void *)&rank, sizeof(rank));
	D_ASSERT(rlink != NULL);
	li = crt_li_link2ptr(rlink);
	D_ASSERT(li->li_grp_priv == grp_priv);
	D_ASSERT(li->li_rank == rank);
	D_ASSERT(li->li_initialized != 0);

	pthread_mutex_lock(&li->li_mutex);
	if (li->li_evicted == 1)
		rc = -DER_EVICTED;
	if (li->li_tag_addr[tag] == NULL) {
		li->li_tag_addr[tag] = *hg_addr;
	} else {
		D_WARN("NA address already exits. "
		       " grp_priv %p ctx_idx %d, rank: %d, tag %d, rlink %p\n",
		       grp_priv, ctx_idx, rank, tag, &li->li_link);
		rc = crt_hg_addr_free(&crt_ctx->cc_hg_ctx, *hg_addr);
		if (rc != 0) {
			D_ERROR("crt_hg_addr_free failed, crt_idx %d, *hg_addr"
				" 0x%p, rc %d\n", ctx_idx, *hg_addr, rc);
			D_GOTO(out, rc);
		}
		*hg_addr = li->li_tag_addr[tag];
	}
out:
	pthread_mutex_unlock(&li->li_mutex);
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);
	d_chash_rec_decref(grp_priv->gp_lookup_cache[ctx_idx], rlink);

	return rc;
}

/*
 * Lookup the URI and NA address of a (rank, tag) combination in the addr cache.
 * This function only looks into the address cache. If the requested (rank, tag)
 * pair doesn't exist in the address cache, *hg_addr will be NULL on return, and
 * an empty record for the requested rank with NULL values will be inserted to
 * the cache. For input parameters, base_addr and hg_addr can not be both NULL.
 * (hg_addr == NULL) means the caller only want to lookup the base_addr.
 * (base_addr == NULL) means the caller only want to lookup the hg_addr.
 */
int
crt_grp_lc_lookup(struct crt_grp_priv *grp_priv, int ctx_idx,
		  d_rank_t rank, uint32_t tag,
		  crt_phy_addr_t *base_addr, hg_addr_t *hg_addr)
{
	struct crt_lookup_item	*li;
	d_list_t		*rlink;
	int			rc = 0;

	D_ASSERT(grp_priv != NULL);
	D_ASSERT(grp_priv->gp_primary != 0);
	D_ASSERT(rank < grp_priv->gp_size);
	D_ASSERT(tag < CRT_SRV_CONTEXT_NUM);
	D_ASSERT(base_addr != NULL || hg_addr != NULL);
	D_ASSERT(ctx_idx >= 0 && ctx_idx < CRT_SRV_CONTEXT_NUM);

	pthread_rwlock_rdlock(&grp_priv->gp_rwlock);
	rlink = d_chash_rec_find(grp_priv->gp_lookup_cache[ctx_idx],
				 (void *)&rank, sizeof(rank));
	if (rlink != NULL) {
		li = crt_li_link2ptr(rlink);
		D_ASSERT(li->li_grp_priv == grp_priv);
		D_ASSERT(li->li_rank == rank);
		D_ASSERT(li->li_initialized != 0);

		pthread_mutex_lock(&li->li_mutex);
		if (li->li_evicted == 1) {
			pthread_mutex_unlock(&li->li_mutex);
			pthread_rwlock_unlock(&grp_priv->gp_rwlock);
			d_chash_rec_decref(grp_priv->gp_lookup_cache[ctx_idx],
					   rlink);
			D_ERROR("tag %d on rank %d already evicted.\n", tag,
				rank);
			D_GOTO(out, rc = -DER_OOG);
		}
		pthread_mutex_unlock(&li->li_mutex);
		if (base_addr != NULL)
			*base_addr = li->li_base_phy_addr;
		if (hg_addr == NULL)
			D_ASSERT(base_addr != NULL);
		else if (li->li_tag_addr[tag] != NULL)
			*hg_addr = li->li_tag_addr[tag];
		pthread_rwlock_unlock(&grp_priv->gp_rwlock);
		d_chash_rec_decref(grp_priv->gp_lookup_cache[ctx_idx], rlink);
		D_GOTO(out, rc);
	}
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

	/* target rank not in cache */
	D_ALLOC_PTR(li);
	if (li == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_INIT_LIST_HEAD(&li->li_link);
	li->li_grp_priv = grp_priv;
	li->li_rank = rank;
	li->li_base_phy_addr = NULL;
	li->li_initialized = 1;
	li->li_evicted = 0;
	pthread_mutex_init(&li->li_mutex, NULL);
	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	rc = d_chash_rec_insert(grp_priv->gp_lookup_cache[ctx_idx], &rank,
			      sizeof(rank), &li->li_link, true /* exclusive */);
	if (rc != 0) {
		D_DEBUG("entry already exists.\n");
		crt_li_destroy(li);
	} else {
		D_DEBUG("Inserted lookup table entry without base URI. "
			"grp_priv %p ctx_idx %d, rank: %d, tag: %d, rlink %p\n",
			grp_priv, ctx_idx, rank, tag, &li->li_link);
	}
	/* the only possible failure is key conflict */
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

out:
	return rc;
}

static inline int
crt_errno2cer(int err)
{
	switch (err) {
	case 0:		return 0;
	case EPERM:
	case EACCES:	return -DER_NO_PERM;
	case ENOMEM:	return -DER_NOMEM;
	case EDQUOT:
	case ENOSPC:	return -DER_NOSPACE;
	case EEXIST:	return -DER_EXIST;
	case ENOENT:	return -DER_NONEXIST;
	case ECANCELED:	return -DER_CANCELED;
	default:	return -DER_MISC;
	}
	return 0;
}

inline bool
crt_grp_id_identical(crt_group_id_t grp_id_1, crt_group_id_t grp_id_2)
{
	D_ASSERT(grp_id_1 != NULL && strlen(grp_id_1) > 0 &&
		 strlen(grp_id_1) < CRT_GROUP_ID_MAX_LEN);
	D_ASSERT(grp_id_2 != NULL && strlen(grp_id_2) > 0 &&
		 strlen(grp_id_2) < CRT_GROUP_ID_MAX_LEN);
	return strcmp(grp_id_1, grp_id_2) == 0;
}

static inline struct crt_grp_priv *
crt_grp_lookup_locked(crt_group_id_t grp_id)
{
	struct crt_grp_priv	*grp_priv;
	bool			found = false;

	d_list_for_each_entry(grp_priv, &crt_grp_list, gp_link) {
		if (crt_grp_id_identical(grp_priv->gp_pub.cg_grpid,
					 grp_id)) {
			found = true;
			break;
		}
	}
	return (found == true) ? grp_priv : NULL;
}

/* lookup by internal subgrp id */
static inline struct crt_grp_priv *
crt_grp_lookup_int_grpid_locked(uint64_t int_grpid)
{
	struct crt_grp_priv	*grp_priv;
	bool			found = false;

	d_list_for_each_entry(grp_priv, &crt_grp_list, gp_link) {
		if (grp_priv->gp_int_grpid == int_grpid) {
			found = true;
			break;
		}
	}
	return (found == true) ? grp_priv : NULL;
}

struct crt_grp_priv *
crt_grp_lookup_int_grpid(uint64_t int_grpid)
{
	struct crt_grp_priv	*grp_priv;

	pthread_rwlock_rdlock(&crt_grp_list_rwlock);
	grp_priv = crt_grp_lookup_int_grpid_locked(int_grpid);
	pthread_rwlock_unlock(&crt_grp_list_rwlock);

	return grp_priv;
}

static inline void
crt_grp_insert_locked(struct crt_grp_priv *grp_priv)
{
	D_ASSERT(grp_priv != NULL);
	d_list_add_tail(&grp_priv->gp_link, &crt_grp_list);
}

static inline void
crt_grp_del_locked(struct crt_grp_priv *grp_priv)
{
	D_ASSERT(grp_priv != NULL);
	d_list_del_init(&grp_priv->gp_link);
}

static inline int
crt_grp_priv_create(struct crt_grp_priv **grp_priv_created,
		    crt_group_id_t grp_id, bool primary_grp,
		    d_rank_list_t *membs, crt_grp_create_cb_t grp_create_cb,
		    void *arg)
{
	struct crt_grp_priv *grp_priv;
	int	rc = 0;

	D_ASSERT(grp_priv_created != NULL);
	D_ASSERT(grp_id != NULL && strlen(grp_id) > 0 &&
		 strlen(grp_id) < CRT_GROUP_ID_MAX_LEN);

	D_ALLOC_PTR(grp_priv);
	if (grp_priv == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_INIT_LIST_HEAD(&grp_priv->gp_link);
	grp_priv->gp_primary = primary_grp;
	grp_priv->gp_pub.cg_grpid = strdup(grp_id);
	if (grp_priv->gp_pub.cg_grpid == NULL) {
		D_ERROR("strdup grp_id (%s) failed.\n", grp_id);
		D_FREE_PTR(grp_priv);
		D_GOTO(out, rc = -DER_NOMEM);
	}
	rc = d_rank_list_dup_sort_uniq(&grp_priv->gp_membs, membs,
					 true /* input */);
	if (rc != 0) {
		D_ERROR("d_rank_list_dup_sort_uniq failed, rc: %d.\n", rc);
		free(grp_priv->gp_pub.cg_grpid);
		D_FREE_PTR(grp_priv);
		D_GOTO(out, rc);
	}

	grp_priv->gp_status = CRT_GRP_CREATING;
	D_INIT_LIST_HEAD(&grp_priv->gp_child_rpcs);
	grp_priv->gp_priv = arg;

	if (!primary_grp) {
		D_ASSERT(grp_priv->gp_membs != NULL);
		grp_priv->gp_parent_rpc = NULL;
		/* TODO tree children num */
		grp_priv->gp_child_num = membs->rl_nr.num;
		grp_priv->gp_child_ack_num = 0;
		grp_priv->gp_create_cb = grp_create_cb;
	}

	pthread_rwlock_init(&grp_priv->gp_rwlock, NULL);

	crt_barrier_info_init(grp_priv);

	*grp_priv_created = grp_priv;

out:
	return rc;
}

static inline int
crt_grp_lookup_create(crt_group_id_t grp_id, d_rank_list_t *member_ranks,
		      crt_grp_create_cb_t grp_create_cb, void *arg,
		      struct crt_grp_priv **grp_result)
{
	struct crt_grp_priv	*grp_priv = NULL;
	int			rc = 0;

	D_ASSERT(member_ranks != NULL);
	D_ASSERT(grp_result != NULL);

	pthread_rwlock_wrlock(&crt_grp_list_rwlock);
	grp_priv = crt_grp_lookup_locked(grp_id);
	if (grp_priv != NULL) {
		pthread_rwlock_unlock(&crt_grp_list_rwlock);
		*grp_result = grp_priv;
		D_GOTO(out, rc = -DER_EXIST);
	}

	rc = crt_grp_priv_create(&grp_priv, grp_id, false /* primary group */,
				 member_ranks, grp_create_cb, arg);
	if (rc != 0) {
		D_ERROR("crt_grp_priv_create failed, rc: %d.\n", rc);
		pthread_rwlock_unlock(&crt_grp_list_rwlock);
		D_GOTO(out, rc);
	}
	D_ASSERT(grp_priv != NULL);
	crt_grp_insert_locked(grp_priv);
	pthread_rwlock_unlock(&crt_grp_list_rwlock);

	*grp_result = grp_priv;

out:
	return rc;
}

static inline void
crt_grp_priv_destroy(struct crt_grp_priv *grp_priv)
{
	if (grp_priv == NULL)
		return;

	/* remove from group list */
	pthread_rwlock_wrlock(&crt_grp_list_rwlock);
	crt_grp_del_locked(grp_priv);
	pthread_rwlock_unlock(&crt_grp_list_rwlock);

	/* destroy the grp_priv */
	d_rank_list_free(grp_priv->gp_membs);
	if (grp_priv->gp_psr_phy_addr != NULL)
		free(grp_priv->gp_psr_phy_addr);
	pthread_rwlock_destroy(&grp_priv->gp_rwlock);
	free(grp_priv->gp_pub.cg_grpid);

	crt_barrier_info_destroy(grp_priv);

	D_FREE_PTR(grp_priv);
}

struct gc_req {
	d_list_t	 gc_link;
	crt_rpc_t	*gc_rpc;
};

static inline int
gc_add_child_rpc(struct crt_grp_priv *grp_priv, crt_rpc_t *gc_rpc)
{
	struct gc_req	*gc_req_item;
	int		rc = 0;

	D_ASSERT(grp_priv != NULL);
	D_ASSERT(gc_rpc != NULL);

	D_ALLOC_PTR(gc_req_item);
	if (gc_req_item == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_INIT_LIST_HEAD(&gc_req_item->gc_link);
	gc_req_item->gc_rpc = gc_rpc;

	rc = crt_req_addref(gc_rpc);
	D_ASSERT(rc == 0);

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	d_list_add_tail(&gc_req_item->gc_link, &grp_priv->gp_child_rpcs);
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

out:
	return rc;
}

static inline void
gc_del_child_rpc(struct crt_grp_priv *grp_priv, crt_rpc_t *gc_rpc)
{
	struct gc_req	*gc, *gc_next;
	int		rc = 0;

	D_ASSERT(grp_priv != NULL);
	D_ASSERT(gc_rpc != NULL);

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	d_list_for_each_entry_safe(gc, gc_next, &grp_priv->gp_child_rpcs,
				   gc_link) {
		if (gc->gc_rpc == gc_rpc) {
			d_list_del_init(&gc->gc_link);
			/* decref corresponds to the addref in
			 * gc_add_child_rpc */
			rc = crt_req_decref(gc_rpc);
			D_ASSERT(rc == 0);
			D_FREE_PTR(gc);
			break;
		}
	}
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);
}

void
crt_hdlr_grp_create(crt_rpc_t *rpc_req)
{
	struct crt_grp_priv		*grp_priv = NULL;
	struct crt_grp_create_in	*gc_in;
	struct crt_grp_create_out	*gc_out;
	d_rank_t			 pri_rank;
	int				 rc = 0;

	D_ASSERT(rpc_req != NULL);
	gc_in = crt_req_get(rpc_req);
	gc_out = crt_reply_get(rpc_req);
	D_ASSERT(gc_in != NULL && gc_out != NULL);

	rc = crt_group_rank(NULL, &pri_rank);
	D_ASSERT(rc == 0);

	/*
	 * the grp_priv->gp_membs will be duplicated from gc_membs and sorted
	 * unique after returns succeed. (d_rank_list_dup_sort_uniq).
	 */
	rc = crt_grp_lookup_create(gc_in->gc_grp_id, gc_in->gc_membs,
				   NULL /* grp_create_cb */, NULL /* arg */,
				   &grp_priv);
	if (rc == 0) {
		D_ASSERT(grp_priv != NULL);
		grp_priv->gp_status = CRT_GRP_NORMAL;
		grp_priv->gp_ctx = rpc_req->cr_ctx;
		grp_priv->gp_int_grpid = gc_in->gc_int_grpid;
	} else if (rc == -DER_EXIST) {
		D_ASSERT(grp_priv != NULL);
		if (pri_rank == gc_in->gc_initiate_rank &&
		    grp_priv->gp_status == CRT_GRP_CREATING) {
			grp_priv->gp_status = CRT_GRP_NORMAL;
			grp_priv->gp_ctx = rpc_req->cr_ctx;
			rc = 0;
		} else {
			D_ERROR("crt_grp_lookup_create (%s) failed, existed.\n",
				gc_in->gc_grp_id);
			D_GOTO(out, rc);
		}
	} else {
		D_ERROR("crt_grp_lookup_create (%s) failed, rc: %d.\n",
			gc_in->gc_grp_id, rc);
		D_GOTO(out, rc);
	}

	/* assign the size and logical rank number for the subgrp */
	D_ASSERT(grp_priv->gp_membs != NULL &&
		 grp_priv->gp_membs->rl_nr.num > 0 &&
		 grp_priv->gp_membs->rl_ranks != NULL);
	grp_priv->gp_size = grp_priv->gp_membs->rl_nr.num;
	rc = d_idx_in_rank_list(grp_priv->gp_membs, pri_rank,
				&grp_priv->gp_self, true /* input */);
	if (rc != 0) {
		D_ERROR("d_idx_in_rank_list(rank %d, group %s) failed, "
			"rc: %d.\n", pri_rank, gc_in->gc_grp_id, rc);
		D_GOTO(out, rc = -DER_OOG);
	}

	crt_barrier_update_master(grp_priv);
out:
	gc_out->gc_rank = pri_rank;
	gc_out->gc_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_req->cr_opc);
	else if (gc_out->gc_rc == 0)
		D_DEBUG("pri_rank %d created subgrp (%s), internal group id 0x"
			CF_X64", gp_size %d, gp_self %d.\n", pri_rank,
			grp_priv->gp_pub.cg_grpid, grp_priv->gp_int_grpid,
			grp_priv->gp_size, grp_priv->gp_self);
}

static void
gc_rpc_cb(const struct crt_cb_info *cb_info)
{
	struct crt_grp_priv		*grp_priv;
	crt_rpc_t			*gc_req;
	struct crt_grp_create_in	*gc_in;
	struct crt_grp_create_out	*gc_out;
	d_rank_t			 my_rank;
	bool				 gc_done = false;
	int				 rc = 0;

	gc_req = cb_info->cci_rpc;
	gc_in = crt_req_get(gc_req);
	gc_out = crt_reply_get(gc_req);
	rc = cb_info->cci_rc;
	grp_priv = cb_info->cci_arg;
	D_ASSERT(grp_priv != NULL && gc_in != NULL && gc_out != NULL);

	crt_group_rank(NULL, &my_rank);
	if (rc != 0)
		D_ERROR("RPC error, rc: %d.\n", rc);
	if (gc_out->gc_rc)
		D_ERROR("group create failed at rank %d, rc: %d.\n",
			gc_out->gc_rank, gc_out->gc_rc);

	/* TODO error handling */

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	if (rc != 0 || gc_out->gc_rc != 0)
		grp_priv->gp_rc = (rc == 0) ? gc_out->gc_rc : rc;
	grp_priv->gp_child_ack_num++;
	D_ASSERT(grp_priv->gp_child_ack_num <= grp_priv->gp_child_num);
	if (grp_priv->gp_child_ack_num == grp_priv->gp_child_num)
		gc_done = true;
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

	gc_del_child_rpc(grp_priv, gc_req);

	if (!gc_done)
		D_GOTO(out, rc);

	if (grp_priv->gp_create_cb != NULL)
		grp_priv->gp_create_cb(&grp_priv->gp_pub, grp_priv->gp_priv,
				       grp_priv->gp_rc);

	if (grp_priv->gp_rc != 0) {
		D_ERROR("group create failed, rc: %d.\n", grp_priv->gp_rc);
		crt_grp_priv_destroy(grp_priv);
	} else {
		grp_priv->gp_status = CRT_GRP_NORMAL;
	}

out:
	return;
}

/**
 * Validates an input group id string. Checks both length and for presence of
 * invalid characters.
 *
 * \param grpid [IN]		unique group ID.
 *
 * \return			zero if grpid is valid, -DER_INVAL otherwise
 */
int
crt_validate_grpid(const crt_group_id_t grpid) {
	const char *ptr = grpid;
	size_t len = strnlen(grpid, CRT_GROUP_ID_MAX_LEN + 1);

	if (len == 0 || len > CRT_GROUP_ID_MAX_LEN)
		return -DER_INVAL;

	while (*ptr != '\0') {
		if (*ptr < ' ' || *ptr > '~' || /* non-printable characters */
		    *ptr == ';' || *ptr == '"' || *ptr == '`' ||
		    *ptr == 39 || /* single quote */
		    *ptr == 92) /* backslash */
			return -DER_INVAL;
		ptr++;
	}
	return 0;
}

int
crt_group_create(crt_group_id_t grp_id, d_rank_list_t *member_ranks,
		 bool populate_now, crt_grp_create_cb_t grp_create_cb,
		 void *arg)
{
	crt_context_t		 crt_ctx;
	struct crt_grp_priv	*grp_priv = NULL;
	bool			 gc_req_sent = false;
	d_rank_t		 myrank;
	uint32_t		 grp_size;
	bool			 in_grp = false;
	int			 i;
	int			 rc = 0;

	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	if (!crt_is_service()) {
		D_ERROR("Cannot create subgroup on pure client side.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}
	if (crt_validate_grpid(grp_id) != 0) {
		D_ERROR("grp_id contains invalid characters or is too long\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (member_ranks == NULL || grp_create_cb == NULL) {
		D_ERROR("invalid arg, member_ranks %p, grp_create_cb %p.\n",
			member_ranks, grp_create_cb);
		D_GOTO(out, rc = -DER_INVAL);
	}
	crt_group_rank(NULL, &myrank);
	crt_group_size(NULL, &grp_size);
	for (i = 0; i < member_ranks->rl_nr.num; i++) {
		if (member_ranks->rl_ranks[i] >= grp_size) {
			D_ERROR("invalid arg, member_ranks[%d]: %d exceed "
				"primary group size %d.\n",
				i, member_ranks->rl_ranks[i], grp_size);
			D_GOTO(out, rc = -DER_INVAL);
		}
		if (member_ranks->rl_ranks[i] == myrank) {
			in_grp = true;
		}
	}
	if (in_grp == false) {
		D_ERROR("myrank %d not in member_ranks, cannot create group.\n",
			myrank);
		D_GOTO(out, rc = -DER_OOG);
	}
	crt_ctx = crt_context_lookup(0);
	if (crt_ctx == CRT_CONTEXT_NULL) {
		D_ERROR("crt_context_lookup failed.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}

	rc = crt_grp_lookup_create(grp_id, member_ranks, grp_create_cb, arg,
				   &grp_priv);
	if (rc != 0) {
		D_ERROR("crt_grp_lookup_create failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	grp_priv->gp_int_grpid = crt_get_subgrp_id();
	grp_priv->gp_ctx = crt_ctx;

	/* TODO handle the populate_now == false */

	/* send RPC one by one now */
	for (i = 0; i < member_ranks->rl_nr.num; i++) {
		crt_rpc_t			*gc_rpc;
		struct crt_grp_create_in	*gc_in;
		crt_endpoint_t			 tgt_ep = {0};

		tgt_ep.ep_rank = member_ranks->rl_ranks[i];

		rc = crt_req_create(crt_ctx, &tgt_ep, CRT_OPC_GRP_CREATE,
				    &gc_rpc);
		if (rc != 0) {
			D_ERROR("crt_req_create(CRT_OPC_GRP_CREATE) failed, "
				"tgt_ep: %d, rc: %d.\n", tgt_ep.ep_rank, rc);
			grp_priv->gp_child_ack_num +=
				grp_priv->gp_child_num - i;
			grp_priv->gp_rc = rc;
			D_GOTO(out, rc);
		}

		gc_in = crt_req_get(gc_rpc);
		D_ASSERT(gc_in != NULL);
		gc_in->gc_grp_id = grp_priv->gp_pub.cg_grpid;
		gc_in->gc_int_grpid = grp_priv->gp_int_grpid;
		gc_in->gc_membs = grp_priv->gp_membs;
		crt_group_rank(NULL, &gc_in->gc_initiate_rank);

		rc = crt_req_send(gc_rpc, gc_rpc_cb, grp_priv);
		if (rc != 0) {
			D_ERROR("crt_req_send(CRT_OPC_GRP_CREATE) failed, "
				"tgt_ep: %d, rc: %d.\n", tgt_ep.ep_rank, rc);
			grp_priv->gp_child_ack_num +=
				grp_priv->gp_child_num - i;
			grp_priv->gp_rc = rc;
			D_GOTO(out, rc);
		}
		rc = gc_add_child_rpc(grp_priv, gc_rpc);
		D_ASSERT(rc == 0);

		gc_req_sent =  true;
	}

out:
	if (gc_req_sent == false) {
		D_ASSERT(rc != 0);
		D_ERROR("crt_group_create failed, rc: %d.\n", rc);

		if (grp_create_cb != NULL)
			grp_create_cb(NULL, arg, rc);

		crt_grp_priv_destroy(grp_priv);
	}
	return rc;
}

crt_group_t *
crt_group_lookup(crt_group_id_t grp_id)
{
	struct crt_grp_priv	*grp_priv = NULL;
	struct crt_grp_gdata	*grp_gdata;

	if (!crt_initialized()) {
		D_ERROR("CaRT not initialized yet.\n");
		goto out;
	}
	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);
	if (grp_id == NULL) {
		/* to lookup local primary group handle */
		grp_priv = crt_is_service() ? grp_gdata->gg_srv_pri_grp :
					      grp_gdata->gg_cli_pri_grp;
		goto out;
	}
	if (crt_validate_grpid(grp_id) != 0) {
		D_ERROR("grp_id contains invalid characters or is too long\n");
		goto out;
	}

	/* check with local primary group or attached remote primary group */
	if (!crt_is_service()) {
		grp_priv = grp_gdata->gg_cli_pri_grp;
		if (crt_grp_id_identical(grp_id, grp_priv->gp_pub.cg_grpid))
			goto out;
	}
	grp_priv = grp_gdata->gg_srv_pri_grp;
	if (grp_priv && crt_grp_id_identical(grp_id, grp_priv->gp_pub.cg_grpid))
		goto out;

	pthread_rwlock_rdlock(&grp_gdata->gg_rwlock);
	d_list_for_each_entry(grp_priv, &grp_gdata->gg_srv_grps_attached,
			     gp_link) {
		if (crt_grp_id_identical(grp_id, grp_priv->gp_pub.cg_grpid)) {
			pthread_rwlock_unlock(&grp_gdata->gg_rwlock);
			goto out;
		}
	}
	pthread_rwlock_unlock(&grp_gdata->gg_rwlock);

	/* check sub-group */
	pthread_rwlock_rdlock(&crt_grp_list_rwlock);
	grp_priv = crt_grp_lookup_locked(grp_id);
	if (grp_priv == NULL)
		D_DEBUG("group non-exist.\n");
	pthread_rwlock_unlock(&crt_grp_list_rwlock);

out:
	return (grp_priv == NULL) ? NULL : &grp_priv->gp_pub;
}

void
crt_hdlr_grp_destroy(crt_rpc_t *rpc_req)
{
	struct crt_grp_priv		*grp_priv = NULL;
	struct crt_grp_destroy_in	*gd_in;
	struct crt_grp_destroy_out	*gd_out;
	d_rank_t			 my_rank;
	int				 rc = 0;

	D_ASSERT(rpc_req != NULL);
	gd_in = crt_req_get(rpc_req);
	gd_out = crt_reply_get(rpc_req);
	D_ASSERT(gd_in != NULL && gd_out != NULL);

	pthread_rwlock_rdlock(&crt_grp_list_rwlock);
	grp_priv = crt_grp_lookup_locked(gd_in->gd_grp_id);
	if (grp_priv == NULL) {
		D_DEBUG("group non-exist.\n");
		pthread_rwlock_unlock(&crt_grp_list_rwlock);
		D_GOTO(out, rc = -DER_NONEXIST);
	}
	pthread_rwlock_unlock(&crt_grp_list_rwlock);

	rc = crt_group_rank(NULL, &my_rank);
	D_ASSERT(rc == 0);
	/* for gd_initiate_rank, destroy the group in gd_rpc_cb */
	if (my_rank != gd_in->gd_initiate_rank)
		crt_grp_priv_destroy(grp_priv);

out:
	crt_group_rank(NULL, &gd_out->gd_rank);
	gd_out->gd_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_req->cr_opc);
}

static void
gd_rpc_cb(const struct crt_cb_info *cb_info)
{
	struct crt_grp_priv		*grp_priv;
	crt_rpc_t			*gd_req;
	struct crt_grp_destroy_in	*gd_in;
	struct crt_grp_destroy_out	*gd_out;
	d_rank_t			 my_rank;
	bool				 gd_done = false;
	int				 rc = 0;

	gd_req = cb_info->cci_rpc;
	gd_in = crt_req_get(gd_req);
	gd_out = crt_reply_get(gd_req);
	rc = cb_info->cci_rc;
	grp_priv = cb_info->cci_arg;
	D_ASSERT(grp_priv != NULL && gd_in != NULL && gd_out != NULL);

	crt_group_rank(NULL, &my_rank);
	if (rc != 0)
		D_ERROR("RPC error, rc: %d.\n", rc);
	if (gd_out->gd_rc)
		D_ERROR("group create failed at rank %d, rc: %d.\n",
			gd_out->gd_rank, gd_out->gd_rc);

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	if (rc != 0 || gd_out->gd_rc != 0)
		grp_priv->gp_rc = (rc == 0) ? gd_out->gd_rc : rc;
	grp_priv->gp_child_ack_num++;
	D_ASSERT(grp_priv->gp_child_ack_num <= grp_priv->gp_child_num);
	if (grp_priv->gp_child_ack_num == grp_priv->gp_child_num)
		gd_done = true;
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

	gc_del_child_rpc(grp_priv, gd_req);

	if (!gd_done)
		D_GOTO(out, rc);

	if (grp_priv->gp_destroy_cb != NULL)
		grp_priv->gp_destroy_cb(grp_priv->gp_destroy_cb_arg,
					grp_priv->gp_rc);

	if (grp_priv->gp_rc != 0)
		D_ERROR("group destroy failed, rc: %d.\n", grp_priv->gp_rc);
	else
		crt_grp_priv_destroy(grp_priv);

out:
	return;
}

int
crt_group_destroy(crt_group_t *grp, crt_grp_destroy_cb_t grp_destroy_cb,
		  void *arg)
{
	struct crt_grp_priv	*grp_priv = NULL;
	d_rank_list_t		*member_ranks;
	crt_context_t		 crt_ctx;
	bool			 gd_req_sent = false;
	int			 i;
	int			 rc = 0;

	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	if (!crt_is_service()) {
		D_ERROR("Cannot destroy subgroup on pure client side.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}
	if (grp == NULL) {
		D_ERROR("invalid parameter of NULL grp.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	grp_priv = container_of(grp, struct crt_grp_priv, gp_pub);
	if (grp_priv->gp_primary) {
		D_DEBUG("cannot destroy primary group.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	pthread_rwlock_rdlock(&crt_grp_list_rwlock);
	if (grp_priv->gp_status != CRT_GRP_NORMAL) {
		D_ERROR("group status: 0x%x, cannot be destroyed.\n",
			grp_priv->gp_status);
		pthread_rwlock_unlock(&crt_grp_list_rwlock);
		D_GOTO(out, rc = -DER_BUSY);
	}
	D_ASSERT(grp_priv->gp_rc == 0);
	member_ranks = grp_priv->gp_membs;
	D_ASSERT(member_ranks != NULL);
	grp_priv->gp_status = CRT_GRP_DESTROYING;
	grp_priv->gp_child_num = member_ranks->rl_nr.num;
	grp_priv->gp_child_ack_num = 0;
	grp_priv->gp_destroy_cb = grp_destroy_cb;
	grp_priv->gp_destroy_cb_arg = arg;
	pthread_rwlock_unlock(&crt_grp_list_rwlock);

	crt_ctx = grp_priv->gp_ctx;
	D_ASSERT(crt_ctx != NULL);

	/* send RPC one by one now */
	for (i = 0; i < member_ranks->rl_nr.num; i++) {
		crt_rpc_t			*gd_rpc;
		struct crt_grp_destroy_in	*gd_in;
		crt_endpoint_t			 tgt_ep = {0};

		tgt_ep.ep_rank = member_ranks->rl_ranks[i];
		rc = crt_req_create(crt_ctx, &tgt_ep, CRT_OPC_GRP_DESTROY,
				    &gd_rpc);
		if (rc != 0) {
			D_ERROR("crt_req_create(CRT_OPC_GRP_DESTROY) failed, "
				"tgt_ep: %d, rc: %d.\n", tgt_ep.ep_rank, rc);
			grp_priv->gp_child_ack_num +=
				grp_priv->gp_child_num - i;
			grp_priv->gp_rc = rc;
			D_GOTO(out, rc);
		}

		gd_in = crt_req_get(gd_rpc);
		D_ASSERT(gd_in != NULL);
		gd_in->gd_grp_id = grp->cg_grpid;
		crt_group_rank(NULL, &gd_in->gd_initiate_rank);

		rc = crt_req_send(gd_rpc, gd_rpc_cb, grp_priv);
		if (rc != 0) {
			D_ERROR("crt_req_send(CRT_OPC_GRP_DESTROY) failed, "
				"tgt_ep: %d, rc: %d.\n", tgt_ep.ep_rank, rc);
			grp_priv->gp_child_ack_num +=
				grp_priv->gp_child_num - i;
			grp_priv->gp_rc = rc;
			D_GOTO(out, rc);
		}

		gd_req_sent =  true;
	}
out:
	if (gd_req_sent == false) {
		D_ASSERT(rc != 0);
		D_ERROR("crt_group_destroy failed, rc: %d.\n", rc);

		if (grp_destroy_cb != NULL)
			grp_destroy_cb(arg, rc);
	}
	return rc;
}

int
crt_group_rank(crt_group_t *grp, d_rank_t *rank)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_grp_priv	*grp_priv;
	int			 rc = 0;

	if (rank == NULL) {
		D_ERROR("invalid parameter of NULL rank pointer.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);

	if (grp == NULL) {
		*rank = crt_is_service() ? grp_gdata->gg_srv_pri_grp->gp_self :
			grp_gdata->gg_cli_pri_grp->gp_self;
	} else {
		grp_priv = container_of(grp, struct crt_grp_priv, gp_pub);
		if (grp_priv->gp_primary && !grp_priv->gp_local) {
			D_DEBUG("not belong to attached remote group (%s).\n",
				grp->cg_grpid);
			D_GOTO(out, rc = -DER_OOG);
		}
		grp_priv = container_of(grp, struct crt_grp_priv, gp_pub);
		*rank = grp_priv->gp_self;
	}

out:
	return rc;
}

int
crt_group_size(crt_group_t *grp, uint32_t *size)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_grp_priv	*grp_priv;
	int			 rc = 0;

	if (size == NULL) {
		D_ERROR("invalid parameter of NULL size pointer.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);

	if (grp == NULL) {
		/* query size of the local primary group */
		*size = crt_is_service() ? grp_gdata->gg_srv_pri_grp->gp_size :
			grp_gdata->gg_cli_pri_grp->gp_size;
	} else {
		grp_priv = container_of(grp, struct crt_grp_priv, gp_pub);
		*size = grp_priv->gp_size;
	}

out:
	return rc;
}

/*
 * Return the enclosing private struct ptr of grp.
 */
struct crt_grp_priv *
crt_grp_pub2priv(crt_group_t *grp)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_grp_priv	*grp_priv;

	D_ASSERT(crt_initialized());

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);
	if (grp == NULL)
		grp_priv = crt_is_service() ?
			grp_gdata->gg_srv_pri_grp : grp_gdata->gg_cli_pri_grp;
	else
		grp_priv = container_of(grp, struct crt_grp_priv, gp_pub);

	return grp_priv;
}

static int
crt_grp_ras_init(struct crt_grp_priv *grp_priv)
{
	int			rc = 0;

	D_ASSERT(grp_priv->gp_service);
	D_ASSERT(grp_priv->gp_primary);
	/* create a dummy list to simplify list management */
	grp_priv->gp_failed_ranks = d_rank_list_alloc(0);
	if (grp_priv->gp_failed_ranks == NULL) {
		D_ERROR("d_rank_list_alloc failed.\n");
		rc = -DER_NOMEM;
	}

	return rc;
}

static int
crt_primary_grp_init(crt_group_id_t grpid)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata;
	struct crt_grp_priv	*grp_priv = NULL;
	crt_group_id_t		 pri_grpid;
	bool			 is_service;
	int			 rc = 0;

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);
	pmix_gdata = grp_gdata->gg_pmix;
	D_ASSERT(grp_gdata->gg_pmix_inited == 1);
	D_ASSERT(pmix_gdata != NULL);

	is_service = crt_is_service();
	if (is_service)
		pri_grpid = (grpid != NULL) ? grpid : CRT_DEFAULT_SRV_GRPID;
	else
		pri_grpid = (grpid != NULL) ? grpid : CRT_DEFAULT_CLI_GRPID;
	rc = crt_grp_priv_create(&grp_priv, pri_grpid, true /* primary group */,
				 NULL /* member_ranks */,
				 NULL /* grp_create_cb */, NULL /* arg */);
	if (rc != 0) {
		D_ERROR("crt_grp_priv_create failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	D_ASSERT(grp_priv != NULL);
	grp_priv->gp_status = CRT_GRP_NORMAL;
	grp_priv->gp_local = 1;
	grp_priv->gp_service = is_service;

	if (crt_is_singleton()) {
		grp_priv->gp_size = 1;
		grp_priv->gp_self = 0;
	} else {
		/* init the rank map */
		D_ALLOC_ARRAY(grp_priv->gp_rank_map, pmix_gdata->pg_univ_size);
		if (grp_priv->gp_rank_map == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		rc = crt_pmix_assign_rank(grp_priv);
		if (rc != 0)
			D_GOTO(out, rc);

		rc = crt_pmix_publish_self(grp_priv);
		if (rc != 0)
			D_GOTO(out, rc);

		rc = crt_pmix_fence();
		if (rc != 0)
			D_GOTO(out, rc);
	}

	grp_priv->gp_membs = d_rank_list_alloc(grp_priv->gp_size);
	if (grp_priv->gp_membs == NULL) {
		D_ERROR("d_rank_list_alloc failed.\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	if (is_service) {
		grp_priv->gp_int_grpid = ((uint64_t)grp_priv->gp_self << 32);
		grp_priv->gp_subgrp_idx = 1;

		grp_gdata->gg_srv_pri_grp = grp_priv;
		rc = crt_grp_lc_create(grp_gdata->gg_srv_pri_grp);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_create failed, rc: %d.\n",
				rc);
			D_GOTO(out, rc);
		}
		rc = crt_grp_ras_init(grp_priv);
		if (rc != 0) {
			D_ERROR("crt_grp_ras_init() failed, rc %d.\n", rc);
			D_GOTO(out, rc);
		}

		crt_barrier_update_master(grp_priv);
	} else {
		grp_gdata->gg_cli_pri_grp = grp_priv;
	}

out:
	if (rc == 0) {
		D_DEBUG("primary group %s, gp_size %d, gp_self %d.\n",
			grp_priv->gp_pub.cg_grpid, grp_priv->gp_size,
			grp_priv->gp_self);
	} else {
		D_ERROR("crt_primary_grp_init failed, rc: %d.\n", rc);
		if (grp_priv != NULL)
			crt_grp_priv_destroy(grp_priv);
	}

	return rc;
}

static void
crt_grp_ras_fini(struct crt_grp_priv *grp_priv)
{
	D_ASSERT(grp_priv->gp_service);
	D_ASSERT(grp_priv->gp_primary);
	d_rank_list_free(grp_priv->gp_failed_ranks);
}
static int
crt_primary_grp_fini(void)
{
	struct crt_grp_priv	*grp_priv;
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata;
	int			 rc = 0;

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);
	pmix_gdata = grp_gdata->gg_pmix;
	D_ASSERT(grp_gdata->gg_pmix_inited == 1);
	D_ASSERT(pmix_gdata != NULL);

	/* destroy the rank map */
	grp_priv = crt_is_service() ? grp_gdata->gg_srv_pri_grp :
				      grp_gdata->gg_cli_pri_grp;
	if (grp_priv->gp_rank_map != NULL)
		D_FREE(grp_priv->gp_rank_map);

	if (crt_is_service()) {
		crt_grp_ras_fini(grp_priv);
		rc = crt_grp_lc_destroy(grp_priv);
		if (rc != 0)
			D_GOTO(out, rc);
	}
	crt_grp_priv_destroy(grp_priv);

out:
	if (rc != 0)
		D_ERROR("crt_primary_grp_fini failed, rc: %d.\n", rc);
	return rc;
}

void
crt_hdlr_uri_lookup(crt_rpc_t *rpc_req)
{
	struct crt_grp_priv		*grp_priv;
	struct crt_context		*crt_ctx;
	struct crt_uri_lookup_in	*ul_in;
	struct crt_uri_lookup_out	*ul_out;
	char				*tmp_uri = NULL;
	int				rc = 0;

	D_ASSERT(rpc_req != NULL);
	ul_in = crt_req_get(rpc_req);
	ul_out = crt_reply_get(rpc_req);
	D_ASSERT(ul_in != NULL && ul_out != NULL);

	if (!crt_is_service()) {
		D_ERROR("crt_hdlr_uri_lookup invalid on client.\n");
		rc = -DER_PROTO;
	}
	grp_priv = crt_gdata.cg_grp->gg_srv_pri_grp;
	if (strncmp(ul_in->ul_grp_id, grp_priv->gp_pub.cg_grpid,
		    CRT_GROUP_ID_MAX_LEN) != 0) {
		D_ERROR("ul_grp_id %s mismatch with gg_srv_pri_grp %s.\n",
			ul_in->ul_grp_id, grp_priv->gp_pub.cg_grpid);
		rc = -DER_INVAL;
	}
	if (rc != 0) {
		ul_out->ul_uri = NULL;
		D_GOTO(out, rc = 0);
	}

	crt_ctx = rpc_req->cr_ctx;

	if (ul_in->ul_rank >= grp_priv->gp_size) {
		D_WARN("Lookup of invalid rank %d in group %s (%d)\n",
		       ul_in->ul_rank, grp_priv->gp_pub.cg_grpid,
		       grp_priv->gp_size);
		D_GOTO(out, rc = -DER_INVAL);
	}
	rc = crt_grp_lc_lookup(grp_priv, crt_ctx->cc_idx,
			       ul_in->ul_rank, 0, &ul_out->ul_uri, NULL);
	if (rc != 0) {
		D_ERROR("crt_grp_lc_lookup(grp %s, rank %d) failed, rc: %d.\n",
			grp_priv->gp_pub.cg_grpid, ul_in->ul_rank, rc);
		D_GOTO(out, rc);
	}
	if (ul_out->ul_uri != NULL)
		D_GOTO(out, rc);
	rc = crt_pmix_uri_lookup(ul_in->ul_grp_id, ul_in->ul_rank,
				 &tmp_uri);
	if (rc != 0) {
		D_ERROR("crt_pmix_uri_lookup() failed, rc %d\n", rc);
		D_GOTO(out, rc);
	}
	ul_out->ul_uri = tmp_uri;
	rc = crt_grp_lc_uri_insert(grp_priv, crt_ctx->cc_idx, ul_in->ul_rank,
				   tmp_uri);
	if (rc != 0)
		D_ERROR("crt_grp_lc_uri_insert() failed, rc %d\n", rc);

out:
	ul_out->ul_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_req->cr_opc);
	if (tmp_uri != NULL)
		free(tmp_uri);
}

/*
 * Given a base URI and a tag number, return the URI of that tag.
 */
char *
crt_get_tag_uri(const char *base_uri, int tag)
{
	char		*tag_uri = NULL;
	char		*pchar;
	int		 port;

	if (tag >= CRT_SRV_CONTEXT_NUM) {
		D_ERROR("invalid tag %d (CRT_SRV_CONTEXT_NUM %d).\n",
			tag, CRT_SRV_CONTEXT_NUM);
		D_GOTO(out, 0);
	}
	D_ALLOC(tag_uri, CRT_ADDR_STR_MAX_LEN);
	if (tag_uri == NULL)
		D_GOTO(out, 0);
	strncpy(tag_uri, base_uri, CRT_ADDR_STR_MAX_LEN);

	if (tag != 0) {
		pchar = strrchr(tag_uri, ':');
		if (pchar == NULL) {
			D_ERROR("bad format of base_addr %s.\n", tag_uri);
			free(tag_uri);
			tag_uri = NULL;
			D_GOTO(out, 0);
		}
		pchar++;
		port = atoi(pchar);
		port += tag;
		snprintf(pchar, 16, "%d", port);
		D_DEBUG("base uri(%s), tag(%d) uri(%s).\n",
			base_uri, tag, tag_uri);
	}

out:
	return tag_uri;
}

int
crt_group_attach(crt_group_id_t srv_grpid, crt_group_t **attached_grp)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_grp_priv	*grp_priv;
	crt_group_t		*grp_at = NULL;
	bool			 is_service;
	crt_context_t		 crt_ctx;
	int			 rc = 0;

	if (srv_grpid == NULL) {
		D_ERROR("invalid parameter, NULL srv_grpid.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (crt_validate_grpid(srv_grpid) != 0) {
		D_ERROR("srv_grpid contains invalid characters "
			"or is too long\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (attached_grp == NULL) {
		D_ERROR("invalid parameter, NULL attached_grp.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (crt_gdata.cg_grp_inited == 0) {
		D_ERROR("crt group not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);

	crt_ctx = crt_context_lookup(0);
	if (crt_ctx == NULL) {
		D_ERROR("crt_context 0 doesn't exist.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	is_service = crt_is_service();
	pthread_rwlock_rdlock(&grp_gdata->gg_rwlock);
	if (!is_service) {
		grp_priv = grp_gdata->gg_srv_pri_grp;
		if (grp_priv && crt_grp_id_identical(srv_grpid,
					grp_priv->gp_pub.cg_grpid)) {
			if (grp_priv->gp_finalizing == 0) {
				crt_grp_priv_addref(grp_priv);
				*attached_grp = &grp_priv->gp_pub;
			} else {
				D_DEBUG("group %s is finalizing, try attach "
					"again later.\n",
					grp_priv->gp_pub.cg_grpid);
				rc = -DER_AGAIN;
			}
			pthread_rwlock_unlock(&grp_gdata->gg_rwlock);
			D_GOTO(out, rc);
		};
	}

	d_list_for_each_entry(grp_priv, &grp_gdata->gg_srv_grps_attached,
			      gp_link) {
		if (crt_grp_id_identical(srv_grpid,
					 grp_priv->gp_pub.cg_grpid)) {
			if (grp_priv->gp_finalizing == 0) {
				crt_grp_priv_addref(grp_priv);
				*attached_grp = &grp_priv->gp_pub;
			} else {
				D_DEBUG("group %s is finalizing, try attach "
					"again later.\n",
					grp_priv->gp_pub.cg_grpid);
				rc = -DER_AGAIN;
			}
			pthread_rwlock_unlock(&grp_gdata->gg_rwlock);
			D_GOTO(out, rc);
		}
	}
	pthread_rwlock_unlock(&grp_gdata->gg_rwlock);

	rc = crt_grp_attach(srv_grpid, &grp_at);
	if (rc != 0) {
		D_ERROR("crt_grp_attach to %s failed, rc: %d.\n",
			srv_grpid, rc);
		D_GOTO(out, rc);
	}
	D_ASSERT(grp_at != NULL);

	pthread_rwlock_wrlock(&grp_gdata->gg_rwlock);

	if (!is_service) {
		grp_priv = grp_gdata->gg_srv_pri_grp;
		if (grp_priv == NULL) {
			/*
			 * for client, set gg_srv_pri_grp as first attached
			 * service group.
			 */
			grp_gdata->gg_srv_pri_grp = container_of(grp_at,
						struct crt_grp_priv, gp_pub);
			crt_grp_priv_addref(grp_gdata->gg_srv_pri_grp);
			*attached_grp = grp_at;
			pthread_rwlock_unlock(&grp_gdata->gg_rwlock);
			D_GOTO(out, rc);
		} else if (crt_grp_id_identical(srv_grpid,
					grp_priv->gp_pub.cg_grpid)) {
			if (grp_priv->gp_finalizing == 0) {
				crt_grp_priv_addref(grp_priv);
				*attached_grp = &grp_priv->gp_pub;
			} else {
				D_ERROR("group %s finalizing, canot attach.\n",
					grp_priv->gp_pub.cg_grpid);
				rc = -DER_NO_PERM;
			}
			pthread_rwlock_unlock(&grp_gdata->gg_rwlock);
			D_GOTO(out, rc);
		};
	}

	d_list_for_each_entry(grp_priv, &grp_gdata->gg_srv_grps_attached,
			      gp_link) {
		if (crt_grp_id_identical(srv_grpid,
					 grp_priv->gp_pub.cg_grpid)) {
			crt_grp_detach(grp_at);
			if (grp_priv->gp_finalizing == 0) {
				crt_grp_priv_addref(grp_priv);
				*attached_grp = &grp_priv->gp_pub;
			} else {
				D_DEBUG("group %s is finalizing, try attach "
					"again later.\n",
					grp_priv->gp_pub.cg_grpid);
				rc = -DER_AGAIN;
			}
			pthread_rwlock_unlock(&grp_gdata->gg_rwlock);
			D_GOTO(out, rc);
		}
	}

	grp_priv = container_of(grp_at, struct crt_grp_priv, gp_pub);
	crt_grp_priv_addref(grp_priv);
	d_list_add_tail(&grp_priv->gp_link, &grp_gdata->gg_srv_grps_attached);
	*attached_grp = grp_at;

	pthread_rwlock_unlock(&grp_gdata->gg_rwlock);

out:
	if (rc != 0)
		D_ERROR("crt_group_attach failed, rc: %d.\n", rc);
	return rc;
}

int
crt_grp_attach(crt_group_id_t srv_grpid, crt_group_t **attached_grp)
{
	struct crt_grp_priv	*grp_priv = NULL;
	struct crt_context	*crt_ctx = NULL;
	struct lm_grp_priv_t	*lm_grp_priv = NULL;
	int			 rc = 0;

	D_ASSERT(srv_grpid != NULL);
	D_ASSERT(attached_grp != NULL);

	rc = crt_grp_priv_create(&grp_priv, srv_grpid, true /* primary group */,
				 NULL /* member_ranks */,
				 NULL /* grp_create_cb */, NULL /* arg */);
	if (rc != 0) {
		D_ERROR("crt_grp_priv_create failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	D_ASSERT(grp_priv != NULL);
	grp_priv->gp_status = CRT_GRP_NORMAL;
	grp_priv->gp_local = 0;
	grp_priv->gp_service = 1;

	if (crt_is_singleton()) {
		rc = crt_grp_config_load(grp_priv);
		if (rc != 0) {
			D_ERROR("crt_grp_config_load (grpid %s) failed, "
				"rc: %d.\n", srv_grpid, rc);
			D_GOTO(out, rc);
		}
	} else {
		rc = crt_pmix_attach(grp_priv);
		if (rc != 0) {
			D_ERROR("crt_pmix_attach GROUP %s failed, rc: %d.\n",
				srv_grpid, rc);
			D_GOTO(out, rc);
		}
	}

	rc = crt_grp_lc_create(grp_priv);
	if (rc != 0) {
		D_ERROR("crt_grp_lc_create failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	/* insert PSR's base uri into hash table */
	d_list_for_each_entry(crt_ctx, &crt_gdata.cg_ctx_list, cc_link) {
		rc = crt_grp_lc_uri_insert(grp_priv, crt_ctx->cc_idx,
					   grp_priv->gp_psr_rank,
					   grp_priv->gp_psr_phy_addr);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_uri_insert() failed, rc %d\n", rc);
			D_GOTO(out, rc);
		}
	}
	/* insert PSR candidates' base uri into hash table */
	rc = crt_lm_attach(&grp_priv->gp_pub, &lm_grp_priv);
	if (rc != DER_SUCCESS) {
		D_ERROR("crt_lm_attach to %s failed, rc %d.\n", srv_grpid, rc);
		D_GOTO(out, rc);
	}

	rc = crt_grp_ras_init(grp_priv);
	if (rc != 0) {
		D_ERROR("crt_grp_ras_init() failed, rc %d.\n", rc);
		D_GOTO(out, rc);
	}

	*attached_grp = &grp_priv->gp_pub;

out:
	if (rc != 0) {
		D_ERROR("crt_grp_attach, failed, rc: %d.\n", rc);
		if (grp_priv != NULL)
			crt_grp_priv_destroy(grp_priv);
	}
	return rc;
}

int
crt_group_detach(crt_group_t *attached_grp)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_grp_priv	*grp_priv;
	int			 rc = 0;

	if (attached_grp == NULL) {
		D_ERROR("invalid parameter, NULL attached_grp.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (crt_gdata.cg_grp_inited == 0) {
		D_ERROR("crt group not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);

	grp_priv = container_of(attached_grp, struct crt_grp_priv, gp_pub);
	if (grp_priv->gp_local == 1 || grp_priv->gp_service == 0) {
		D_ERROR("the group %s is a local group or non-service group, "
			"cannot be detached.\n", attached_grp->cg_grpid);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_grp_priv_decref(grp_priv);
	if (rc < 0) {
		D_ERROR("crt_grp_priv_decref (group %s) failed, rc: %d.\n",
			grp_priv->gp_pub.cg_grpid, rc);
	} else if (rc > 0) {
		rc = 0;
	} else {
		rc = crt_grp_detach(attached_grp);
		if (rc == 0 && !crt_is_service() &&
		    grp_gdata->gg_srv_pri_grp == grp_priv) {
			grp_gdata->gg_srv_pri_grp = NULL;
			D_DEBUG("reset grp_gdata->gg_srv_pri_grp as NULL.\n");
		}
	}

out:
	return rc;
}

int
crt_grp_detach(crt_group_t *attached_grp)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_grp_priv	*grp_priv;
	struct crt_grp_priv	*grp_priv_tmp;
	struct crt_context	*ctx;
	bool			 found = false;
	int			 i;
	int			 rc = 0;

	D_ASSERT(attached_grp != NULL);
	D_ASSERT(crt_gdata.cg_grp_inited == 1);
	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);
	grp_priv = container_of(attached_grp, struct crt_grp_priv, gp_pub);
	D_ASSERT(grp_priv->gp_local == 0 && grp_priv->gp_service == 1);

	crt_grp_ras_fini(grp_priv);

	pthread_rwlock_rdlock(&crt_gdata.cg_rwlock);
	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		ctx = crt_context_lookup(i);
		if (ctx == NULL)
			continue;
		rc = crt_grp_ctx_invalid(ctx, true /* locked */);
		if (rc != 0) {
			D_ERROR("crt_grp_ctx_invalid failed, rc: %d.\n", rc);
			pthread_rwlock_unlock(&crt_gdata.cg_rwlock);
			D_GOTO(out, rc);
		}
	}
	pthread_rwlock_unlock(&crt_gdata.cg_rwlock);

	rc = crt_grp_lc_destroy(grp_priv);
	if (rc != 0) {
		D_ERROR("crt_grp_lc_destroy failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	if (grp_priv == grp_gdata->gg_srv_pri_grp) {
		crt_grp_priv_destroy(grp_priv);
		grp_gdata->gg_srv_pri_grp = NULL;
		D_GOTO(out, rc);
	}

	/* remove from gg_srv_grps_attached */
	pthread_rwlock_wrlock(&grp_gdata->gg_rwlock);
	d_list_for_each_entry(grp_priv_tmp, &grp_gdata->gg_srv_grps_attached,
			      gp_link) {
		if (crt_grp_id_identical(attached_grp->cg_grpid,
					 grp_priv_tmp->gp_pub.cg_grpid)) {
			found = true;
			break;
		}
	}
	if (found == true)
		d_list_del_init(&grp_priv->gp_link);
	pthread_rwlock_unlock(&grp_gdata->gg_rwlock);

	if (found == true) {
		crt_grp_priv_destroy(grp_priv);
	} else {
		D_ERROR("group %s not in attached list.\n",
			attached_grp->cg_grpid);
		rc = -DER_INVAL;
	}

out:
	if (rc != 0)
		D_ERROR("crt_grp_detach %s failed, rc: %d.\n",
			attached_grp->cg_grpid, rc);
	return rc;
}

int
crt_grp_init(crt_group_id_t grpid)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata;
	int			 rc = 0;

	D_ASSERT(crt_gdata.cg_grp_inited == 0);
	D_ASSERT(crt_gdata.cg_grp == NULL);

	D_ALLOC_PTR(grp_gdata);
	if (grp_gdata == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_INIT_LIST_HEAD(&grp_gdata->gg_cli_grps_attached);
	D_INIT_LIST_HEAD(&grp_gdata->gg_srv_grps_attached);
	D_INIT_LIST_HEAD(&grp_gdata->gg_sub_grps);
	pthread_rwlock_init(&grp_gdata->gg_rwlock, NULL);

	crt_gdata.cg_grp = grp_gdata;

	rc = crt_pmix_init();
	if (rc != 0)
		D_GOTO(out, rc);
	pmix_gdata = grp_gdata->gg_pmix;
	D_ASSERT(grp_gdata->gg_pmix_inited == 1);
	D_ASSERT(pmix_gdata != NULL);

	rc = crt_primary_grp_init(grpid);
	if (rc != 0) {
		crt_pmix_fini();
		D_GOTO(out, rc);
	}

	grp_gdata->gg_inited = 1;
	crt_gdata.cg_grp_inited = 1;

out:
	if (rc != 0) {
		D_ERROR("crt_grp_init failed, rc: %d.\n", rc);
		if (grp_gdata != NULL)
			D_FREE_PTR(grp_gdata);
		crt_gdata.cg_grp = NULL;
	}
	return rc;
}

int
crt_grp_fini(void)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata;
	int			 rc = 0;

	D_ASSERT(crt_gdata.cg_grp_inited == 1);
	D_ASSERT(crt_gdata.cg_grp != NULL);
	grp_gdata = crt_gdata.cg_grp;
	pmix_gdata = grp_gdata->gg_pmix;
	D_ASSERT(pmix_gdata != NULL);

	if (!d_list_empty(&grp_gdata->gg_srv_grps_attached)) {
		D_ERROR("gg_srv_grps_attached non-empty, need to detach the "
			"attached groups first.\n");
		D_GOTO(out, rc = -DER_BUSY);
	}

	rc = crt_primary_grp_fini();
	if (rc != 0)
		D_GOTO(out, rc);

	rc = crt_pmix_fini();
	if (rc != 0)
		D_GOTO(out, rc);

	pthread_rwlock_destroy(&grp_gdata->gg_rwlock);
	D_FREE_PTR(grp_gdata);
	crt_gdata.cg_grp = NULL;
	crt_gdata.cg_grp_inited = 0;

out:
	if (rc != 0)
		D_ERROR("crt_grp_fini failed, rc: %d.\n", rc);
	return rc;
}

#define CRT_MAX_ATTACH_PREFIX 256
static char	crt_attach_prefix[CRT_MAX_ATTACH_PREFIX] = "/tmp";

static inline char *
crt_grp_attach_info_filename(struct crt_grp_priv *grp_priv)
{
	crt_group_id_t	 grpid;
	char		*filename;
	int		 rc;

	D_ASSERT(grp_priv != NULL);
	grpid = grp_priv->gp_pub.cg_grpid;

	rc = asprintf(&filename, "%s/%s.attach_info_tmp", crt_attach_prefix,
		      grpid);
	if (rc == -1) {
		D_ERROR("asprintf %s failed (%s).\n", grpid, strerror(errno));
		filename = NULL;
	} else
		D_ASSERT(filename != NULL);

	return filename;
}

int
crt_group_config_path_set(const char *path)
{
	struct stat buf;
	int rc;

	if (path == NULL) {
		D_ERROR("path can't be NULL");
		return -DER_INVAL;
	}

	if (strlen(path) >= CRT_MAX_ATTACH_PREFIX) {
		D_ERROR("specified path must be fewer than %d characters",
			CRT_MAX_ATTACH_PREFIX);
		return -DER_INVAL;
	}

	rc = stat(path, &buf);
	if (rc != 0) {
		D_ERROR("bad path specified: %s", path);
		return crt_errno2cer(errno);
	}

	if (!S_ISDIR(buf.st_mode)) {
		D_ERROR("not a directory: %s", path);
		return -DER_NOTDIR;
	}

	strcpy(crt_attach_prefix, path);

	return 0;
}

/**
 * Save attach info to file with the name
 * "<singleton_attach_path>/grpid.attach_info_tmp".
 * The format of the file is:
 * line 1: the process set name
 * line 2: process set size
 * line 3: "all" or "self"
 *	   "all" means dump all ranks' uri
 *	   "self" means only dump this rank's uri
 * line 4 ~ N: <rank> <uri>
 *
 * An example file named service_set.attach_info:
 * ========================
 * service_set
 * 5
 * self
 * 4 tcp://192.168.0.1:1234
 * ========================
 */
int
crt_group_config_save(crt_group_t *grp, bool forall)
{
	struct crt_grp_priv	*grp_priv;
	FILE			*fp = NULL;
	char			*filename = NULL;
	crt_group_id_t		 grpid;
	d_rank_t		 rank;
	crt_phy_addr_t		 addr;
	int			 rc = 0;


	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}

	grp_priv = crt_grp_pub2priv(grp);
	if (!grp_priv->gp_service || !grp_priv->gp_primary) {
		D_ERROR("can-only save config info for primary service grp.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (grp_priv->gp_local) {
		rank = grp_priv->gp_self;
		addr = crt_gdata.cg_addr;
	} else {
		rank = grp_priv->gp_psr_rank;
		addr = grp_priv->gp_psr_phy_addr;
	}

	grpid = grp_priv->gp_pub.cg_grpid;
	filename = crt_grp_attach_info_filename(grp_priv);
	if (filename == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	fp = fopen(filename, "w");
	if (fp == NULL) {
		D_ERROR("cannot create file %s(%s).\n",
			filename, strerror(errno));
		D_GOTO(out, rc = crt_errno2cer(errno));
	}
	rc = fprintf(fp, "%s %s\n", "name", grpid);
	if (rc < 0) {
		D_ERROR("write to file %s failed (%s).\n",
			filename, strerror(errno));
		D_GOTO(out, rc = crt_errno2cer(errno));
	}
	rc = fprintf(fp, "%s %d\n", "size", grp_priv->gp_size);
	if (rc < 0) {
		D_ERROR("write to file %s failed (%s).\n",
			filename, strerror(errno));
		D_GOTO(out, rc = crt_errno2cer(errno));
	}
	if (forall)
		rc = fprintf(fp, "all\n");
	else
		rc = fprintf(fp, "self\n");
	if (rc < 0) {
		D_ERROR("write to file %s failed (%s).\n",
			filename, strerror(errno));
		D_GOTO(out, rc = crt_errno2cer(errno));
	}

	if (!forall || grp_priv->gp_size == 1) {
		rc = fprintf(fp, "%d %s\n", rank, addr);
		if (rc < 0) {
			D_ERROR("write to file %s failed (%s).\n",
				filename, strerror(errno));
			D_GOTO(out, rc = crt_errno2cer(errno));
		}
		D_GOTO(done, rc);
	}

	for (rank = 0; rank < grp_priv->gp_size; rank++) {
		char *uri;

		uri = NULL;
		rc = crt_pmix_uri_lookup(grpid, rank, &uri);
		if (rc != 0) {
			D_ERROR("crt_pmix_uri_lookup(grp %s, rank %d), failed "
				"rc: %d.\n", grpid, rank, rc);
			D_GOTO(out, rc);
		}
		D_ASSERT(uri != NULL);
		rc = fprintf(fp, "%d %s\n", rank, uri);
		free(uri);
		if (rc < 0) {
			D_ERROR("write to file %s failed (%s).\n",
				filename, strerror(errno));
			D_GOTO(out, rc = crt_errno2cer(errno));
		}
	}

done:
	if (fclose(fp) != 0) {
		D_ERROR("file %s closing failed (%s).\n",
			filename, strerror(errno));
		fp = NULL;
		D_GOTO(out, rc = crt_errno2cer(errno));
	}
	fp = NULL;
	rc = 0;
out:
	if (filename != NULL)
		free(filename);
	if (fp != NULL)
		fclose(fp);
	return rc;
}

int
crt_grp_config_load(struct crt_grp_priv *grp_priv)
{
	char		*filename = NULL;
	FILE		*fp = NULL;
	crt_group_id_t	grpid = NULL, grpname = NULL;
	char		all_or_self[8] = {'\0'};
	char		fmt[64] = {'\0'};
	crt_phy_addr_t	addr_str = NULL;
	d_rank_t	rank, psr_rank, idx;
	bool		forall;
	int		rc = 0;

	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	if (grp_priv == NULL) {
		D_ERROR("Invalid NULL grp_priv pointer.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	grpid = grp_priv->gp_pub.cg_grpid;
	filename = crt_grp_attach_info_filename(grp_priv);
	if (filename == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	fp = fopen(filename, "r");
	if (fp == NULL) {
		D_ERROR("open file %s failed (%s).\n",
			filename, strerror(errno));
		D_GOTO(out, rc = crt_errno2cer(errno));
	}

	D_ALLOC(grpname, CRT_GROUP_ID_MAX_LEN);
	if (grpname == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	snprintf(fmt, 64, "%%*s%%%ds", CRT_GROUP_ID_MAX_LEN);
	rc = fscanf(fp, fmt, grpname);
	if (rc == EOF) {
		D_ERROR("read from file %s failed (%s).\n",
			filename, strerror(errno));
		D_GOTO(out, rc = crt_errno2cer(errno));
	}
	if (strncmp(grpname, grpid, CRT_GROUP_ID_MAX_LEN) != 0) {
		D_ERROR("grpname %s in file mismatch with grpid %s.\n",
			grpname, grpid);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = fscanf(fp, "%*s%d", &grp_priv->gp_size);
	if (rc == EOF) {
		D_ERROR("read from file %s failed (%s).\n",
			filename, strerror(errno));
		D_GOTO(out, rc = crt_errno2cer(errno));
	}

	rc = fscanf(fp, "%4s", all_or_self);
	if (rc == EOF) {
		D_ERROR("read from file %s failed (%s).\n",
			filename, strerror(errno));
		D_GOTO(out, rc = crt_errno2cer(errno));
	}
	if (strncmp(all_or_self, "all", 3) == 0) {
		forall = true;
	} else if (strncmp(all_or_self, "self", 4) == 0) {
		forall = false;
	} else {
		D_ERROR("got bad all_or_self: %s, illegal file format.\n",
			all_or_self);
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_ALLOC(addr_str, CRT_ADDR_STR_MAX_LEN);
	if (addr_str == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	crt_group_rank(NULL, &rank);
	psr_rank = rank % grp_priv->gp_size;

	memset(fmt, 0, 64);
	snprintf(fmt, 64, "%%d %%%ds", CRT_ADDR_STR_MAX_LEN);
	for (idx = 0; idx < grp_priv->gp_size; idx++) {
		rc = fscanf(fp, fmt, &grp_priv->gp_psr_rank, (char *)addr_str);
		if (rc == EOF) {
			D_ERROR("read from file %s failed (%s).\n",
				filename, strerror(errno));
			D_GOTO(out, rc = crt_errno2cer(errno));
		}
		grp_priv->gp_psr_phy_addr = addr_str;
		if (!forall || grp_priv->gp_psr_rank == psr_rank) {
			D_DEBUG("grp %s selected psr_rank %d, uri %s.\n",
				grpid, grp_priv->gp_psr_rank,
				grp_priv->gp_psr_phy_addr);
			break;
		}
	}
	rc = 0;
out:
	if (fp)
		fclose(fp);
	if (filename != NULL)
		free(filename);
	if (grpname != NULL)
		D_FREE(grpname);
	if (rc != 0) {
		if (addr_str != NULL)
			D_FREE(addr_str);
		D_ERROR("crt_grp_config_load (grpid %s) failed, rc: %d.\n",
			grpid, rc);
	}
	return rc;
}

/*
 * mark rank as evicted in every crt_context's address lookup cache.
 */
static int
crt_grp_lc_mark_evicted(struct crt_grp_priv *grp_priv, d_rank_t rank)
{
	int				 ctx_idx;
	d_list_t			*rlink;
	struct crt_lookup_item		*li;
	struct d_chash_table		*htable;
	int				 rc = 0;

	D_ASSERT(grp_priv != NULL);
	D_ASSERT(rank >= 0 && rank < grp_priv->gp_size);

	for (ctx_idx = 0; ctx_idx < CRT_SRV_CONTEXT_NUM; ctx_idx++) {
		htable = grp_priv->gp_lookup_cache[ctx_idx];
		pthread_rwlock_rdlock(&grp_priv->gp_rwlock);
		rlink = d_chash_rec_find(htable, (void *) &rank, sizeof(rank));
		if (rlink == NULL) {
			pthread_rwlock_unlock(&grp_priv->gp_rwlock);
			D_ALLOC_PTR(li);
			if (li == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			D_INIT_LIST_HEAD(&li->li_link);
			li->li_grp_priv = grp_priv;
			li->li_rank = rank;
			li->li_base_phy_addr = strdup("evicted");
			li->li_initialized = 1;
			li->li_evicted = 1;
			pthread_mutex_init(&li->li_mutex, NULL);
			pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
			rc = d_chash_rec_insert(htable, &rank, sizeof(rank),
						&li->li_link, true);
			if (rc == 0) {
				pthread_rwlock_unlock(&grp_priv->gp_rwlock);
				continue;
			}
			/* insert failed */
			crt_li_destroy(li);
			D_DEBUG("entry already exists, "
				"group %s, rank %d, context id %d\n",
				grp_priv->gp_pub.cg_grpid, rank, ctx_idx);
			rlink = d_chash_rec_find(htable, (void *) &rank,
						 sizeof(rank));
			D_ASSERT(rlink != NULL);
		}
		li = crt_li_link2ptr(rlink);
		D_ASSERT(li->li_grp_priv == grp_priv);
		D_ASSERT(li->li_rank == rank);
		pthread_mutex_lock(&li->li_mutex);
		li->li_evicted = 1;
		pthread_mutex_unlock(&li->li_mutex);
		d_chash_rec_decref(htable, rlink);
		pthread_rwlock_unlock(&grp_priv->gp_rwlock);
	}

out:
	return rc;
}

/* query if a rank is evicted from a group. grp must be a primary group */
bool
crt_rank_evicted(crt_group_t *grp, d_rank_t rank)
{
	struct crt_grp_priv	*grp_priv = NULL;
	bool			 ret = false;

	D_ASSERT(crt_initialized());
	grp_priv = crt_grp_pub2priv(grp);
	D_ASSERT(grp_priv != NULL);

	if (grp_priv->gp_primary == 0) {
		D_ERROR("grp must be a primary group.\n");
		D_GOTO(out, ret);
	}

	if (rank >= grp_priv->gp_size) {
		D_ERROR("Rank out of range. Attempted rank: %d, "
			"valid range [0, %d).\n", rank, grp_priv->gp_size);
		D_GOTO(out, ret);
	}

	pthread_rwlock_rdlock(&grp_priv->gp_rwlock);
	ret = d_rank_in_rank_list(grp_priv->gp_failed_ranks, rank, true);
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

out:
	return ret;
}

static void
crt_exec_eviction_cb(crt_group_t *grp, d_rank_t rank)
{
	struct crt_plugin_cb_priv	*cb_priv;

	/* locking is not necessary since new callbacks are only appended to the
	 * end of the list and there's no function to take items off the list
	 */
	d_list_for_each_entry(cb_priv, &crt_plugin_gdata.cpg_eviction_cbs,
			      cp_link)
		cb_priv->cp_eviction_cb(grp, rank, cb_priv->cp_args);
}

int
crt_rank_evict(crt_group_t *grp, d_rank_t rank)
{
	struct crt_grp_priv	*grp_priv = NULL;
	int			 rc = 0;

	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	/* local non-service groups can't evict ranks */
	if (grp == NULL && !crt_is_service()) {
		D_ERROR("grp must be a service group.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	grp_priv = crt_grp_pub2priv(grp);
	D_ASSERT(grp_priv != NULL);

	if (grp_priv->gp_primary == 0) {
		D_ERROR("grp must be a primary group.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (rank >= grp_priv->gp_size) {
		D_ERROR("Rank out of range. Attempted rank: %d, "
			"valid range [0, %d).\n", rank, grp_priv->gp_size);
		D_GOTO(out, rc = -DER_OOG);
	}

	pthread_rwlock_wrlock(&grp_priv->gp_rwlock);
	if (d_rank_in_rank_list(grp_priv->gp_failed_ranks, rank, true)) {
		D_DEBUG("Rank %d already evicted.\n", rank);
		pthread_rwlock_unlock(&grp_priv->gp_rwlock);
		D_GOTO(out, rc = -DER_EVICTED);
	}
	rc = d_rank_list_append(grp_priv->gp_failed_ranks, rank);
	if (rc != 0) {
		D_ERROR("d_rank_list_append() failed, rc: %d\n", rc);
		pthread_rwlock_unlock(&grp_priv->gp_rwlock);
		D_GOTO(out, rc);
	}
	D_ASSERT(grp_priv->gp_failed_ranks->rl_nr.num < grp_priv->gp_size);
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);

	rc = crt_grp_lc_mark_evicted(grp_priv, rank);
	if (rc != 0) {
		D_ERROR("crt_grp_lc_mark_evicted() failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	D_DEBUG("evicted group %s rank %d.\n", grp_priv->gp_pub.cg_grpid, rank);

	crt_exec_eviction_cb(&grp_priv->gp_pub, rank);
out:
	return rc;
}

int
crt_register_eviction_cb(crt_eviction_cb cb, void *arg)
{
	struct crt_plugin_cb_priv	*cb_priv;
	int				 rc = 0;

	D_ALLOC_PTR(cb_priv);
	if (cb_priv == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	cb_priv->cp_eviction_cb = cb;
	cb_priv->cp_args = arg;
	pthread_rwlock_wrlock(&crt_plugin_gdata.cpg_eviction_rwlock);
	d_list_add_tail(&cb_priv->cp_link, &crt_plugin_gdata.cpg_eviction_cbs);
	pthread_rwlock_unlock(&crt_plugin_gdata.cpg_eviction_rwlock);

out:
	return rc;
}

int
crt_grp_failed_ranks_dup(crt_group_t *grp, d_rank_list_t **failed_ranks)
{
	struct crt_grp_priv	*grp_priv;
	int			 rc = 0;

	D_ASSERT(crt_initialized());
	D_ASSERT(failed_ranks != NULL);

	grp_priv = crt_grp_pub2priv(grp);
	D_ASSERT(grp_priv != NULL);
	if (grp_priv->gp_primary == 0) {
		D_ERROR("grp should be a primary group.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}
	pthread_rwlock_rdlock(&grp_priv->gp_rwlock);
	rc = d_rank_list_dup(failed_ranks, grp_priv->gp_failed_ranks, true);
	pthread_rwlock_unlock(&grp_priv->gp_rwlock);
	if (rc != 0)
		D_ERROR("d_rank_list_dup() failed, group: %s, rc: %d\n",
			grp_priv->gp_pub.cg_grpid, rc);

out:
	return rc;
}

bool
crt_grp_is_local(crt_group_t *grp)
{
	struct crt_grp_priv	*grp_priv;

	D_ASSERT(crt_initialized());
	grp_priv = crt_grp_pub2priv(grp);
	D_ASSERT(grp_priv != NULL);

	return grp_priv->gp_local == 1;
}
