/* Copyright (C) 2016-2019 Intel Corporation
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

static int crt_group_primary_add_internal(struct crt_grp_priv *grp_priv,
					d_rank_t rank, int tag,
					char *uri);

/* global CRT group list */
D_LIST_HEAD(crt_grp_list);
/* protect global group list */
pthread_rwlock_t crt_grp_list_rwlock = PTHREAD_RWLOCK_INITIALIZER;

static void
crt_li_destroy(struct crt_lookup_item *li)
{
	int	i;

	D_ASSERT(li != NULL);
	D_ASSERT(li->li_ref == 0);
	D_ASSERT(li->li_initialized == 1);


	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		if (li->li_tag_addr[i] != NULL)
			D_ERROR("tag %d, li_tag_addr not freed.\n", i);

	}

	D_MUTEX_DESTROY(&li->li_mutex);

	D_FREE_PTR(li);
}

struct crt_lookup_item *
crt_li_link2ptr(d_list_t *rlink)
{
	D_ASSERT(rlink != NULL);
	return container_of(rlink, struct crt_lookup_item, li_link);
}

static int
li_op_key_get(struct d_hash_table *hhtab, d_list_t *rlink, void **key_pp)
{
	struct crt_lookup_item *li = crt_li_link2ptr(rlink);

	*key_pp = (void *)&li->li_rank;
	return sizeof(li->li_rank);
}

static uint32_t
li_op_key_hash(struct d_hash_table *hhtab, const void *key, unsigned int ksize)
{
	D_ASSERT(ksize == sizeof(d_rank_t));

	return (unsigned int)(*(const uint32_t *)key %
		(1U << CRT_LOOKUP_CACHE_BITS));
}

static bool
li_op_key_cmp(struct d_hash_table *hhtab, d_list_t *rlink,
	      const void *key, unsigned int ksize)
{
	struct crt_lookup_item *li = crt_li_link2ptr(rlink);

	D_ASSERT(ksize == sizeof(d_rank_t));

	return li->li_rank == *(d_rank_t *)key;
}

static void
li_op_rec_addref(struct d_hash_table *hhtab, d_list_t *rlink)
{
	struct crt_lookup_item *li = crt_li_link2ptr(rlink);

	D_ASSERT(li->li_initialized);
	D_MUTEX_LOCK(&li->li_mutex);
	li->li_ref++;
	D_MUTEX_UNLOCK(&li->li_mutex);
}

static bool
li_op_rec_decref(struct d_hash_table *hhtab, d_list_t *rlink)
{
	uint32_t			 ref;
	struct crt_lookup_item		*li = crt_li_link2ptr(rlink);

	D_ASSERT(li->li_initialized);
	D_MUTEX_LOCK(&li->li_mutex);
	li->li_ref--;
	ref = li->li_ref;
	D_MUTEX_UNLOCK(&li->li_mutex);

	return ref == 0;
}

static void
li_op_rec_free(struct d_hash_table *hhtab, d_list_t *rlink)
{
	crt_li_destroy(crt_li_link2ptr(rlink));
}

static d_hash_table_ops_t lookup_table_ops = {
	.hop_key_get		= li_op_key_get,
	.hop_key_hash		= li_op_key_hash,
	.hop_key_cmp		= li_op_key_cmp,
	.hop_rec_addref		= li_op_rec_addref,
	.hop_rec_decref		= li_op_rec_decref,
	.hop_rec_free		= li_op_rec_free,
};

struct crt_rank_mapping *
crt_rm_link2ptr(d_list_t *rlink)
{
	D_ASSERT(rlink != NULL);
	return container_of(rlink, struct crt_rank_mapping, rm_link);
}


static int
rm_op_key_get(struct d_hash_table *hhtab, d_list_t *rlink, void **key_pp)
{
	struct crt_rank_mapping *rm = crt_rm_link2ptr(rlink);

	*key_pp = (void *)&rm->rm_key;
	return sizeof(rm->rm_key);
}

static uint32_t
rm_op_key_hash(struct d_hash_table *hhtab, const void *key, unsigned int ksize)
{
	D_ASSERT(ksize == sizeof(d_rank_t));

	return (unsigned int)(*(const uint32_t *)key %
		(1U << CRT_LOOKUP_CACHE_BITS));
}

static bool
rm_op_key_cmp(struct d_hash_table *hhtab, d_list_t *rlink,
	      const void *key, unsigned int ksize)
{
	struct crt_rank_mapping *rm = crt_rm_link2ptr(rlink);

	D_ASSERT(ksize == sizeof(d_rank_t));

	return rm->rm_key == *(d_rank_t *)key;
}

static void
rm_op_rec_addref(struct d_hash_table *hhtab, d_list_t *rlink)
{
	struct crt_rank_mapping *rm = crt_rm_link2ptr(rlink);

	D_ASSERT(rm->rm_initialized);
	D_MUTEX_LOCK(&rm->rm_mutex);
	rm->rm_ref++;
	D_MUTEX_UNLOCK(&rm->rm_mutex);
}

static bool
rm_op_rec_decref(struct d_hash_table *hhtab, d_list_t *rlink)
{
	uint32_t		ref;
	struct crt_rank_mapping	*rm = crt_rm_link2ptr(rlink);

	D_ASSERT(rm->rm_initialized);
	D_MUTEX_LOCK(&rm->rm_mutex);
	ref = --rm->rm_ref;
	D_MUTEX_UNLOCK(&rm->rm_mutex);

	return ref == 0;
}

static void
crt_rm_destroy(struct crt_rank_mapping *rm)
{
	D_ASSERT(rm != NULL);
	D_ASSERT(rm->rm_ref == 0);
	D_ASSERT(rm->rm_initialized == 1);

	D_MUTEX_DESTROY(&rm->rm_mutex);
	D_FREE(rm);
}

static void
rm_op_rec_free(struct d_hash_table *hhtab, d_list_t *rlink)
{
	crt_rm_destroy(crt_rm_link2ptr(rlink));
}

struct crt_uri_item *
crt_ui_link2ptr(d_list_t *rlink)
{
	D_ASSERT(rlink != NULL);
	return container_of(rlink, struct crt_uri_item, ui_link);
}

static int
ui_op_key_get(struct d_hash_table *hhtab, d_list_t *rlink, void **key_pp)
{
	struct crt_uri_item *ui = crt_ui_link2ptr(rlink);

	*key_pp = (void *)&ui->ui_rank;
	return sizeof(ui->ui_rank);
}

static uint32_t
ui_op_key_hash(struct d_hash_table *hhtab, const void *key, unsigned int ksize)
{
	D_ASSERT(ksize == sizeof(d_rank_t));

	return (unsigned int)(*(const uint32_t *)key %
		(1U << CRT_LOOKUP_CACHE_BITS));
}

static bool
ui_op_key_cmp(struct d_hash_table *hhtab, d_list_t *rlink,
	      const void *key, unsigned int ksize)
{
	struct crt_uri_item *ui = crt_ui_link2ptr(rlink);

	D_ASSERT(ksize == sizeof(d_rank_t));

	return ui->ui_rank == *(d_rank_t *)key;
}

static void
ui_op_rec_addref(struct d_hash_table *hhtab, d_list_t *rlink)
{
	struct crt_uri_item *ui = crt_ui_link2ptr(rlink);

	D_ASSERT(ui->ui_initialized);
	D_MUTEX_LOCK(&ui->ui_mutex);
	ui->ui_ref++;
	D_MUTEX_UNLOCK(&ui->ui_mutex);
}

static bool
ui_op_rec_decref(struct d_hash_table *hhtab, d_list_t *rlink)
{
	uint32_t		ref;
	struct crt_uri_item	*ui = crt_ui_link2ptr(rlink);

	D_ASSERT(ui->ui_initialized);
	D_MUTEX_LOCK(&ui->ui_mutex);
	ui->ui_ref--;
	ref = ui->ui_ref;

	D_MUTEX_UNLOCK(&ui->ui_mutex);

	return ref == 0;
}

static void
crt_ui_destroy(struct crt_uri_item *ui)
{
	int	i;

	D_ASSERT(ui != NULL);
	D_ASSERT(ui->ui_ref == 0);
	D_ASSERT(ui->ui_initialized == 1);


	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		if (ui->ui_uri[i])
			D_FREE(ui->ui_uri[i]);
	}

	D_MUTEX_DESTROY(&ui->ui_mutex);

	D_FREE_PTR(ui);
}


static inline char *
grp_li_uri_get(struct crt_lookup_item *li, int tag)
{
	struct crt_uri_item	*ui;
	d_list_t		*rlink;
	struct crt_grp_priv	*grp_priv;
	d_rank_t		rank;

	rank = li->li_rank;
	grp_priv = li->li_grp_priv;

	rlink = d_hash_rec_find(&grp_priv->gp_uri_lookup_cache,
				(void *)&rank, sizeof(rank));
	/* It's possible to have crt_lookup_item for which uri
	 * info has not been populated yet
	 */
	if (rlink == NULL) {
		D_DEBUG(DB_TRACE,
			"Failed to find uri_info for %d:%d\n",
			rank, tag);
		return NULL;
	}

	ui = crt_ui_link2ptr(rlink);
	d_hash_rec_decref(&grp_priv->gp_uri_lookup_cache, rlink);

	return ui->ui_uri[tag];
}

static inline int
grp_li_uri_set(struct crt_lookup_item *li, int tag, const char *uri)
{
	struct crt_uri_item	*ui;
	d_list_t		*rlink;
	struct crt_grp_priv	*grp_priv;
	d_rank_t		rank;
	int			rc = 0;

	rank = li->li_rank;
	grp_priv = li->li_grp_priv;

	rlink = d_hash_rec_find(&grp_priv->gp_uri_lookup_cache,
				(void *)&rank, sizeof(rank));

	if (rlink == NULL) {
		D_ALLOC_PTR(ui);
		if (!ui) {
			D_ERROR("Failed to allocate uri item\n");
			D_GOTO(exit, rc = -DER_NOMEM);
		}

		D_INIT_LIST_HEAD(&ui->ui_link);
		ui->ui_ref = 0;
		ui->ui_initialized = 1;

		rc = D_MUTEX_INIT(&ui->ui_mutex, NULL);
		if (rc != 0) {
			D_FREE_PTR(ui);
			D_GOTO(exit, rc);
		}

		ui->ui_rank = li->li_rank;

		rc = d_hash_rec_insert(&grp_priv->gp_uri_lookup_cache,
				&rank, sizeof(rank),
				&ui->ui_link,
				true /* exclusive */);
		if (rc != 0) {
			D_ERROR("Entry already present\n");
			D_MUTEX_DESTROY(&ui->ui_mutex);
			D_FREE_PTR(ui);
			D_GOTO(exit, rc);
		}
		D_STRNDUP(ui->ui_uri[tag], uri, CRT_ADDR_STR_MAX_LEN);

		if (!ui->ui_uri[tag]) {
			d_hash_rec_delete(&grp_priv->gp_uri_lookup_cache,
					&rank, sizeof(d_rank_t));
			D_GOTO(exit, rc = -DER_NOMEM);
		}
	} else {
		ui = crt_ui_link2ptr(rlink);
		if (!ui->ui_uri[tag]) {
			D_STRNDUP(ui->ui_uri[tag], uri, CRT_ADDR_STR_MAX_LEN);
		}

		if (!ui->ui_uri[tag]) {
			D_ERROR("Failed to strndup uri string\n");
			rc = -DER_NOMEM;
		}

		d_hash_rec_decref(&grp_priv->gp_uri_lookup_cache, rlink);
	}

exit:
	return rc;
}

static void
ui_op_rec_free(struct d_hash_table *hhtab, d_list_t *rlink)
{
	crt_ui_destroy(crt_ui_link2ptr(rlink));
}

static d_hash_table_ops_t uri_lookup_table_ops = {
	.hop_key_get		= ui_op_key_get,
	.hop_key_hash		= ui_op_key_hash,
	.hop_key_cmp		= ui_op_key_cmp,
	.hop_rec_addref		= ui_op_rec_addref,
	.hop_rec_decref		= ui_op_rec_decref,
	.hop_rec_free		= ui_op_rec_free,
};

static d_hash_table_ops_t rank_mapping_ops = {
	.hop_key_get		= rm_op_key_get,
	.hop_key_hash		= rm_op_key_hash,
	.hop_key_cmp		= rm_op_key_cmp,
	.hop_rec_addref		= rm_op_rec_addref,
	.hop_rec_decref		= rm_op_rec_decref,
	.hop_rec_free		= rm_op_rec_free,
};

static int
crt_grp_lc_create(struct crt_grp_priv *grp_priv)
{
	struct d_hash_table	*htables;
	int			  rc = 0, rc2, i, j;

	D_ASSERT(grp_priv != NULL);

	if (grp_priv->gp_primary == 0) {
		D_ERROR("need not create lookup cache for sub-group.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	D_ALLOC_ARRAY(htables, CRT_SRV_CONTEXT_NUM);
	if (htables == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK,
						 CRT_LOOKUP_CACHE_BITS,
						 NULL, &lookup_table_ops,
						 &htables[i]);
		if (rc != 0) {
			D_ERROR("d_hash_table_create failed, rc: %d.\n", rc);
			D_GOTO(free_htables, rc);
		}
	}
	grp_priv->gp_lookup_cache = htables;

	rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK,
				CRT_LOOKUP_CACHE_BITS,
				NULL, &uri_lookup_table_ops,
				&grp_priv->gp_uri_lookup_cache);
	if (rc != 0) {
		D_ERROR("d_hash_table_create failed, rc: %d.\n", rc);
		D_GOTO(free_htables, rc);
	}

	return 0;

free_htables:
	for (j = 0; j < i; j++) {
		rc2 = d_hash_table_destroy_inplace(&htables[j],
						true /* force */);
		if (rc2 != 0)
			D_ERROR("d_hash_table_destroy failed, rc: %d.\n", rc2);
	}
	D_FREE(htables);

out:
	if (rc != 0)
		D_ERROR("crt_grp_lc_create failed, rc: %d.\n", rc);

	return rc;
}

static int
crt_grp_lc_destroy(struct crt_grp_priv *grp_priv)
{
	int	rc = 0, rc2, i;

	D_ASSERT(grp_priv != NULL);

	if (grp_priv->gp_lookup_cache == NULL)
		return 0;

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		rc2 = d_hash_table_destroy_inplace(
					&grp_priv->gp_lookup_cache[i],
					true /* force */);
		if (rc2 != 0) {
			D_ERROR("d_hash_table_destroy failed, rc: %d.\n", rc2);
			rc = rc ? rc : rc2;
		}
	}
	D_FREE(grp_priv->gp_lookup_cache);

	rc2 = d_hash_table_destroy_inplace(&grp_priv->gp_uri_lookup_cache,
					   true /* force */);
	if (rc2 != 0) {
		D_ERROR("d_hash_table_destroy failed, rc: %d.\n", rc2);
		rc = rc ? rc : rc2;
	}

	return rc;
}


static void
crt_grp_lc_uri_remove(struct crt_grp_priv *passed_grp_priv, int ctx_idx,
		d_rank_t rank)
{
	d_list_t		*rlink;
	struct crt_lookup_item	*li;
	int			i;
	struct crt_context	*ctx;
	struct crt_grp_priv	*grp_priv;

	grp_priv = passed_grp_priv;

	if (passed_grp_priv->gp_primary == 0) {
		if (CRT_PMIX_ENABLED())
			grp_priv = crt_grp_pub2priv(NULL);
		else
			grp_priv = passed_grp_priv->gp_priv_prim;

		rank = crt_grp_priv_get_primary_rank(passed_grp_priv, rank);
	}

	ctx = crt_context_lookup(ctx_idx);
	rlink = d_hash_rec_find(&grp_priv->gp_lookup_cache[ctx_idx],
				&rank, sizeof(rank));
	if (rlink == NULL) {
		D_ERROR("Record for rank %d is not found\n", rank);
		return;
	}

	li = crt_li_link2ptr(rlink);

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		if (li->li_tag_addr[i]) {
			crt_hg_addr_free(&ctx->cc_hg_ctx, li->li_tag_addr[i]);
		}
	}

	d_hash_rec_delete_at(&grp_priv->gp_lookup_cache[ctx_idx], rlink);
}


static int
grp_lc_uri_insert_internal_locked(struct crt_grp_priv *grp_priv,
				int ctx_idx, d_rank_t rank,
				uint32_t tag,
				const char *uri)
{
	struct crt_lookup_item	*li;
	int			 rc = 0;
	d_list_t		*rlink;

	rlink = d_hash_rec_find(&grp_priv->gp_lookup_cache[ctx_idx],
				(void *)&rank, sizeof(rank));
	if (rlink == NULL) {
		/* target rank not in cache */
		D_ALLOC_PTR(li);
		if (li == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		rc = D_MUTEX_INIT(&li->li_mutex, NULL);
		if (rc != 0)
			D_GOTO(err_free_li, rc);

		D_INIT_LIST_HEAD(&li->li_link);
		li->li_grp_priv = grp_priv;
		li->li_rank = rank;

		if (uri) {
			rc = grp_li_uri_set(li, tag, uri);
			if (rc != DER_SUCCESS)
				D_GOTO(err_destroy_mutex, rc);
		}

		li->li_initialized = 1;
		li->li_evicted = 0;

		rc = d_hash_rec_insert(&grp_priv->gp_lookup_cache[ctx_idx],
				       &rank, sizeof(rank), &li->li_link,
				       true /* exclusive */);
		if (rc != 0) {
			D_DEBUG(DB_TRACE, "entry already exists in lookup "
				"table, grp_priv %p ctx_idx %d, rank: %d.\n",
				grp_priv, ctx_idx, rank);
			crt_li_destroy(li);
			rc = 0;
		} else {
			D_DEBUG(DB_TRACE, "Filling in URI in lookup table. "
				" grp_priv %p ctx_idx %d, rank: %d, rlink %p\n",
				grp_priv, ctx_idx, rank, &li->li_link);
		}
		D_GOTO(out, rc);
	}

	if (!uri)
		D_GOTO(decref, rc);

	li = crt_li_link2ptr(rlink);
	D_ASSERT(li->li_grp_priv == grp_priv);
	D_ASSERT(li->li_rank == rank);
	D_ASSERT(li->li_initialized != 0);
	D_MUTEX_LOCK(&li->li_mutex);

	if (grp_li_uri_get(li, tag) == NULL) {
		rc = grp_li_uri_set(li, tag, uri);

		if (rc != DER_SUCCESS) {
			D_ERROR("Failed to set uri for %d:%d, uri=%s\n",
				li->li_rank, tag, uri);
			rc = -DER_NOMEM;
		}

		D_DEBUG(DB_TRACE, "Filling in URI in lookup table. "
			"grp_priv %p ctx_idx %d, rank: %d, tag: %u rlink %p\n",
			grp_priv, ctx_idx, rank, tag, &li->li_link);
	}
	D_MUTEX_UNLOCK(&li->li_mutex);

decref:
	d_hash_rec_decref(&grp_priv->gp_lookup_cache[ctx_idx], rlink);
	return rc;

err_destroy_mutex:
	D_MUTEX_DESTROY(&li->li_mutex);

err_free_li:
	D_FREE(li);

out:
	return rc;
}
/*
 * Fill in the base URI of rank in the lookup cache of the crt_ctx.
 */
int
crt_grp_lc_uri_insert(struct crt_grp_priv *passed_grp_priv, int ctx_idx,
		      d_rank_t rank, uint32_t tag, const char *uri)
{
	struct crt_grp_priv	*grp_priv;
	int			 rc = 0;

	D_ASSERT(ctx_idx >= 0 && ctx_idx < CRT_SRV_CONTEXT_NUM);
	if (tag >= CRT_SRV_CONTEXT_NUM) {
		D_ERROR("tag %d out of range [0, %d].\n",
			tag, CRT_SRV_CONTEXT_NUM - 1);
		return -DER_INVAL;
	}

	grp_priv = passed_grp_priv;

	if (passed_grp_priv->gp_primary == 0) {
		if (CRT_PMIX_ENABLED())
			grp_priv = crt_grp_pub2priv(NULL);
		else
			grp_priv = passed_grp_priv->gp_priv_prim;

		rank = crt_grp_priv_get_primary_rank(passed_grp_priv, rank);
	}

	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	rc = grp_lc_uri_insert_internal_locked(grp_priv, ctx_idx, rank, tag,
						uri);
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	return rc;

}

/**
 * Fill in the base URI of rank in the lookup cache of all crt_ctx. grp can be
 * NULL
 */
int
crt_grp_lc_uri_insert_all(crt_group_t *grp, d_rank_t rank, int tag,
			const char *uri)
{
	struct crt_grp_priv	*grp_priv;
	int			 i;
	int			 rc = 0;

	grp_priv = crt_grp_pub2priv(grp);

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		rc = crt_grp_lc_uri_insert(grp_priv, i, rank, tag, uri);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_uri_insert(%p, %d, %d, %d, %s)"
				" failed. rc: %d\n", grp_priv, i, rank, tag,
				uri, rc);
			return rc;
		}
	}

	return rc;
}

int
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

	D_MUTEX_LOCK(&li->li_mutex);
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

out:
	D_MUTEX_UNLOCK(&li->li_mutex);
	return rc;
}

/*
 * Invalid all cached hg_addr in group of one context.
 * It should only be called by crt_context_destroy.
 */
static int
crt_grp_lc_ctx_invalid(struct crt_grp_priv *grp_priv, struct crt_context *ctx)
{
	int	 ctx_idx;
	int	 rc = 0;

	D_ASSERT(grp_priv != NULL && grp_priv->gp_primary == 1);
	D_ASSERT(ctx != NULL);
	ctx_idx = ctx->cc_idx;
	D_ASSERT(ctx_idx >= 0 && ctx_idx < CRT_SRV_CONTEXT_NUM);

	rc = d_hash_table_traverse(&grp_priv->gp_lookup_cache[ctx_idx],
				   crt_grp_lc_addr_invalid, ctx);
	if (rc != 0)
		D_ERROR("d_hash_table_traverse failed, ctx_idx %d, rc: %d.\n",
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
		D_RWLOCK_RDLOCK(&grp_gdata->gg_rwlock);
	grp_priv = grp_gdata->gg_srv_pri_grp;
	if (grp_priv != NULL) {
		crt_swim_disable_all(grp_priv);
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
		D_RWLOCK_UNLOCK(&grp_gdata->gg_rwlock);
	return rc;
}

/*
 * Fill in the hg address  of a tag in the lookup cache of crt_ctx. The host
 * rank where the tag resides in must exist in the cache before calling this
 * routine.
 */
int
crt_grp_lc_addr_insert(struct crt_grp_priv *passed_grp_priv,
		       struct crt_context *crt_ctx,
		       d_rank_t rank, uint32_t tag, hg_addr_t *hg_addr)
{
	d_list_t		*rlink;
	struct crt_lookup_item	*li;
	struct crt_grp_priv	*grp_priv;
	int			 ctx_idx;
	int			 rc = 0;

	D_ASSERT(crt_ctx != NULL);

	if (crt_gdata.cg_share_na == true)
		tag = 0;

	grp_priv = passed_grp_priv;

	if (passed_grp_priv->gp_primary == 0) {
		if (CRT_PMIX_ENABLED())
			grp_priv = crt_grp_pub2priv(NULL);
		else
			grp_priv = passed_grp_priv->gp_priv_prim;

		rank = crt_grp_priv_get_primary_rank(passed_grp_priv, rank);
	}

	ctx_idx = crt_ctx->cc_idx;
	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);

	rlink = d_hash_rec_find(&grp_priv->gp_lookup_cache[ctx_idx],
				(void *)&rank, sizeof(rank));
	D_ASSERT(rlink != NULL);
	li = crt_li_link2ptr(rlink);
	D_ASSERT(li->li_grp_priv == grp_priv);
	D_ASSERT(li->li_rank == rank);
	D_ASSERT(li->li_initialized != 0);

	D_MUTEX_LOCK(&li->li_mutex);
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
	D_MUTEX_UNLOCK(&li->li_mutex);
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
	d_hash_rec_decref(&grp_priv->gp_lookup_cache[ctx_idx], rlink);

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
		  crt_phy_addr_t *uri, hg_addr_t *hg_addr)
{
	struct crt_lookup_item	*li;
	d_list_t		*rlink;
	struct crt_grp_priv	*default_grp_priv;
	int			 rc = 0;

	D_ASSERT(grp_priv != NULL);

	D_ASSERT(tag < CRT_SRV_CONTEXT_NUM);
	D_ASSERT(uri != NULL || hg_addr != NULL);
	D_ASSERT(ctx_idx >= 0 && ctx_idx < CRT_SRV_CONTEXT_NUM);

	if (crt_gdata.cg_share_na == true)
		tag = 0;

	default_grp_priv = grp_priv;
	if (grp_priv->gp_primary == 0) {

		if (CRT_PMIX_ENABLED()) {
			/* this is a local subgroup, get the primary group. */
			default_grp_priv = crt_grp_pub2priv(NULL);
			D_ASSERT(default_grp_priv != NULL);
		} else {
			default_grp_priv = grp_priv->gp_priv_prim;
		}

		/* convert subgroup rank to primary group rank */
		rank = crt_grp_priv_get_primary_rank(grp_priv, rank);
	}

	if (CRT_PMIX_ENABLED())
		D_ASSERT(rank < default_grp_priv->gp_size);

	D_RWLOCK_RDLOCK(&default_grp_priv->gp_rwlock);
	rlink = d_hash_rec_find(&default_grp_priv->gp_lookup_cache[ctx_idx],
				(void *)&rank, sizeof(rank));
	if (rlink != NULL) {
		li = crt_li_link2ptr(rlink);
		D_ASSERT(li->li_grp_priv == default_grp_priv);
		D_ASSERT(li->li_rank == rank);
		D_ASSERT(li->li_initialized != 0);

		D_MUTEX_LOCK(&li->li_mutex);
		if (li->li_evicted == 1) {
			D_MUTEX_UNLOCK(&li->li_mutex);
			d_hash_rec_decref(
				&default_grp_priv->gp_lookup_cache[ctx_idx],
				rlink);
			D_ERROR("tag %d on rank %d already evicted.\n", tag,
				rank);
			D_GOTO(out, rc = -DER_EVICTED);
		}
		D_MUTEX_UNLOCK(&li->li_mutex);

		if (uri != NULL)
			*uri = grp_li_uri_get(li, tag);

		if (hg_addr == NULL)
			D_ASSERT(uri != NULL);
		else if (li->li_tag_addr[tag] != NULL)
			*hg_addr = li->li_tag_addr[tag];
		d_hash_rec_decref(&default_grp_priv->gp_lookup_cache[ctx_idx],
				  rlink);
		D_GOTO(out, rc);
	} else {
		D_DEBUG(DB_ALL, "Entry for rank=%d not found\n", rank);
	}
	D_RWLOCK_UNLOCK(&default_grp_priv->gp_rwlock);

	if (uri)
		*uri = NULL;
	if (hg_addr)
		*hg_addr = NULL;

	return rc;

out:
	D_RWLOCK_UNLOCK(&default_grp_priv->gp_rwlock);
	return rc;
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
struct crt_grp_priv *
crt_grp_lookup_int_grpid(uint64_t int_grpid)
{
	struct crt_grp_priv	*grp_priv;
	bool			 found = false;

	/* Check if the group is primary group or not */
	if (!crt_grp_is_subgrp_id(int_grpid)) {
		grp_priv = crt_grp_pub2priv(NULL);

		crt_grp_priv_addref(grp_priv);
		D_GOTO(exit, 0);
	}

	/* Lookup subgroup in the list */
	D_RWLOCK_RDLOCK(&crt_grp_list_rwlock);
	d_list_for_each_entry(grp_priv, &crt_grp_list, gp_link) {
		if (grp_priv->gp_int_grpid == int_grpid) {
			found = true;
			break;
		}
	}
	if (found == true)
		crt_grp_priv_addref(grp_priv);
	else
		grp_priv = NULL;
	D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);

exit:
	return grp_priv;
}

/* lookup by string subgrp id */
struct crt_grp_priv *
crt_grp_lookup_grpid(crt_group_id_t grp_id)
{
	struct crt_grp_priv	*grp_priv;
	bool			found = false;

	D_RWLOCK_RDLOCK(&crt_grp_list_rwlock);
	d_list_for_each_entry(grp_priv, &crt_grp_list, gp_link) {
		if (crt_grp_id_identical(grp_priv->gp_pub.cg_grpid,
					 grp_id)) {
			found = true;
			break;
		}
	}

	if (found == true)
		crt_grp_priv_addref(grp_priv);
	else
		grp_priv = NULL;
	D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);

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
	struct crt_grp_priv	*grp_priv;
	struct crt_swim_membs	*csm;
	struct crt_swim_target	*cst;
	int			 rc = 0;

	D_ASSERT(grp_priv_created != NULL);
	D_ASSERT(grp_id != NULL && strlen(grp_id) > 0 &&
		 strlen(grp_id) < CRT_GROUP_ID_MAX_LEN);

	D_ALLOC_PTR(grp_priv);
	if (grp_priv == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_INIT_LIST_HEAD(&grp_priv->gp_link);
	grp_priv->gp_local = 1;
	grp_priv->gp_primary = primary_grp;
	D_STRNDUP(grp_priv->gp_pub.cg_grpid, grp_id, CRT_GROUP_ID_MAX_LEN + 1);
	if (grp_priv->gp_pub.cg_grpid == NULL)
		D_GOTO(out_grp_priv, rc = -DER_NOMEM);

	csm = &grp_priv->gp_membs_swim;
	D_CIRCLEQ_INIT(&csm->csm_head);

	rc = D_RWLOCK_INIT(&csm->csm_rwlock, NULL);
	if (rc)
		D_GOTO(out_grpid, rc);

	rc = grp_priv_set_membs(grp_priv, membs);
	if (rc) {
		D_ERROR("grp_priv_set_membs failed, rc: %d.\n", rc);
		D_GOTO(out_swim_lock, rc);
	}

	if (membs != NULL)
		grp_priv->gp_size = membs->rl_nr;

	grp_priv->gp_status = CRT_GRP_CREATING;
	grp_priv->gp_priv = arg;

	if (!primary_grp)
		grp_priv->gp_create_cb = grp_create_cb;

	grp_priv->gp_refcount = 1;
	rc = D_RWLOCK_INIT(&grp_priv->gp_rwlock, NULL);
	if (rc)
		D_GOTO(out_swim_lock, rc);

	rc = crt_barrier_info_init(grp_priv);
	if (rc)
		D_GOTO(out_grp_lock, rc);

	*grp_priv_created = grp_priv;
	D_GOTO(out, rc);

out_grp_lock:
	D_RWLOCK_DESTROY(&grp_priv->gp_rwlock);
out_swim_lock:
	csm->csm_target = NULL;
	while (!D_CIRCLEQ_EMPTY(&csm->csm_head)) {
		cst = D_CIRCLEQ_FIRST(&csm->csm_head);
		D_CIRCLEQ_REMOVE(&csm->csm_head, cst, cst_link);
		D_FREE(cst);
	}
	D_RWLOCK_DESTROY(&csm->csm_rwlock);
out_grpid:
	D_FREE(grp_priv->gp_pub.cg_grpid);
out_grp_priv:
	D_FREE(grp_priv);
out:
	return rc;
}

static int
crt_grp_ras_init(struct crt_grp_priv *grp_priv)
{
	d_rank_list_t	*rank_list;
	d_rank_list_t	*live_ranks;
	d_rank_list_t	*failed_ranks;
	int		rc = 0;

	D_ASSERT(grp_priv->gp_service);
	if (!CRT_PMIX_ENABLED()) {
		D_ALLOC_PTR(grp_priv->gp_rwlock_ft);
		if (!grp_priv->gp_rwlock_ft)
			return -DER_NOMEM;

		return D_RWLOCK_INIT(grp_priv->gp_rwlock_ft, NULL);
	}

	rank_list = grp_priv_get_membs(grp_priv);
	rc = grp_priv_copy_live_ranks(grp_priv, rank_list);
	if (rc != 0) {
		D_ERROR("grp_priv_copy_live_ranks() failed, grp: %s, rc: %d\n",
			grp_priv->gp_pub.cg_grpid, rc);
		D_GOTO(out, rc);
	}

	if (grp_priv->gp_primary) {
		rc = grp_priv_init_failed_ranks(grp_priv);
		if (rc != 0) {
			grp_priv_fini_live_ranks(grp_priv);
			D_GOTO(out, rc);
		}

		D_ALLOC_PTR(grp_priv->gp_rwlock_ft);
		if (grp_priv->gp_rwlock_ft == NULL) {
			grp_priv_fini_failed_ranks(grp_priv);
			grp_priv_fini_live_ranks(grp_priv);
			D_GOTO(out, rc = -DER_NOMEM);
		}

		rc = D_RWLOCK_INIT(grp_priv->gp_rwlock_ft, NULL);
		if (rc != 0) {
			D_FREE(grp_priv->gp_rwlock_ft);
			grp_priv_fini_failed_ranks(grp_priv);
			grp_priv_fini_live_ranks(grp_priv);
			D_GOTO(out, rc);
		}

	} else {
		struct crt_grp_priv	*default_grp_priv = NULL;

		default_grp_priv = crt_grp_pub2priv(NULL);
		D_ASSERT(default_grp_priv != NULL);

		grp_priv_set_default_failed_ranks(grp_priv, default_grp_priv);
		grp_priv->gp_rwlock_ft = default_grp_priv->gp_rwlock_ft;
	}

	D_RWLOCK_WRLOCK(grp_priv->gp_rwlock_ft);

	failed_ranks = grp_priv_get_failed_ranks(grp_priv);
	live_ranks = grp_priv_get_live_ranks(grp_priv);

	d_rank_list_filter(failed_ranks, live_ranks, true /* exclude */);

	D_RWLOCK_UNLOCK(grp_priv->gp_rwlock_ft);

out:
	return rc;
}

static inline int
crt_grp_lookup_create(crt_group_id_t grp_id, d_rank_list_t *member_ranks,
		      crt_grp_create_cb_t grp_create_cb, void *arg,
		      struct crt_grp_priv **grp_result)
{
	struct crt_grp_priv	*grp_priv = NULL;
	int			 rc = 0;

	D_ASSERT(member_ranks != NULL);
	D_ASSERT(grp_result != NULL);

	D_RWLOCK_WRLOCK(&crt_grp_list_rwlock);
	grp_priv = crt_grp_lookup_locked(grp_id);
	if (grp_priv != NULL) {
		D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);
		*grp_result = grp_priv;
		D_GOTO(out, rc = -DER_EXIST);
	}

	rc = crt_grp_priv_create(&grp_priv, grp_id, false /* primary group */,
				 member_ranks, grp_create_cb, arg);
	if (rc != 0) {
		D_ERROR("crt_grp_priv_create failed, rc: %d.\n", rc);
		D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);
		D_GOTO(out, rc);
	}
	D_ASSERT(grp_priv != NULL);
	/* only service groups can have sub groups */
	grp_priv->gp_service = 1;
	rc = crt_grp_ras_init(grp_priv);
	if (rc != 0) {
		D_ERROR("crt_grp_ras_init() failed, rc: %d.\n", rc);
		D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);
		D_GOTO(out, rc);
	}

	crt_grp_insert_locked(grp_priv);
	D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);

	*grp_result = grp_priv;

out:
	return rc;
}

void
crt_grp_priv_destroy(struct crt_grp_priv *grp_priv)
{
	if (grp_priv == NULL)
		return;

	/* remove from group list */
	D_RWLOCK_WRLOCK(&crt_grp_list_rwlock);
	crt_grp_del_locked(grp_priv);
	D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);

	crt_swim_rank_del_all(grp_priv);
	D_RWLOCK_DESTROY(&grp_priv->gp_membs_swim.csm_rwlock);

	/* destroy the members */
	grp_priv_fini_membs(grp_priv);

	if (!grp_priv->gp_primary) {
		struct crt_grp_priv *grp_priv_prim = grp_priv->gp_priv_prim;

		/*
		 * grp_priv_prim may be NULL as crt_grp_priv_destroy is also
		 * used to destroy partially created secondary groups. See
		 * crt_group_secondary_create.
		 */
		if (grp_priv_prim != NULL) {
			int i;

			for (i = 0; i < CRT_MAX_SEC_GRPS; i++) {
				if (grp_priv_prim->gp_priv_sec[i] == grp_priv)
					break;
			}
			D_ASSERT(i < CRT_MAX_SEC_GRPS); /* must be found */
			grp_priv->gp_priv_prim->gp_priv_sec[i] = NULL;
		}
		d_hash_table_destroy_inplace(&grp_priv->gp_p2s_table,
						true);
		d_hash_table_destroy_inplace(&grp_priv->gp_s2p_table,
						true);
	}

	if (grp_priv->gp_psr_phy_addr != NULL)
		D_FREE(grp_priv->gp_psr_phy_addr);
	D_RWLOCK_DESTROY(&grp_priv->gp_rwlock);
	D_FREE(grp_priv->gp_pub.cg_grpid);

	crt_barrier_info_destroy(grp_priv);

	D_FREE(grp_priv);
}

struct gc_req {
	d_list_t	 gc_link;
	crt_rpc_t	*gc_rpc;
};

void
crt_hdlr_grp_create(crt_rpc_t *rpc_req)
{
	struct crt_grp_priv		*grp_priv = NULL;
	struct crt_grp_create_in	*gc_in;
	struct crt_grp_create_out	*gc_out;
	d_rank_t			 pri_rank;
	int				 rc = 0;
	d_rank_list_t			*membs;

	D_ASSERT(rpc_req != NULL);
	gc_in = crt_req_get(rpc_req);
	gc_out = crt_reply_get(rpc_req);

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
	membs = grp_priv_get_membs(grp_priv);

	grp_priv->gp_size = membs->rl_nr;
	rc = d_idx_in_rank_list(membs, pri_rank,
				&grp_priv->gp_self);

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
		D_ERROR("crt_reply_send failed, rc: %d, opc: %#x.\n",
			rc, rpc_req->cr_opc);
	else if (gc_out->gc_rc == 0)
		D_DEBUG(DB_TRACE, "pri_rank %d created subgrp (%s), internal "
			"group id 0x"DF_X64", gp_size %d, gp_self %d.\n",
			pri_rank, grp_priv->gp_pub.cg_grpid,
			grp_priv->gp_int_grpid, grp_priv->gp_size,
			grp_priv->gp_self);
}


int
crt_grp_create_corpc_aggregate(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct crt_grp_create_out	*gc_out_source;
	struct crt_grp_create_out	*gc_out_result;

	D_ASSERT(source != NULL && result != NULL);
	gc_out_source = crt_reply_get(source);
	gc_out_result = crt_reply_get(result);
	if (gc_out_source->gc_rc != 0)
		gc_out_result->gc_rc = gc_out_source->gc_rc;

	return 0;
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

static int
gc_corpc_err_cb(void *arg, int status)
{
	struct crt_grp_priv		*grp_priv;

	grp_priv = arg;

	if (grp_priv->gp_create_cb != NULL)
		grp_priv->gp_create_cb(NULL,
				       grp_priv->gp_priv, grp_priv->gp_rc);
	crt_grp_priv_decref(grp_priv);

	return status;
}

static void
grp_create_corpc_cb(const struct crt_cb_info *cb_info)
{
	struct crt_grp_priv		*grp_priv;
	crt_rpc_t			*gc_req;
	struct crt_grp_create_out	*gc_out;
	int				 rc;

	gc_req = cb_info->cci_rpc;
	D_ASSERT(gc_req != NULL);
	gc_out = crt_reply_get(gc_req);
	grp_priv = (struct crt_grp_priv *)cb_info->cci_arg;
	D_ASSERT(gc_out != NULL && grp_priv != NULL);
	if (cb_info->cci_rc)
		D_ERROR("RPC error, rc: %d.\n", cb_info->cci_rc);
	if (gc_out->gc_rc)
		D_ERROR("group create failed, rc: %d.\n", gc_out->gc_rc);
	rc = cb_info->cci_rc;
	if (rc == 0)
		rc = gc_out->gc_rc;

	if (rc != 0) {
		D_ERROR("group create failed, rc: %d.\n", rc);
		grp_priv->gp_rc = rc;
		rc = crt_group_destroy(&grp_priv->gp_pub,
				       gc_corpc_err_cb, grp_priv);
		if (rc != 0)
			D_ERROR("crt_group_destroy() failed, rc: %d.\n", rc);
	} else {
		grp_priv->gp_status = CRT_GRP_NORMAL;
		if (grp_priv->gp_create_cb != NULL)
			grp_priv->gp_create_cb(&grp_priv->gp_pub,
					       grp_priv->gp_priv, rc);
	}
}

int
crt_group_create(crt_group_id_t grp_id, d_rank_list_t *member_ranks,
		 bool populate_now, crt_grp_create_cb_t grp_create_cb,
		 void *arg)
{
	crt_context_t			 crt_ctx;
	struct crt_grp_priv		*grp_priv = NULL;
	bool				 gc_req_sent = false;
	struct crt_grp_priv		*default_grp_priv = NULL;
	d_rank_t			 myrank, rank;
	uint32_t			 grp_size;
	crt_rpc_t			*gc_corpc;
	struct crt_grp_create_in	*gc_in;
	d_rank_list_t			*excluded_ranks;
	d_rank_list_t			*default_gp_membs;
	d_rank_list_t			*membs;
	bool				 in_grp = false;
	int				 i, j;
	int				 rc = 0;
	bool				 found;

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

	default_grp_priv = crt_grp_pub2priv(NULL);
	myrank = default_grp_priv->gp_self;
	grp_size = default_grp_priv->gp_size;
	default_gp_membs = grp_priv_get_membs(default_grp_priv);

	if (default_gp_membs == NULL) {
		D_ERROR("Primary group is empty\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	for (i = 0; i < member_ranks->rl_nr; i++) {
		rank = member_ranks->rl_ranks[i];

		if (CRT_PMIX_ENABLED()) {
			if (rank >= grp_size) {
				D_ERROR("invalid arg, member_ranks[%d]: %d "
					"exceed primary group size %d.\n",
					i, rank, grp_size);
				D_GOTO(out, rc = -DER_INVAL);
			}
		} else {
			found = false;
			for (j = 0; j < default_gp_membs->rl_nr; j++) {
				if (default_gp_membs->rl_ranks[j] == rank) {
					found = true;
					break;
				}
			}

			if (!found) {
				D_ERROR("Rank %d not part of primary group\n",
					rank);
				D_GOTO(out, rc = -DER_INVAL);
			}
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
	/* TODO add another corpc function which takes a inclusion list */

	/* Construct an exclusion list for crt_corpc_req_create(). The exclusion
	 * list contains all live ranks minus non subgroup members so that the
	 * RPC is only sent to subgroup members.
	 */
	rc = d_rank_list_dup(&excluded_ranks, default_gp_membs);
	if (rc != 0) {
		D_ERROR("d_rank_list_dup() failed, rc %d\n", rc);
		D_GOTO(out, rc);
	}

	d_rank_list_filter(member_ranks, excluded_ranks, true /* exlude */);
	rc = crt_corpc_req_create(crt_ctx, NULL, excluded_ranks,
			     CRT_OPC_GRP_CREATE, NULL, NULL, 0,
			     crt_tree_topo(CRT_TREE_KNOMIAL, 4),
			     &gc_corpc);
	d_rank_list_free(excluded_ranks);
	if (rc != 0) {
		D_ERROR("crt_corpc_req_create(CRT_OPC_GRP_CREATE) failed, "
			"rc %d\n", rc);
		D_GOTO(out, rc);
	}

	gc_in = crt_req_get(gc_corpc);
	D_ASSERT(gc_in != NULL);
	gc_in->gc_grp_id = grp_id;
	gc_in->gc_int_grpid = grp_priv->gp_int_grpid;
	/* grp_priv->gp_membs is a sorted rank list */
	membs = grp_priv_get_membs(grp_priv);
	gc_in->gc_membs = membs;

	crt_group_rank(NULL, &gc_in->gc_initiate_rank);

	rc = crt_req_send(gc_corpc, grp_create_corpc_cb, grp_priv);
	if (rc != 0) {
		D_ERROR("crt_req_send(CRT_OPC_GRP_CREATE) failed, "
			"rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	gc_req_sent =  true;

out:
	if (gc_req_sent == false) {
		D_ASSERT(rc != 0);
		D_ERROR("crt_group_create failed, rc: %d.\n", rc);

		if (grp_create_cb != NULL)
			grp_create_cb(NULL, arg, rc);

		if (grp_priv)
			crt_grp_priv_decref(grp_priv);
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

	D_RWLOCK_RDLOCK(&grp_gdata->gg_rwlock);
	d_list_for_each_entry(grp_priv, &grp_gdata->gg_srv_grps_attached,
			     gp_link) {
		if (crt_grp_id_identical(grp_id, grp_priv->gp_pub.cg_grpid)) {
			D_RWLOCK_UNLOCK(&grp_gdata->gg_rwlock);
			goto out;
		}
	}
	D_RWLOCK_UNLOCK(&grp_gdata->gg_rwlock);

	/* check sub-group */
	D_RWLOCK_RDLOCK(&crt_grp_list_rwlock);
	grp_priv = crt_grp_lookup_locked(grp_id);
	if (grp_priv == NULL)
		D_DEBUG(DB_TRACE, "group non-exist (%s).\n", grp_id);
	D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);

out:
	return (grp_priv == NULL) ? NULL : &grp_priv->gp_pub;
}

static void
crt_grp_ras_fini(struct crt_grp_priv *grp_priv)
{
	D_ASSERT(grp_priv->gp_service);
	if (grp_priv->gp_primary) {
		grp_priv_fini_failed_ranks(grp_priv);

		D_RWLOCK_DESTROY(grp_priv->gp_rwlock_ft);
		D_FREE(grp_priv->gp_rwlock_ft);
	}

	grp_priv_fini_live_ranks(grp_priv);
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

	D_RWLOCK_RDLOCK(&crt_grp_list_rwlock);
	grp_priv = crt_grp_lookup_locked(gd_in->gd_grp_id);
	if (grp_priv == NULL) {
		D_DEBUG(DB_TRACE, "group non-exist.\n");
		D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);
		D_GOTO(out, rc = -DER_NONEXIST);
	}
	D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);

	rc = crt_group_rank(NULL, &my_rank);
	D_ASSERT(rc == 0);
	/* for gd_initiate_rank, destroy the group in gd_rpc_cb */
	if (my_rank != gd_in->gd_initiate_rank) {
		D_DEBUG(DB_TRACE, "my_rank %d root of bcast %d\n", my_rank,
			gd_in->gd_initiate_rank);
		crt_grp_ras_fini(grp_priv);
		crt_grp_priv_decref(grp_priv);
	}

out:
	crt_group_rank(NULL, &gd_out->gd_rank);
	gd_out->gd_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d, opc: %#x.\n",
			rc, rpc_req->cr_opc);
}

int
crt_grp_destroy_corpc_aggregate(crt_rpc_t *source, crt_rpc_t *result,
				void *priv)
{
	struct crt_grp_destroy_out	*gd_out_source;
	struct crt_grp_destroy_out	*gd_out_result;

	D_ASSERT(source != NULL && result != NULL);
	gd_out_source = crt_reply_get(source);
	gd_out_result = crt_reply_get(result);
	if (gd_out_source->gd_rc != 0)
		gd_out_result->gd_rc = gd_out_source->gd_rc;

	return 0;
}

static void
grp_destroy_corpc_cb(const struct crt_cb_info *cb_info)
{
	struct crt_grp_priv		*grp_priv;
	crt_rpc_t			*gd_req;
	struct crt_grp_destroy_out	*gd_out;
	int				 rc;

	gd_req = cb_info->cci_rpc;
	gd_out = crt_reply_get(gd_req);
	grp_priv = cb_info->cci_arg;
	D_ASSERT(gd_out != NULL && grp_priv != NULL);

	if (cb_info->cci_rc != 0)
		D_ERROR("RPC error, rc: %d.\n", cb_info->cci_rc);
	if (gd_out->gd_rc)
		D_ERROR("group create failed, rc: %d.\n", gd_out->gd_rc);

	rc = cb_info->cci_rc;
	if (rc == 0)
		rc = gd_out->gd_rc;

	if (grp_priv->gp_destroy_cb != NULL)
		grp_priv->gp_destroy_cb(grp_priv->gp_destroy_cb_arg, rc);

	if (rc != 0)
		D_ERROR("group destroy failed, rc: %d.\n", rc);

	crt_grp_ras_fini(grp_priv);
	crt_grp_priv_decref(grp_priv);
}

int
crt_group_destroy(crt_group_t *grp, crt_grp_destroy_cb_t grp_destroy_cb,
		  void *arg)
{
	struct crt_grp_priv		*grp_priv = NULL;
	crt_context_t			 crt_ctx;
	bool				 gd_req_sent = false;
	crt_rpc_t			*gd_corpc;
	struct crt_grp_destroy_in	*gd_in;
	int				 rc = 0;

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
		D_ERROR("cannot destroy primary group.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	D_RWLOCK_RDLOCK(&crt_grp_list_rwlock);
	if (grp_priv->gp_status != CRT_GRP_NORMAL) {
		D_ERROR("group status: %#x, cannot be destroyed.\n",
			grp_priv->gp_status);
		D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);
		D_GOTO(out, rc = -DER_BUSY);
	}

	grp_priv->gp_status = CRT_GRP_DESTROYING;
	grp_priv->gp_destroy_cb = grp_destroy_cb;
	grp_priv->gp_destroy_cb_arg = arg;
	D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);

	crt_ctx = grp_priv->gp_ctx;
	D_ASSERT(crt_ctx != NULL);

	rc = crt_corpc_req_create(crt_ctx, grp, NULL,
				  CRT_OPC_GRP_DESTROY, NULL, NULL, 0,
				  crt_tree_topo(CRT_TREE_KNOMIAL, 4),
				  &gd_corpc);
	if (rc != 0) {
		D_ERROR("crt_corpc_req_create(CRT_OPC_GRP_DESTROY) failed, "
			"rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	gd_in = crt_req_get(gd_corpc);
	D_ASSERT(gd_in != NULL);
	gd_in->gd_grp_id = grp->cg_grpid;
	crt_group_rank(NULL, &gd_in->gd_initiate_rank);

	rc = crt_req_send(gd_corpc, grp_destroy_corpc_cb, grp_priv);
	if (rc != 0) {
		D_ERROR("crt_req_send(CRT_OPC_GRP_DESTROY) failed, rc: %d\n",
			rc);
		D_GOTO(out, rc);
	}
	gd_req_sent =  true;
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
			D_DEBUG(DB_TRACE, "not belong to attached remote "
				"group (%s).\n", grp->cg_grpid);
			D_GOTO(out, rc = -DER_OOG);
		}

		*rank = grp_priv->gp_self;
	}

	if (!CRT_PMIX_ENABLED() && *rank == CRT_NO_RANK) {
		D_ERROR("Self rank was not set yet\n");
		D_GOTO(out, rc = -DER_NONEXIST);
	}

out:
	return rc;
}

int
crt_group_rank_p2s(crt_group_t *subgrp, d_rank_t rank_in, d_rank_t *rank_out)
{
	struct crt_grp_priv	*grp_priv;
	struct crt_rank_mapping	*rm;
	d_list_t		*rlink;
	int			 rc = 0;

	if (!crt_initialized()) {
		D_ERROR("CaRT not initialized yet.\n");
		return -DER_UNINIT;
	}

	if (subgrp == NULL) {
		D_ERROR("Invalid argument: subgrp is NULL.\n");
		return -DER_INVAL;
	}

	if (rank_out == NULL) {
		D_ERROR("Invalid argument: rank_out is NULL.\n");
		return -DER_INVAL;
	}

	grp_priv = container_of(subgrp, struct crt_grp_priv, gp_pub);

	if (grp_priv->gp_primary) {
		*rank_out = rank_in;
		return rc;
	}

	if (CRT_PMIX_ENABLED()) {
		d_rank_list_t *membs;

		membs = grp_priv_get_membs(grp_priv);

		rc = d_idx_in_rank_list(membs, rank_in, rank_out);
		if (rc != 0) {
			D_ERROR("primary rank %d is not a member of grp %s.\n",
				rank_in, subgrp->cg_grpid);
			rc = -DER_OOG;
		}
		D_GOTO(out, rc);
	}

	rlink = d_hash_rec_find(&grp_priv->gp_p2s_table,
			(void *)&rank_in, sizeof(rank_in));
	if (!rlink) {
		D_ERROR("Rank=%d not part of the group\n", rank_in);
		return -DER_OOG;
	}

	rm = crt_rm_link2ptr(rlink);
	*rank_out = rm->rm_value;

	d_hash_rec_decref(&grp_priv->gp_p2s_table, rlink);

out:
	return rc;
}

int
crt_group_rank_s2p(crt_group_t *subgrp, d_rank_t rank_in, d_rank_t *rank_out)
{
	struct crt_grp_priv	*grp_priv;
	int			 rc = 0;

	if (!crt_initialized()) {
		D_ERROR("CaRT not initialized yet.\n");
		return -DER_UNINIT;
	}

	if (!crt_is_service()) {
		D_ERROR("Can only be called in a service group.\n");
		return -DER_INVAL;
	}

	if (subgrp == NULL) {
		D_ERROR("Invalid argument: subgrp is NULL.\n");
		return -DER_INVAL;
	}

	if (rank_out == NULL) {
		D_ERROR("Invalid argument: rank_out is NULL.\n");
		return -DER_INVAL;
	}

	grp_priv = container_of(subgrp, struct crt_grp_priv, gp_pub);
	*rank_out = crt_grp_priv_get_primary_rank(grp_priv, rank_in);

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

int
crt_group_version(crt_group_t *grp, uint32_t *version)
{
	struct crt_grp_priv	*grp_priv;
	int			 rc = 0;

	if (version == NULL) {
		D_ERROR("invalid parameter: version pointer is NULL.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	grp_priv = crt_grp_pub2priv(grp);
	D_ASSERT(grp_priv != NULL);
	D_RWLOCK_RDLOCK(grp_priv->gp_rwlock_ft);
	*version = grp_priv->gp_membs_ver;
	D_RWLOCK_UNLOCK(grp_priv->gp_rwlock_ft);

out:
	return rc;
}

static int
crt_primary_grp_init(crt_group_id_t grpid)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata = NULL;
	struct crt_grp_priv	*grp_priv = NULL;
	crt_group_id_t		 pri_grpid;
	bool			 is_service;
	int			 rc = 0;

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);

	if (CRT_PMIX_ENABLED()) {
		pmix_gdata = grp_gdata->gg_pmix;
		D_ASSERT(grp_gdata->gg_pmix_inited == 1);
		D_ASSERT(pmix_gdata != NULL);
	}

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
	grp_priv->gp_service = is_service;

	if (crt_is_singleton()) {
		grp_priv->gp_size = 1;
		grp_priv->gp_self = 0;
	} else {
		if (!CRT_PMIX_ENABLED()) {
			grp_priv->gp_self = CRT_NO_RANK;
			grp_priv->gp_size = 0;
		} else {
			/* init the pmix rank map */
			D_ALLOC_ARRAY(grp_priv->gp_pmix_rank_map,
					pmix_gdata->pg_univ_size);
			if (grp_priv->gp_pmix_rank_map == NULL)
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
	}

	rc = grp_priv_init_membs(grp_priv, grp_priv->gp_size);
	if (rc != 0) {
		D_ERROR("grp_priv_init_membs() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
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
			crt_grp_lc_destroy(grp_gdata->gg_srv_pri_grp);
			D_GOTO(out, rc);
		}

		crt_barrier_update_master(grp_priv);
	} else {
		grp_gdata->gg_cli_pri_grp = grp_priv;
	}

out:
	if (rc == 0) {
		D_DEBUG(DB_TRACE, "primary group %s, gp_size %d, gp_self %d.\n",
			grp_priv->gp_pub.cg_grpid, grp_priv->gp_size,
			grp_priv->gp_self);
	} else {
		D_ERROR("crt_primary_grp_init failed, rc: %d.\n", rc);
		if (grp_priv != NULL) {
			D_FREE(grp_priv->gp_pmix_rank_map);
			crt_grp_priv_decref(grp_priv);
		}
	}

	return rc;
}

static int
crt_primary_grp_fini(void)
{
	struct crt_grp_priv	*grp_priv;
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata = NULL;
	int			 rc = 0;

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);


	if (CRT_PMIX_ENABLED()) {
		pmix_gdata = grp_gdata->gg_pmix;
		D_ASSERT(grp_gdata->gg_pmix_inited == 1);
		D_ASSERT(pmix_gdata != NULL);
	}

	/* destroy the rank map */
	grp_priv = crt_is_service() ? grp_gdata->gg_srv_pri_grp :
				      grp_gdata->gg_cli_pri_grp;
	D_FREE(grp_priv->gp_pmix_rank_map);

	if (crt_is_service()) {
		crt_grp_ras_fini(grp_priv);
		rc = crt_grp_lc_destroy(grp_priv);
		if (rc != 0)
			D_GOTO(out, rc);
	}
	crt_grp_priv_decref(grp_priv);

out:
	if (rc != 0)
		D_ERROR("crt_primary_grp_fini failed, rc: %d.\n", rc);
	return rc;
}

static void
crt_uri_lookup_forward_cb(const struct crt_cb_info *cb_info)
{
	struct crt_grp_priv		*default_grp_priv;
	crt_rpc_t			*ul_req;
	struct crt_uri_lookup_out	*ul_out;
	struct crt_uri_lookup_in	*ul_fwd_in;
	struct crt_uri_lookup_out	*ul_fwd_out;
	struct crt_context		*crt_ctx;
	d_rank_t			 g_rank;
	uint32_t			 tag;
	char				*uri = NULL;
	int				 rc;

	ul_req = cb_info->cci_arg;
	D_ASSERT(ul_req != NULL);

	if (cb_info->cci_rc != DER_SUCCESS)
		D_GOTO(out, rc = cb_info->cci_rc);

	/* extract URI, insert to local cache */
	ul_fwd_in = crt_req_get(cb_info->cci_rpc);
	ul_fwd_out = crt_reply_get(cb_info->cci_rpc);

	if (ul_fwd_out->ul_rc != 0)
		D_GOTO(out, rc = ul_fwd_out->ul_rc);

	crt_ctx = cb_info->cci_rpc->cr_ctx;
	g_rank = ul_fwd_in->ul_rank;
	tag = ul_fwd_in->ul_tag;
	uri = ul_fwd_out->ul_uri;

	default_grp_priv = crt_gdata.cg_grp->gg_srv_pri_grp;
	rc = crt_grp_lc_uri_insert(default_grp_priv, crt_ctx->cc_idx,
				   g_rank, tag, uri);
	if (rc != 0)
		D_ERROR("crt_grp_lc_uri_insert(%p, %d, %u, %s) failed."
			" rc: %d\n", default_grp_priv, crt_ctx->cc_idx, g_rank,
			uri, rc);

out:
	/* reply to original requester */
	ul_out = crt_reply_get(ul_req);
	ul_out->ul_rc = rc;
	ul_out->ul_uri = uri;

	rc = crt_reply_send(ul_req);
	if (rc != DER_SUCCESS)
		D_ERROR("crt_reply_send failed, rc: %d, opc: %#x.\n",
			rc, ul_req->cr_opc);

	/* Corresponding addref done in crt_uri_lookup_forward*/
	RPC_PUB_DECREF(ul_req);
}

static int
crt_uri_lookup_forward(crt_rpc_t *ul_req, d_rank_t g_rank)
{
	struct crt_grp_priv		*default_grp_priv;
	crt_endpoint_t			 tgt_ep;
	struct crt_uri_lookup_in	*ul_in, *ul_fwd_in;
	crt_rpc_t			*ul_fwd_req;
	struct crt_cb_info		 cbinfo;
	int				 rc = DER_SUCCESS;

	/* corresponding decref done in crt_uri_lookup_forward_cb */
	RPC_PUB_ADDREF(ul_req);

	default_grp_priv = crt_gdata.cg_grp->gg_srv_pri_grp;
	ul_in = crt_req_get(ul_req);
	tgt_ep.ep_grp = &default_grp_priv->gp_pub;
	tgt_ep.ep_rank = g_rank;
	tgt_ep.ep_tag = 0;
	rc = crt_req_create(ul_req->cr_ctx, &tgt_ep, CRT_OPC_URI_LOOKUP,
			    &ul_fwd_req);
	if (rc != DER_SUCCESS) {
		D_ERROR("crt_req_create() failed, rc: %d\n", rc);
		D_GOTO(err_out, rc);
	}

	ul_fwd_in = crt_req_get(ul_fwd_req);
	ul_fwd_in->ul_grp_id = default_grp_priv->gp_pub.cg_grpid;
	ul_fwd_in->ul_rank = g_rank;
	ul_fwd_in->ul_tag = ul_in->ul_tag;

	rc = crt_req_send(ul_fwd_req, crt_uri_lookup_forward_cb, ul_req);
	if (rc != DER_SUCCESS) {
		/* crt_req_send() calls the callback on failure */
		D_ERROR("crt_req_send() failed, rc: %d\n", rc);
	}
	return rc;

err_out:
	cbinfo.cci_rpc = NULL;
	cbinfo.cci_arg = ul_req;
	cbinfo.cci_rc  = rc;
	crt_uri_lookup_forward_cb(&cbinfo);

	return rc;
}

void
crt_hdlr_uri_lookup(crt_rpc_t *rpc_req)
{
	struct crt_grp_priv		*grp_priv;
	struct crt_grp_priv		*default_grp_priv;
	struct crt_grp_priv		*grp_priv_primary;
	struct crt_context		*crt_ctx;
	struct crt_uri_lookup_in	*ul_in;
	struct crt_uri_lookup_out	*ul_out;
	d_rank_t			 g_rank;
	char				*tmp_uri = NULL;
	char				*cached_uri = NULL;
	bool				 should_decref = false;
	int				rc = 0;

	D_ASSERT(rpc_req != NULL);
	ul_in = crt_req_get(rpc_req);
	ul_out = crt_reply_get(rpc_req);

	if (!crt_is_service()) {
		D_ERROR("crt_hdlr_uri_lookup invalid on client.\n");
		rc = -DER_PROTO;
	}
	default_grp_priv = crt_gdata.cg_grp->gg_srv_pri_grp;
	if (strncmp(ul_in->ul_grp_id, default_grp_priv->gp_pub.cg_grpid,
		    CRT_GROUP_ID_MAX_LEN) == 0) {
		grp_priv = default_grp_priv;
		D_DEBUG(DB_TRACE, "ul_grp_id %s matches with gg_srv_pri_grp "
			"%s.\n", ul_in->ul_grp_id,
			default_grp_priv->gp_pub.cg_grpid);
	} else {
		/* handle subgroup lookups */
		D_RWLOCK_RDLOCK(&crt_grp_list_rwlock);
		grp_priv = crt_grp_lookup_locked(ul_in->ul_grp_id);
		if (grp_priv == NULL) {
			rc = -DER_INVAL;
		} else {
			crt_grp_priv_addref(grp_priv);
			should_decref = true;
		}
		D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);
	}

	if (rc != 0 || grp_priv == NULL) {
		D_ERROR("Could not find the group %s specified\n",
			ul_in->ul_grp_id);
		ul_out->ul_uri = NULL;
		D_GOTO(out, rc);
	}

	crt_ctx = rpc_req->cr_ctx;

	if (CRT_PMIX_ENABLED()) {
		if (ul_in->ul_rank >= grp_priv->gp_size) {
			D_WARN("Lookup of invalid rank %d in group %s (%d)\n",
			       ul_in->ul_rank, grp_priv->gp_pub.cg_grpid,
			       grp_priv->gp_size);
			D_GOTO(out, rc = -DER_OOG);
		}
	}

	if (ul_in->ul_tag >= CRT_SRV_CONTEXT_NUM) {
		D_WARN("Looking up invalid tag %d of rank %d "
		       "in group %s (%d)\n",
		       ul_in->ul_tag, ul_in->ul_rank,
		       grp_priv->gp_pub.cg_grpid, grp_priv->gp_size);
		D_GOTO(out, rc = -DER_INVAL);
	}

	grp_priv_primary = default_grp_priv;

	if (grp_priv->gp_primary == 0) {
		if (!CRT_PMIX_ENABLED())
			grp_priv_primary = grp_priv->gp_priv_prim;
	}

	/* convert the requested rank to global rank */
	g_rank = crt_grp_priv_get_primary_rank(grp_priv, ul_in->ul_rank);

	/* step 0, if I am the final target, reply with URI */
	if (g_rank == grp_priv_primary->gp_self) {
		rc = crt_self_uri_get(ul_in->ul_tag, &tmp_uri);
		if (rc != DER_SUCCESS)
			D_ERROR("crt_self_uri_get(tag: %d) failed, "
				"rc %d\n", ul_in->ul_tag, rc);
		ul_out->ul_uri = tmp_uri;
		D_GOTO(out, rc);
	}


	/* step 1, lookup the URI in the local cache */
	rc = crt_grp_lc_lookup(grp_priv_primary, crt_ctx->cc_idx, g_rank,
			       ul_in->ul_tag, &cached_uri, NULL);
	if (rc != 0) {
		D_ERROR("crt_grp_lc_lookup(grp %s, rank %d, tag %d) failed, "
			"rc: %d.\n", grp_priv_primary->gp_pub.cg_grpid,
			g_rank, ul_in->ul_tag, rc);
		D_GOTO(out, rc);
	}
	ul_out->ul_uri = cached_uri;
	if (ul_out->ul_uri != NULL)
		D_GOTO(out, rc);

	/**
	 * step 2, make sure URI of tag 0 is in cache in preparation to forward
	 * the lookup RPC
	 */
	if (ul_in->ul_tag != 0) {
		/* tag != 0, check if tag 0 is in cache */
		rc = crt_grp_lc_lookup(grp_priv_primary, crt_ctx->cc_idx,
				       g_rank, 0, &cached_uri, NULL);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_lookup(grp %s, rank %d, tag %d) "
				"failed, rc: %d.\n",
				grp_priv_primary->gp_pub.cg_grpid,
				g_rank, 0, rc);
			D_GOTO(out, rc);
		}
	}
	ul_out->ul_uri = cached_uri;

	if (cached_uri == NULL && CRT_PMIX_ENABLED()) {
		/* tag 0 URI not in cache, resort to PMIx */
		rc = crt_pmix_uri_lookup(grp_priv_primary->gp_pub.cg_grpid,
					 g_rank, &tmp_uri);
		if (rc != 0) {
			D_ERROR("crt_pmix_uri_lookup() failed, rc %d\n", rc);
			D_GOTO(out, rc);
		}
		rc = crt_grp_lc_uri_insert(grp_priv_primary, crt_ctx->cc_idx,
				g_rank, 0, tmp_uri);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_uri_insert() failed, rc %d\n", rc);
			D_GOTO(out, rc);
		}

		ul_out->ul_uri = tmp_uri;
	}

	if (ul_out->ul_uri == NULL)
		D_GOTO(out, rc = -DER_OOG);

	/* tag 0 uri in cache now */
	if (ul_in->ul_tag == 0) {
		/* we are done at this point */
		D_GOTO(out, rc);
	}

	/* step 3, forward the requst to the final target */
	rc = crt_uri_lookup_forward(rpc_req, g_rank);
	if (rc != 0)
		D_ERROR("crt_uri_lookup_forward() failed, rc %d\n", rc);

	if (should_decref)
		crt_grp_priv_decref(grp_priv);
	return;

out:
	if (should_decref)
		crt_grp_priv_decref(grp_priv);
	ul_out->ul_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d, opc: %#x.\n",
			rc, rpc_req->cr_opc);
	if (tmp_uri != NULL)
		D_FREE(tmp_uri);
}

int
crt_group_attach(crt_group_id_t srv_grpid, crt_group_t **attached_grp)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_grp_priv	*grp_priv;
	crt_group_t		*grp_at = NULL;
	bool			 is_service;
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

	/* For non-PMIX case attach is a group view creation + cfg load */
	if (!CRT_PMIX_ENABLED()) {
		rc = crt_group_view_create(srv_grpid, attached_grp);
		if (rc != 0) {
			D_ERROR("crt_group_view_create() failed; rc=%d\n", rc);
			D_GOTO(out, rc);
		}

		grp_priv = container_of(*attached_grp,
				struct crt_grp_priv, gp_pub);
		rc = crt_grp_config_load(grp_priv);
		if (rc != 0) {
			D_ERROR("crt_grp_config_load() failed; rc=%d\n", rc);
			crt_group_view_destroy(*attached_grp);
		}

		D_GOTO(out, rc);
	}

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);

	is_service = crt_is_service();
	D_RWLOCK_RDLOCK(&grp_gdata->gg_rwlock);
	if (!is_service) {
		grp_priv = grp_gdata->gg_srv_pri_grp;
		if (grp_priv && crt_grp_id_identical(srv_grpid,
					grp_priv->gp_pub.cg_grpid)) {
			if (grp_priv->gp_finalizing == 0) {
				crt_grp_priv_addref(grp_priv);
				*attached_grp = &grp_priv->gp_pub;
			} else {
				D_DEBUG(DB_TRACE, "group %s is finalizing, try "
					"attach again later.\n",
					grp_priv->gp_pub.cg_grpid);
				rc = -DER_AGAIN;
			}
			D_RWLOCK_UNLOCK(&grp_gdata->gg_rwlock);
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
				D_DEBUG(DB_TRACE, "group %s is finalizing, try "
					"attach again later.\n",
					grp_priv->gp_pub.cg_grpid);
				rc = -DER_AGAIN;
			}
			D_RWLOCK_UNLOCK(&grp_gdata->gg_rwlock);
			D_GOTO(out, rc);
		}
	}
	D_RWLOCK_UNLOCK(&grp_gdata->gg_rwlock);

	rc = crt_grp_attach(srv_grpid, &grp_at);
	if (rc != 0) {
		D_ERROR("crt_grp_attach to %s failed, rc: %d.\n",
			srv_grpid, rc);
		D_GOTO(out, rc);
	}
	D_ASSERT(grp_at != NULL);

	D_RWLOCK_WRLOCK(&grp_gdata->gg_rwlock);

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
			D_RWLOCK_UNLOCK(&grp_gdata->gg_rwlock);
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
			D_RWLOCK_UNLOCK(&grp_gdata->gg_rwlock);
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
				D_DEBUG(DB_TRACE, "group %s is finalizing, try "
					"attach again later.\n",
					grp_priv->gp_pub.cg_grpid);
				rc = -DER_AGAIN;
			}
			D_RWLOCK_UNLOCK(&grp_gdata->gg_rwlock);
			crt_grp_detach(grp_at);
			D_GOTO(out, rc);
		}
	}

	grp_priv = container_of(grp_at, struct crt_grp_priv, gp_pub);
	crt_grp_priv_addref(grp_priv);
	d_list_add_tail(&grp_priv->gp_link, &grp_gdata->gg_srv_grps_attached);
	*attached_grp = grp_at;

	D_RWLOCK_UNLOCK(&grp_gdata->gg_rwlock);

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
	int			 rc = 0;

	D_ASSERT(srv_grpid != NULL);
	D_ASSERT(attached_grp != NULL);

	if (!CRT_PMIX_ENABLED()) {
		D_ERROR("Should never be called for non-pmix mode\n");
		D_ASSERT(0);
	}

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
					   grp_priv->gp_psr_rank, 0,
					   grp_priv->gp_psr_phy_addr);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_uri_insert() failed, rc %d\n", rc);
			D_GOTO(out, rc);
		}
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
			crt_grp_priv_decref(grp_priv);
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
	if (rc < 0)
		D_ERROR("crt_grp_priv_decref (group %s) failed, rc: %d.\n",
			grp_priv->gp_pub.cg_grpid, rc);

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

	if (grp_priv->gp_primary) {
		D_RWLOCK_RDLOCK(&crt_gdata.cg_rwlock);
		for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
			ctx = crt_context_lookup(i);
			if (ctx == NULL)
				continue;
			rc = crt_grp_ctx_invalid(ctx, true /* locked */);
			if (rc != 0) {
				D_ERROR("crt_grp_ctx_invalid failed, rc: %d.\n",
					rc);
				D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
				D_GOTO(out, rc);
			}
		}
		D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);

		rc = crt_grp_lc_destroy(grp_priv);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_destroy failed, rc: %d.\n", rc);
			D_GOTO(out, rc);
		}

		if (grp_priv == grp_gdata->gg_srv_pri_grp) {
			crt_grp_priv_decref(grp_priv);
			grp_gdata->gg_srv_pri_grp = NULL;
			D_GOTO(out, rc);
		}

		/* remove from gg_srv_grps_attached */
		D_RWLOCK_WRLOCK(&grp_gdata->gg_rwlock);
		d_list_for_each_entry(grp_priv_tmp,
				&grp_gdata->gg_srv_grps_attached,
				      gp_link) {
			if (crt_grp_id_identical(attached_grp->cg_grpid,
					grp_priv_tmp->gp_pub.cg_grpid)) {
				found = true;
				break;
			}
		}
	} else {
		D_RWLOCK_WRLOCK(&grp_gdata->gg_rwlock);
		found = true;
	}

	if (found == true)
		d_list_del_init(&grp_priv->gp_link);
	D_RWLOCK_UNLOCK(&grp_gdata->gg_rwlock);

	if (found == true) {
		crt_grp_priv_decref(grp_priv);
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
	rc = D_RWLOCK_INIT(&grp_gdata->gg_rwlock, NULL);
	if (rc != 0)
		D_GOTO(out, rc);

	crt_gdata.cg_grp = grp_gdata;

	if (CRT_PMIX_ENABLED()) {
		rc = crt_pmix_init();
		if (rc != 0) {
			D_RWLOCK_DESTROY(&grp_gdata->gg_rwlock);
			D_GOTO(out, rc);
		}

		pmix_gdata = grp_gdata->gg_pmix;
		D_ASSERT(grp_gdata->gg_pmix_inited == 1);
		D_ASSERT(pmix_gdata != NULL);
	}

	rc = crt_primary_grp_init(grpid);
	if (rc != 0) {
		if (CRT_PMIX_ENABLED())
			crt_pmix_fini();
		D_RWLOCK_DESTROY(&grp_gdata->gg_rwlock);
		D_GOTO(out, rc);
	}

	grp_gdata->gg_inited = 1;
	crt_gdata.cg_grp_inited = 1;

out:
	if (rc != 0) {
		D_ERROR("crt_grp_init failed, rc: %d.\n", rc);
		D_FREE(grp_gdata);
		crt_gdata.cg_grp = NULL;
	}
	return rc;
}

int
crt_grp_fini(void)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_pmix_gdata	*pmix_gdata = NULL;
	int			 rc = 0;

	D_ASSERT(crt_gdata.cg_grp_inited == 1);
	D_ASSERT(crt_gdata.cg_grp != NULL);
	grp_gdata = crt_gdata.cg_grp;

	if (CRT_PMIX_ENABLED()) {
		pmix_gdata = grp_gdata->gg_pmix;
		D_ASSERT(pmix_gdata != NULL);
	}

	if (!d_list_empty(&grp_gdata->gg_srv_grps_attached)) {
		D_ERROR("gg_srv_grps_attached non-empty, need to detach the "
			"attached groups first.\n");
		D_GOTO(out, rc = -DER_BUSY);
	}

	rc = crt_primary_grp_fini();
	if (rc != 0)
		D_GOTO(out, rc);

	if (CRT_PMIX_ENABLED()) {
		rc = crt_pmix_fini();
		if (rc != 0)
			D_GOTO(out, rc);
	}

	D_RWLOCK_DESTROY(&grp_gdata->gg_rwlock);
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

	D_ASSERT(grp_priv != NULL);
	grpid = grp_priv->gp_pub.cg_grpid;

	D_ASPRINTF(filename, "%s/%s.attach_info_tmp", crt_attach_prefix, grpid);

	return filename;
}

static inline FILE *
open_tmp_attach_info_file(char **filename)
{
	char		 template[] = "attach-info-XXXXXX";
	int		 tmp_fd;
	FILE		*tmp_file;

	if (filename == NULL) {
		D_ERROR("filename can't be NULL.\n");
		return NULL;
	}

	D_ASPRINTF(*filename, "%s/%s", crt_attach_prefix, template);
	if (*filename == NULL)
		return NULL;
	D_ASSERT(*filename != NULL);

	tmp_fd = mkstemp(*filename);
	if (tmp_fd == -1) {
		D_ERROR("mktemp() failed on %s, error: %s.\n",
			*filename, strerror(errno));
		return NULL;
	}

	tmp_file = fdopen(tmp_fd, "w");
	if (tmp_file == NULL) {
		D_ERROR("fdopen() failed on %s, error: %s\n",
			*filename, strerror(errno));
		close(tmp_fd);
	}

	return tmp_file;
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
		return d_errno2der(errno);
	}

	if (!S_ISDIR(buf.st_mode)) {
		D_ERROR("not a directory: %s", path);
		return -DER_NOTDIR;
	}

	strncpy(crt_attach_prefix, path, CRT_MAX_ATTACH_PREFIX-1);

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
	struct crt_grp_priv	*grp_priv = NULL;
	FILE			*fp = NULL;
	char			*filename = NULL;
	char			*tmp_name = NULL;
	crt_group_id_t		 grpid;
	d_rank_t		 rank;
	crt_phy_addr_t		 addr = NULL;
	bool			 addr_free = false;
	bool			 locked = false;
	d_rank_list_t		*membs = NULL;
	int			 i;
	int			 rc = 0;


	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}

	grp_priv = crt_grp_pub2priv(grp);
	if (!grp_priv->gp_service || !grp_priv->gp_primary) {
		D_ERROR("Can only save config info for primary service grp.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (grp_priv->gp_local) {
		rank = grp_priv->gp_self;
		addr = crt_gdata.cg_addr;
	} else {
		D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
		rank = grp_priv->gp_psr_rank;
		D_STRNDUP(addr, grp_priv->gp_psr_phy_addr,
			  CRT_ADDR_STR_MAX_LEN);
		D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
		if (addr == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		else
			addr_free = true;
	}

	grpid = grp_priv->gp_pub.cg_grpid;
	filename = crt_grp_attach_info_filename(grp_priv);
	if (filename == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	fp = open_tmp_attach_info_file(&tmp_name);
	if (fp == NULL) {
		D_ERROR("cannot create temp file.\n");
		D_GOTO(out, rc = d_errno2der(errno));
	}
	D_ASSERT(tmp_name != NULL);
	rc = fprintf(fp, "%s %s\n", "name", grpid);
	if (rc < 0) {
		D_ERROR("write to file %s failed (%s).\n",
			tmp_name, strerror(errno));
		D_GOTO(out, rc = d_errno2der(errno));
	}
	rc = fprintf(fp, "%s %d\n", "size", grp_priv->gp_size);
	if (rc < 0) {
		D_ERROR("write to file %s failed (%s).\n",
			tmp_name, strerror(errno));
		D_GOTO(out, rc = d_errno2der(errno));
	}
	if (forall)
		rc = fprintf(fp, "all\n");
	else
		rc = fprintf(fp, "self\n");
	if (rc < 0) {
		D_ERROR("write to file %s failed (%s).\n",
			tmp_name, strerror(errno));
		D_GOTO(out, rc = d_errno2der(errno));
	}

	if (!forall || grp_priv->gp_size == 1) {
		rc = fprintf(fp, "%d %s\n", rank, addr);
		if (rc < 0) {
			D_ERROR("write to file %s failed (%s).\n",
				tmp_name, strerror(errno));
			D_GOTO(out, rc = d_errno2der(errno));
		}
		D_GOTO(done, rc);
	}

	if (!CRT_PMIX_ENABLED()) {
		D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
		membs = grp_priv_get_membs(grp_priv);
		locked = true;
	}

	for (i = 0; i < grp_priv->gp_size; i++) {
		char *uri;

		uri = NULL;

		if (CRT_PMIX_ENABLED())
			rank = i;
		else
			rank = membs->rl_ranks[i];

		if (CRT_PMIX_ENABLED()) {
			rc = crt_pmix_uri_lookup(grpid, rank, &uri);
			if (rc != 0) {
				D_ERROR("crt_pmix_uri_lookup(grp %s, rank %d) "
					"failed rc: %d.\n", grpid, rank, rc);
				D_GOTO(out, rc);
			}
		} else {
			rc = crt_rank_uri_get(grp, rank, 0, &uri);
			if (rc != 0) {
				D_ERROR("crt_rank_uri_get(%s, %d) failed "
					"rc: %d.\n", grpid, rank, rc);
				D_GOTO(out, rc);
			}
		}

		D_ASSERT(uri != NULL);

		rc = fprintf(fp, "%d %s\n", rank, uri);

		if (CRT_PMIX_ENABLED())
			free(uri);
		else
			D_FREE(uri);

		if (rc < 0) {
			D_ERROR("write to file %s failed (%s).\n",
				tmp_name, strerror(errno));
			D_GOTO(out, rc = d_errno2der(errno));
		}
	}

done:
	if (fclose(fp) != 0) {
		D_ERROR("file %s closing failed (%s).\n",
			tmp_name, strerror(errno));
		fp = NULL;
		D_GOTO(out, rc = d_errno2der(errno));
	}
	fp = NULL;

	rc = rename(tmp_name, filename);
	if (rc != 0) {
		D_ERROR("Failed to rename %s to %s (%s).\n",
			tmp_name, filename, strerror(errno));
		rc = d_errno2der(errno);
	}
out:
	/* For pmix disabled case. Lock taken above before loop start */
	if (grp_priv && locked)
		D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	D_FREE(filename);
	if (tmp_name != NULL) {
		if (rc != 0)
			unlink(tmp_name);
		D_FREE(tmp_name);
	}
	if (fp != NULL)
		fclose(fp);
	if (addr_free)
		D_FREE(addr);
	return rc;
}

int
crt_group_config_remove(crt_group_t *grp)
{
	struct crt_grp_priv	*grp_priv;
	char			*filename = NULL;
	int			 rc;

	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}

	grp_priv = crt_grp_pub2priv(grp);
	if (!grp_priv->gp_service || !grp_priv->gp_primary) {
		D_ERROR("Can only remove config info for primary service "
			"grp.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	filename = crt_grp_attach_info_filename(grp_priv);
	if (filename == NULL) {
		D_ERROR("crt_grp_attach_info_filename() failed.\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = unlink(filename);
	if (rc != 0) {
		rc = d_errno2der(errno);
		D_ERROR("Failed to remove %s (%s).\n",
			filename, strerror(errno));
	}

out:
	D_FREE(filename);

	return rc;
}

/*
 * Load psr from singleton config file.
 * If psr_rank set as "-1", will mod the group rank with group size as psr rank.
 */
int
crt_grp_config_psr_load(struct crt_grp_priv *grp_priv, d_rank_t psr_rank)
{
	char		*filename = NULL;
	FILE		*fp = NULL;
	crt_group_id_t	grpid = NULL, grpname = NULL;
	char		all_or_self[8] = {'\0'};
	char		fmt[64] = {'\0'};
	crt_phy_addr_t	addr_str = NULL;
	d_rank_t	rank;
	bool		forall;
	int		grp_size;
	int		rc = 0;

	D_ASSERT(crt_initialized());
	D_ASSERT(grp_priv != NULL);

	grpid = grp_priv->gp_pub.cg_grpid;
	filename = crt_grp_attach_info_filename(grp_priv);
	if (filename == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	fp = fopen(filename, "r");
	if (fp == NULL) {
		D_ERROR("open file %s failed (%s).\n",
			filename, strerror(errno));
		D_GOTO(out, rc = d_errno2der(errno));
	}

	D_ALLOC(grpname, CRT_GROUP_ID_MAX_LEN + 1);
	if (grpname == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	snprintf(fmt, 64, "%%*s%%%ds", CRT_GROUP_ID_MAX_LEN);
	rc = fscanf(fp, fmt, grpname);
	if (rc == EOF) {
		D_ERROR("read from file %s failed (%s).\n",
			filename, strerror(errno));
		D_GOTO(out, rc = d_errno2der(errno));
	}
	if (strncmp(grpname, grpid, CRT_GROUP_ID_MAX_LEN) != 0) {
		D_ERROR("grpname %s in file mismatch with grpid %s.\n",
			grpname, grpid);
		D_GOTO(out, rc = -DER_INVAL);
	}

	grp_size = 0;

	rc = fscanf(fp, "%*s%d", &grp_size);
	if (rc == EOF) {
		D_ERROR("read from file %s failed (%s).\n",
			filename, strerror(errno));
		D_GOTO(out, rc = d_errno2der(errno));
	}

	/* Non pmix case will populate group size later on */
	if (CRT_PMIX_ENABLED())
		grp_priv->gp_size = grp_size;

	rc = fscanf(fp, "%4s", all_or_self);
	if (rc == EOF) {
		D_ERROR("read from file %s failed (%s).\n",
			filename, strerror(errno));
		D_GOTO(out, rc = d_errno2der(errno));
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

	D_ALLOC(addr_str, CRT_ADDR_STR_MAX_LEN + 1);
	if (addr_str == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (CRT_PMIX_ENABLED()) {
		if (psr_rank == -1) {
			crt_group_rank(NULL, &rank);
			psr_rank = rank % grp_priv->gp_size;
		} else {
			if (psr_rank >= grp_priv->gp_size) {
				D_ERROR("invalid param (psr %d, gp_size %d)\n",
					psr_rank, grp_priv->gp_size);
				D_GOTO(out, rc = -DER_INVAL);
			}
		}
	}

	memset(fmt, 0, 64);
	snprintf(fmt, 64, "%%d %%%ds", CRT_ADDR_STR_MAX_LEN);
	rc = -DER_INVAL;

	while (1) {
		rc = fscanf(fp, fmt, &rank, (char *)addr_str);
		if (rc == EOF) {
			rc = 0;
			break;
		}

		if (CRT_PMIX_ENABLED()) {
			if (!forall || rank == psr_rank) {
				D_DEBUG(DB_TRACE, "grp %s selected psr_rank %d"
					", uri %s.\n", grpid, rank, addr_str);
				crt_grp_psr_set(grp_priv, rank, addr_str);
				rc = 0;

				break;
			}
		}

		if (!CRT_PMIX_ENABLED()) {


			rc = crt_group_primary_add_internal(grp_priv, rank, 0,
					addr_str);
			if (rc != 0) {
				D_ERROR("crt_group_node_add_internal() failed;"
					" rank=%d uri='%s' rc=%d\n",
					rank, addr_str, rc);
				break;
			}

			if (rank == psr_rank)
				crt_grp_psr_set(grp_priv, rank, addr_str);
		}
	}

	/* If PSR was not specified, pick for now last added node as PSR */
	if (!CRT_PMIX_ENABLED()) {
		/* TODO: PSR selection logic to be changed with CART-688 */
		if (psr_rank != -1)
			crt_grp_psr_set(grp_priv, rank, addr_str);
	}

out:
	if (fp)
		fclose(fp);
	if (filename != NULL)
		free(filename);
	D_FREE(grpname);
	D_FREE(addr_str);

	if (rc != 0)
		D_ERROR("crt_grp_config_psr_load (grpid %s) failed, rc: %d.\n",
			grpid, rc);

	return rc;
}

int
crt_grp_config_load(struct crt_grp_priv *grp_priv)
{
	int	rc;

	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	if (grp_priv == NULL) {
		D_ERROR("Invalid NULL grp_priv pointer.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_grp_config_psr_load(grp_priv, -1);
	if (rc != 0)
		D_ERROR("crt_grp_config_load failed, rc: %d.\n", rc);

out:
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
	struct d_hash_table		*htable;
	int				 rc = 0;

	D_ASSERT(grp_priv != NULL);
	D_ASSERT(rank < grp_priv->gp_size);

	for (ctx_idx = 0; ctx_idx < CRT_SRV_CONTEXT_NUM; ctx_idx++) {
		htable = &grp_priv->gp_lookup_cache[ctx_idx];
		D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
		rlink = d_hash_rec_find(htable, &rank, sizeof(rank));
		if (rlink == NULL) {
			D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
			D_ALLOC_PTR(li);
			if (li == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			D_INIT_LIST_HEAD(&li->li_link);
			li->li_grp_priv = grp_priv;
			li->li_rank = rank;
			li->li_initialized = 1;
			li->li_evicted = 1;
			rc = D_MUTEX_INIT(&li->li_mutex, NULL);
			if (rc != 0) {
				crt_li_destroy(li);
				D_GOTO(out, rc);
			}

			D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
			rc = d_hash_rec_insert(htable, &rank, sizeof(rank),
					       &li->li_link, true);
			if (rc == 0) {
				D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
				continue;
			}
			/* insert failed */
			crt_li_destroy(li);
			D_DEBUG(DB_TRACE, "entry already exists, "
				"group %s, rank %d, context id %d\n",
				grp_priv->gp_pub.cg_grpid, rank, ctx_idx);
			rlink = d_hash_rec_find(htable, (void *) &rank,
						sizeof(rank));
			D_ASSERT(rlink != NULL);
		}
		li = crt_li_link2ptr(rlink);
		D_ASSERT(li->li_grp_priv == grp_priv);
		D_ASSERT(li->li_rank == rank);
		D_MUTEX_LOCK(&li->li_mutex);
		li->li_evicted = 1;
		D_MUTEX_UNLOCK(&li->li_mutex);
		d_hash_rec_decref(htable, rlink);
		D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
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
	d_rank_list_t		*failed_ranks;


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

	D_RWLOCK_RDLOCK(grp_priv->gp_rwlock_ft);
	failed_ranks = grp_priv_get_failed_ranks(grp_priv);
	ret = d_rank_in_rank_list(failed_ranks, rank);
	D_RWLOCK_UNLOCK(grp_priv->gp_rwlock_ft);

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
	crt_endpoint_t		 tgt_ep;
	struct crt_grp_priv	*curr_entry = NULL;
	d_rank_list_t		 tmp_rank_list;
	int			 rc = 0;
	d_rank_list_t		*failed_ranks;
	d_rank_list_t		*live_ranks;
	d_rank_list_t		*tmp_live_ranks;

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

	D_RWLOCK_WRLOCK(grp_priv->gp_rwlock_ft);

	live_ranks = grp_priv_get_live_ranks(grp_priv);
	failed_ranks = grp_priv_get_failed_ranks(grp_priv);

	if (d_rank_in_rank_list(failed_ranks, rank)) {
		D_DEBUG(DB_TRACE, "Rank %d already evicted.\n", rank);
		D_RWLOCK_UNLOCK(grp_priv->gp_rwlock_ft);
		D_GOTO(out, rc = -DER_EVICTED);
	}

	tmp_rank_list.rl_nr = 1;
	tmp_rank_list.rl_ranks = &rank;

	d_rank_list_filter(&tmp_rank_list, live_ranks,
			   true /* exlude */);

	rc = d_rank_list_append(failed_ranks, rank);
	if (rc != 0) {
		D_ERROR("d_rank_list_append() failed, rc: %d\n", rc);
		D_RWLOCK_UNLOCK(grp_priv->gp_rwlock_ft);
		D_GOTO(out_cb, rc);
	}

	grp_priv->gp_membs_ver++;
	/* remove rank from sub groups */
	d_list_for_each_entry(curr_entry, &crt_grp_list, gp_link) {
		tmp_live_ranks = grp_priv_get_live_ranks(curr_entry);

		d_rank_list_filter(&tmp_rank_list, tmp_live_ranks,
				   true /* exclude */);
	}

	D_RWLOCK_UNLOCK(grp_priv->gp_rwlock_ft);

	rc = crt_grp_lc_mark_evicted(grp_priv, rank);
	if (rc != 0) {
		D_ERROR("crt_grp_lc_mark_evicted() failed, rc: %d.\n", rc);
		D_GOTO(out_cb, rc);
	}
	D_DEBUG(DB_TRACE, "evicted group %s rank %d.\n",
		grp_priv->gp_pub.cg_grpid, rank);

out_cb:
	if (grp_priv->gp_local)
		crt_barrier_handle_eviction(grp_priv);

	crt_exec_eviction_cb(&grp_priv->gp_pub, rank);
	tgt_ep.ep_grp = grp;
	tgt_ep.ep_rank = rank;
	/* ep_tag is not used in crt_ep_abort() */
	tgt_ep.ep_tag = 0;
	rc = crt_ep_abort(&tgt_ep);
	if (rc != 0)
		D_ERROR("crt_ep_abort(grp %p, rank %d) failed, rc: %d.\n", grp,
			rank, rc);

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
	D_RWLOCK_WRLOCK(&crt_plugin_gdata.cpg_eviction_rwlock);
	d_list_add_tail(&cb_priv->cp_link, &crt_plugin_gdata.cpg_eviction_cbs);
	D_RWLOCK_UNLOCK(&crt_plugin_gdata.cpg_eviction_rwlock);

out:
	return rc;
}

int
crt_register_event_cb(crt_event_cb event_handler, void *arg)
{
	struct crt_event_cb_priv	*cb_priv, *cb_priv2;
	int				 rc = DER_SUCCESS;

	D_ALLOC_PTR(cb_priv);
	if (cb_priv == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	cb_priv->cecp_func = event_handler;
	cb_priv->cecp_args = arg;

	D_RWLOCK_WRLOCK(&crt_plugin_gdata.cpg_event_rwlock);
	d_list_for_each_entry(cb_priv2, &crt_plugin_gdata.cpg_event_cbs,
			      cecp_link) {
		if (cb_priv2->cecp_func == event_handler &&
		    cb_priv2->cecp_args == arg) {
			D_FREE(cb_priv);
			rc = -DER_EXIST;
			break;
		}
	}
	if (rc == 0)
		d_list_add_tail(&cb_priv->cecp_link,
				&crt_plugin_gdata.cpg_event_cbs);
	D_RWLOCK_UNLOCK(&crt_plugin_gdata.cpg_event_rwlock);

out:
	return rc;
}

int
crt_unregister_event_cb(crt_event_cb event_handler, void *arg)
{
	struct crt_event_cb_priv	*cb_priv;
	int				 rc = -DER_NONEXIST;

	D_RWLOCK_WRLOCK(&crt_plugin_gdata.cpg_event_rwlock);
	d_list_for_each_entry(cb_priv, &crt_plugin_gdata.cpg_event_cbs,
			      cecp_link) {
		if (cb_priv->cecp_func == event_handler &&
		    cb_priv->cecp_args == arg) {
			d_list_del(&cb_priv->cecp_link);
			D_FREE(cb_priv);
			rc = DER_SUCCESS;
			break;
		}
	}
	D_RWLOCK_UNLOCK(&crt_plugin_gdata.cpg_event_rwlock);
	return rc;
}

int
crt_grp_failed_ranks_dup(crt_group_t *grp, d_rank_list_t **failed_ranks)
{
	struct crt_grp_priv	*grp_priv;
	d_rank_list_t		*grp_failed_ranks;
	int			 rc = 0;

	D_ASSERT(crt_initialized());
	D_ASSERT(failed_ranks != NULL);

	grp_priv = crt_grp_pub2priv(grp);
	D_ASSERT(grp_priv != NULL);
	if (grp_priv->gp_primary == 0) {
		D_ERROR("grp should be a primary group.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}
	D_RWLOCK_RDLOCK(grp_priv->gp_rwlock_ft);

	grp_failed_ranks = grp_priv_get_failed_ranks(grp_priv);
	rc = d_rank_list_dup(failed_ranks, grp_failed_ranks);
	D_RWLOCK_UNLOCK(grp_priv->gp_rwlock_ft);
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

int
crt_grp_psr_reload(struct crt_grp_priv *grp_priv)
{
	d_rank_t	psr_rank;
	crt_phy_addr_t	uri = NULL;
	int		rc = 0;

	psr_rank = grp_priv->gp_psr_rank;
	while (1) {
		psr_rank = (psr_rank + 1) % grp_priv->gp_size;
		if (psr_rank == grp_priv->gp_psr_rank) {
			D_ERROR("group %s no more PSR candidate.\n",
				grp_priv->gp_pub.cg_grpid);
			D_GOTO(out, rc = -DER_PROTO);
		}
		rc = crt_grp_lc_lookup(grp_priv, 0, psr_rank, 0, &uri, NULL);
		if (rc == 0) {
			if (uri == NULL)
				break;

			crt_grp_psr_set(grp_priv, psr_rank, uri);
			D_GOTO(out, rc = 0);
		} else if (rc != -DER_EVICTED) {
			/*
			 * DER_EVICTED means the psr_rank being evicted then can
			 * try next one, for other errno just treats as failure.
			 */
			D_ERROR("crt_grp_lc_lookup(grp %s, rank %d tag 0) "
				"failed, rc: %d.\n", grp_priv->gp_pub.cg_grpid,
				psr_rank, rc);
			D_GOTO(out, rc);
		}
	}

	if (crt_is_singleton()) {
		rc = crt_grp_config_psr_load(grp_priv, psr_rank);
		if (rc != 0)
			D_ERROR("crt_grp_config_psr_load(grp %s, psr_rank %d), "
				"failed, rc: %d.\n",
				grp_priv->gp_pub.cg_grpid, psr_rank, rc);
	} else {
		rc = crt_pmix_psr_load(grp_priv, psr_rank);
		if (rc != 0)
			D_ERROR("crt_pmix_psr_load(grp %s, psr_rank %d), "
				"failed, rc: %d.\n",
				grp_priv->gp_pub.cg_grpid, psr_rank, rc);
	}
out:
	return rc;
}

/* Free index list is used for tracking which indexes in the rank list
 * are unused. When rank list gets filled, we reallocate bigger size
 * and add additional indices to the free index list.
 *
 * Each time node is removed, an corresponding index is added back
 * to the free index list
 */
static int
grp_get_free_index(struct crt_grp_priv *priv)
{
	int			ret;
	struct free_index	*free_index;

	free_index = d_list_pop_entry(&priv->gp_membs.cgm_free_indices,
					struct free_index,
					fi_link);

	if (!free_index) {
		D_DEBUG(DB_ALL, "No more free indices left\n");
		return -DER_NOSPACE;
	}

	ret = free_index->fi_index;
	D_FREE(free_index);

	return ret;
}

static int
grp_add_free_index(d_list_t *list, int index, bool tail)
{
	struct free_index *free_index;

	D_ALLOC_PTR(free_index);
	if (!free_index)
		return -DER_NOMEM;

	free_index->fi_index = index;

	if (tail)
		d_list_add_tail(&free_index->fi_link, list);
	else
		d_list_add(&free_index->fi_link, list);

	return 0;
}


static int
grp_regen_linear_list(struct crt_grp_priv *grp_priv)
{
	d_rank_list_t	*membs;
	int		index;
	int		i;
	d_rank_list_t	*linear_list;
	d_rank_t	*tmp_ptr;

	membs = grp_priv->gp_membs.cgm_list;
	linear_list = grp_priv->gp_membs.cgm_linear_list;

	/* If group size changed - reallocate the list */
	if (!linear_list->rl_ranks ||
	    linear_list->rl_nr != grp_priv->gp_size) {
		D_REALLOC_ARRAY(tmp_ptr, linear_list->rl_ranks,
			grp_priv->gp_size);

		if (!tmp_ptr)
			return -DER_NOMEM;

		linear_list->rl_ranks = tmp_ptr;
		linear_list->rl_nr = grp_priv->gp_size;
	}

	index = 0;
	for (i = 0; i < membs->rl_nr; i++) {
		if (membs->rl_ranks[i] != CRT_NO_RANK) {
			linear_list->rl_ranks[index] = membs->rl_ranks[i];
			index++;

			/* We already filled whole linear array */
			if (index == grp_priv->gp_size)
				break;
		}
	}

	linear_list->rl_nr = grp_priv->gp_size;

	return 0;
}


/* This function adds node to the membership list.
 * Function should only be called once per rank, even if multiple
 * tags are added with corresponding URIs
 */
static int
grp_add_to_membs_list(struct crt_grp_priv *grp_priv, d_rank_t rank)
{
	d_rank_list_t	*membs;
	int		index;
	int		first;
	int		i;
	uint32_t	new_amount;
	d_rank_t	*tmp;
	int		rc = 0;

	membs = grp_priv->gp_membs.cgm_list;

	/* TODO: Potentially consider extra checks to ensure that the same
	 * rank is not added multiple times to the list.
	 *
	 * This however would require full traversal of the membership list
	 * making addition of new ranks inefficient.
	 */

	/* Get first unused index in membs->rl_ranks */
	index = grp_get_free_index(grp_priv);

	/* If index is not found - the list is full */
	if (index == -DER_NOSPACE) {
		/* Increase list size and add new indexes to free list */
		first = membs->rl_nr;
		new_amount = first + RANK_LIST_REALLOC_SIZE;

		D_REALLOC_ARRAY(tmp, membs->rl_ranks, new_amount);
		if (!tmp)
			D_GOTO(out, rc = -DER_NOMEM);

		membs->rl_ranks = tmp;

		membs->rl_nr = new_amount;
		for (i = first; i < first + RANK_LIST_REALLOC_SIZE; i++) {
			membs->rl_ranks[i] = CRT_NO_RANK;
			grp_add_free_index(
				&grp_priv->gp_membs.cgm_free_indices,
				i, true);
		}

		index = grp_get_free_index(grp_priv);
	}

	D_ASSERT(index >= 0);

	if (grp_priv->gp_primary) {
		rc = crt_swim_rank_add(grp_priv, rank);
		if (rc) {
			D_ERROR("crt_swim_rank_add() failed: rc=%d\n", rc);
			grp_add_free_index(&grp_priv->gp_membs.cgm_free_indices,
				   index, false);
		} else {
			membs->rl_ranks[index] = rank;
			grp_priv->gp_size++;
		}
	} else {
		membs->rl_ranks[index] = rank;
		grp_priv->gp_size++;
	}

	/* Regenerate linear list*/
	grp_regen_linear_list(grp_priv);

out:
	return rc;
}


static int
crt_group_primary_add_internal(struct crt_grp_priv *grp_priv,
				d_rank_t rank, int tag, char *uri)
{
	int i;
	int rc;

	if (!grp_priv->gp_primary) {
		D_ERROR("Only available for primary groups\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		rc = crt_grp_lc_uri_insert(grp_priv, i, rank, tag, uri);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_uri_insert() failed; rc=%d\n", rc);
			D_GOTO(out, rc);
		}
	}

	/* Only add node to membership list once, for tag 0 */
	/* TODO: This logic needs to be refactored as part of CART-517 */
	if (tag == 0) {
		D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
		rc = grp_add_to_membs_list(grp_priv, rank);
		D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
	}

out:
	return rc;
}


int
crt_rank_self_set(d_rank_t rank)
{
	int rc = 0;
	struct crt_grp_priv	*default_grp_priv;
	na_class_t		*na_class;
	na_size_t		size = CRT_ADDR_STR_MAX_LEN;
	struct crt_context	*ctx;
	char			uri_addr[CRT_ADDR_STR_MAX_LEN] = {'\0'};

	default_grp_priv = crt_gdata.cg_grp->gg_srv_pri_grp;

	if (CRT_PMIX_ENABLED()) {
		D_ERROR("This api only avaialble when PMIX is disabled\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (default_grp_priv->gp_self != CRT_NO_RANK) {
		D_ERROR("Self rank was already set to %d\n",
			default_grp_priv->gp_self);
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_RWLOCK_WRLOCK(&default_grp_priv->gp_rwlock);

	default_grp_priv->gp_self = rank;
	rc = grp_add_to_membs_list(default_grp_priv, rank);
	D_RWLOCK_UNLOCK(&default_grp_priv->gp_rwlock);

	if (rc != 0) {
		D_ERROR("grp_add_to_membs_list() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	D_RWLOCK_RDLOCK(&crt_gdata.cg_rwlock);
	d_list_for_each_entry(ctx, &crt_gdata.cg_ctx_list, cc_link) {
		na_class =  ctx->cc_hg_ctx.chc_nacla;

		rc = crt_na_class_get_addr(na_class, uri_addr, &size);
		if (rc != 0) {
			D_ERROR("crt_na_class_get_addr() failed; rc=%d\n", rc);
			D_GOTO(unlock, rc);
		}

		rc = crt_grp_lc_uri_insert_all(NULL, rank, ctx->cc_idx,
					uri_addr);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_uri_insert_all() failed; rc=%d\n",
				rc);
			D_GOTO(unlock, rc);
		}

	}

unlock:
	D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
out:
	return rc;
}


int
crt_rank_uri_get(crt_group_t *group, d_rank_t rank, int tag, char **uri_str)
{
	int rc = 0;
	struct crt_grp_priv	*grp_priv;
	crt_phy_addr_t		uri;
	hg_addr_t		hg_addr;

	if (uri_str == NULL) {
		D_ERROR("Passed uri_str is NULL\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	grp_priv = crt_grp_pub2priv(group);

	if (!grp_priv->gp_primary) {
		D_ERROR("Only available for primary groups\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (rank == grp_priv->gp_self)
		return crt_self_uri_get(tag, uri_str);

	rc = crt_grp_lc_lookup(grp_priv, 0, rank, tag, &uri, &hg_addr);
	if (rc != 0) {
		D_ERROR("crt_grp_lc_lookup failed for rank=%d tag=%d\n",
			rank, tag);
		D_GOTO(out, rc);
	}

	if (uri == NULL) {
		D_DEBUG(DB_ALL, "uri for %d:%d not found\n", rank, tag);
		D_GOTO(out, rc = -DER_OOG);
	}

	D_STRNDUP(*uri_str, uri, strlen(uri) + 1);
	if (!(*uri_str)) {
		D_ERROR("Failed to allocate uri string\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}


out:
	return rc;
}

static int
crt_group_rank_remove_internal(struct crt_grp_priv *grp_priv, d_rank_t rank)
{
	d_rank_list_t		*membs;
	d_list_t		*rlink;
	struct crt_rank_mapping *rm;
	int			i;
	int			rc = 0;

	if (grp_priv->gp_primary) {
		rlink = d_hash_rec_find(&grp_priv->gp_uri_lookup_cache,
				(void *)&rank, sizeof(rank));
		if (!rlink) {
			D_ERROR("Rank %d is not part of the group\n", rank);
			D_GOTO(out, rc = -DER_OOG);
		}

		d_hash_rec_decref(&grp_priv->gp_uri_lookup_cache, rlink);

		for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++)
			crt_grp_lc_uri_remove(grp_priv, i, rank);

		d_hash_rec_delete(&grp_priv->gp_uri_lookup_cache,
			&rank, sizeof(d_rank_t));
	} else {
		d_rank_t prim_rank;

		rlink = d_hash_rec_find(&grp_priv->gp_s2p_table,
				(void *)&rank, sizeof(rank));
		if (!rlink) {
			D_ERROR("Rank %d is not part of the group\n", rank);
			D_GOTO(out, rc = -DER_OOG);
		}

		rm = crt_rm_link2ptr(rlink);
		prim_rank = rm->rm_value;

		d_hash_rec_decref(&grp_priv->gp_s2p_table, rlink);

		d_hash_rec_delete(&grp_priv->gp_s2p_table,
				&rank, sizeof(d_rank_t));
		d_hash_rec_delete(&grp_priv->gp_p2s_table,
				&prim_rank, sizeof(d_rank_t));
	}

	membs = grp_priv->gp_membs.cgm_list;

	for (i = 0; i < membs->rl_nr; i++) {
		if (membs->rl_ranks[i] == rank) {
			membs->rl_ranks[i] = CRT_NO_RANK;
			grp_priv->gp_size--;
			grp_add_free_index(
				&grp_priv->gp_membs.cgm_free_indices,
				i, false);
			grp_regen_linear_list(grp_priv);
			rc = 0;
			break;
		}

	}
out:
	return rc;
}

static int
crt_grp_remove_from_secondaries(struct crt_grp_priv *grp_priv,
				d_rank_t rank)
{
	struct crt_grp_priv	*sec_priv;
	struct crt_rank_mapping *rm;
	d_list_t		*rlink;
	int			rc;
	int			i;

	for (i = 0; i < CRT_MAX_SEC_GRPS; i++) {
		sec_priv = grp_priv->gp_priv_sec[i];
		if (sec_priv == NULL)
			continue;

		D_RWLOCK_WRLOCK(&sec_priv->gp_rwlock);
		rlink = d_hash_rec_find(&sec_priv->gp_p2s_table,
					(void *)&rank, sizeof(rank));
		if (!rlink) {
			D_RWLOCK_UNLOCK(&sec_priv->gp_rwlock);
			continue;
		}

		rm = crt_rm_link2ptr(rlink);

		rc = crt_group_rank_remove_internal(sec_priv, rm->rm_value);
		if (rc != 0) {
			D_ERROR("crt_group_rank_remove(%s,%d) failed; rc=%d\n",
				sec_priv->gp_pub.cg_grpid, rm->rm_value, rc);
		}

		d_hash_rec_decref(&sec_priv->gp_p2s_table, rlink);
		D_RWLOCK_UNLOCK(&sec_priv->gp_rwlock);
	}

	return 0;
}

int
crt_group_rank_remove(crt_group_t *group, d_rank_t rank)
{
	struct crt_grp_priv	*grp_priv;
	int			rc = -DER_OOG;

	if (CRT_PMIX_ENABLED()) {
		D_ERROR("This api only avaialble when PMIX is disabled\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (group == NULL) {
		D_ERROR("Passed group is NULL\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	grp_priv = crt_grp_pub2priv(group);

	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	rc = crt_group_rank_remove_internal(grp_priv, rank);
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	if (rc != 0)
		D_GOTO(out, rc);

	/* If it's not a primary group - we are done */
	if (!grp_priv->gp_primary)
		D_GOTO(out, rc);

	/* Go through associated secondary groups and remove rank from them */
	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
	crt_grp_remove_from_secondaries(grp_priv, rank);
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

out:
	if (rc == 0 && grp_priv->gp_primary)
		crt_swim_rank_del(grp_priv, rank);

	return rc;
}


int
crt_group_info_get(crt_group_t *group, d_iov_t *grp_info)
{
	D_ERROR("API is currently not supported\n");
	return -DER_NOSYS;
}

int
crt_group_info_set(d_iov_t *grp_info)
{
	D_ERROR("API is currently not supported\n");
	return -DER_NOSYS;
}


int
crt_group_ranks_get(crt_group_t *group, d_rank_list_t **list)
{
	d_rank_list_t		*membs;
	struct crt_grp_priv	*grp_priv;
	int			rc = 0;

	if (group == NULL) {
		D_ERROR("Passed group is NULL\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	grp_priv = crt_grp_pub2priv(group);

	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	membs = grp_priv->gp_membs.cgm_linear_list;
	rc = d_rank_list_dup(list, membs);
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

out:
	return rc;
}

int
crt_group_view_create(crt_group_id_t srv_grpid,
			crt_group_t **ret_grp)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_grp_priv	*grp_priv = NULL;
	int			rc = 0;

	if (ret_grp == NULL) {
		D_ERROR("grp ptr is NULL\n");
		return -DER_INVAL;
	}

	grp_gdata = crt_gdata.cg_grp;

	rc = crt_grp_priv_create(&grp_priv, srv_grpid, true,
				NULL, NULL, NULL);
	if (rc != 0) {
		D_ERROR("crt_grp_priv_create(%s) failed; rc=%d\n",
			srv_grpid, rc);
		D_GOTO(out, rc);
	}

	grp_priv->gp_local = 0;
	grp_priv->gp_service = 1;
	grp_priv->gp_size = 0;
	grp_priv->gp_self = 0;

	rc = grp_priv_init_membs(grp_priv, grp_priv->gp_size);
	if (rc != 0) {
		D_ERROR("grp_priv_init_membs() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	rc = crt_grp_lc_create(grp_priv);
	if (rc != 0) {
		D_ERROR("crt_grp_lc_create() failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = crt_grp_ras_init(grp_priv);
	if (rc != 0) {
		D_ERROR("crt_grp_ras_init() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	*ret_grp = &grp_priv->gp_pub;

	/* decref done in crt_group_view_destroy() */
	crt_grp_priv_addref(grp_priv);

	D_RWLOCK_WRLOCK(&grp_gdata->gg_rwlock);
	d_list_add_tail(&grp_priv->gp_link, &grp_gdata->gg_srv_grps_attached);
	D_RWLOCK_UNLOCK(&grp_gdata->gg_rwlock);
out:
	if (rc != 0 && grp_priv) {
		/* Note: This will perform all cleanups required */
		crt_grp_priv_destroy(grp_priv);
	}

	return rc;
}

int
crt_group_view_destroy(crt_group_t *grp)
{
	struct crt_grp_priv	*grp_priv;
	int			rc = 0;

	if (!grp) {
		D_ERROR("Null grp handle passed\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	grp_priv = container_of(grp, struct crt_grp_priv, gp_pub);

	rc = crt_grp_priv_decref(grp_priv);
	if (rc != 0) {
		D_ERROR("crt_grp_priv_decref() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
	}

out:
	return rc;
}

int
crt_group_psr_set(crt_group_t *grp, d_rank_t rank)
{
	struct crt_grp_priv	*grp_priv;
	char			*uri;
	int			rc = 0;

	if (!grp) {
		D_ERROR("Passed grp is NULL\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	grp_priv = container_of(grp, struct crt_grp_priv, gp_pub);

	rc = crt_rank_uri_get(grp, rank, 0, &uri);
	if (rc != 0) {
		D_ERROR("crt_rank_uri_get() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	crt_grp_psr_set(grp_priv, rank, uri);
	D_FREE(uri);

out:
	return rc;
}


int
crt_group_secondary_create(crt_group_id_t grp_name, crt_group_t *primary_grp,
				d_rank_list_t *ranks, crt_group_t **ret_grp)
{
	struct crt_grp_priv	*grp_priv = NULL;
	struct crt_grp_priv	*grp_priv_prim = NULL;
	int			rc = 0;
	int			i;
	bool			found;

	if (ret_grp == NULL) {
		D_ERROR("grp ptr is NULL\n");
		return -DER_INVAL;
	}

	grp_priv_prim = crt_grp_pub2priv(primary_grp);

	if (grp_priv_prim == NULL) {
		D_ERROR("Invalid primary group\n");
		return -DER_INVAL;
	}

	if (!grp_priv_prim->gp_primary) {
		D_ERROR("Passed group %s is not primary\n",
			primary_grp->cg_grpid);
		return -DER_INVAL;
	}

	rc = crt_grp_priv_create(&grp_priv, grp_name, false,
				NULL, NULL, NULL);
	if (rc != 0) {
		D_ERROR("crt_grp_priv_create(%s) failed; rc=%d\n",
			grp_name, rc);
		D_GOTO(out, rc);
	}

	grp_priv->gp_local = 0;
	grp_priv->gp_service = 1;
	grp_priv->gp_size = 0;
	grp_priv->gp_self = 0;

	grp_priv->gp_int_grpid = crt_get_subgrp_id();

	crt_grp_ras_init(grp_priv);

	rc = grp_priv_init_membs(grp_priv, grp_priv->gp_size);
	if (rc != 0) {
		D_ERROR("grp_priv_init_membs() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	/* URI lookup table here stores secondary ranks instead of addrs */
	rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK,
					CRT_LOOKUP_CACHE_BITS,
					NULL, &rank_mapping_ops,
					&grp_priv->gp_p2s_table);
	if (rc != 0) {
		D_ERROR("d_hash_table_create failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK,
					CRT_LOOKUP_CACHE_BITS,
					NULL, &rank_mapping_ops,
					&grp_priv->gp_s2p_table);
	if (rc != 0) {
		D_ERROR("d_hash_table_create failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

	found = false;
	/* Record secondary group in the primary group */
	for (i = 0; i < CRT_MAX_SEC_GRPS; i++) {
		if (grp_priv_prim->gp_priv_sec[i] == NULL) {
			found = true;
			grp_priv_prim->gp_priv_sec[i] = grp_priv;
			break;
		}
	}
	if (!found) {
		D_ERROR("Exceeded secondary groups limit\n");
		D_GOTO(out, rc = -DER_NONEXIST);
	}
	/*
	 * Record primary group in the secondary group. Note that this field
	 * controls whether crt_grp_priv_destroy attempts to remove this
	 * secondary group from grp_priv_prim->gp_priv_sec.
	 */
	grp_priv->gp_priv_prim = grp_priv_prim;

	*ret_grp = &grp_priv->gp_pub;

	crt_grp_priv_addref(grp_priv);

	D_RWLOCK_WRLOCK(&crt_grp_list_rwlock);
	crt_grp_insert_locked(grp_priv);
	D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);

	if (ranks == NULL)
		D_GOTO(out, rc);

	for (i = 0; i < ranks->rl_nr; i++) {
		rc = crt_group_secondary_rank_add(*ret_grp, i,
						ranks->rl_ranks[i]);
		if (rc != 0) {
			D_ERROR("Failed to add rank %d : %d to the group\n",
				i, ranks->rl_ranks[i]);
			D_GOTO(out, rc);
		}
	}

out:
	if (rc != 0 && grp_priv)
		crt_grp_priv_destroy(grp_priv);

	return rc;
}

/*
 * TODO: This is a temporary function until switch to non-PMIX
 * mode is complete. At that point this function will be
 * replaced by the generic crt_group_destroy().
 */
int
crt_group_secondary_destroy(crt_group_t *grp)
{
	struct crt_grp_priv	*grp_priv;
	int			rc = 0;

	if (!grp) {
		D_ERROR("Null grp handle passed\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	grp_priv = container_of(grp, struct crt_grp_priv, gp_pub);

	rc = crt_grp_priv_decref(grp_priv);
	if (rc != 0) {
		D_ERROR("crt_grp_priv_decref() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
	}

out:
	return rc;
}

d_rank_t
crt_grp_priv_get_primary_rank(struct crt_grp_priv *priv, d_rank_t rank)
{
	d_list_t		*rlink;
	struct crt_rank_mapping	*rm;
	d_rank_t		pri_rank;

	if (priv->gp_primary)
		return rank;

	if (CRT_PMIX_ENABLED()) {
		D_ASSERT(rank < priv->gp_membs.cgm_list->rl_nr);
		return priv->gp_membs.cgm_list->rl_ranks[rank];
	}

	/* Secondary groups, PMIX disabled */
	rlink = d_hash_rec_find(&priv->gp_s2p_table,
				(void *)&rank, sizeof(rank));
	if (!rlink)
		return CRT_NO_RANK;

	rm = crt_rm_link2ptr(rlink);
	pri_rank = rm->rm_value;

	d_hash_rec_decref(&priv->gp_s2p_table, rlink);

	return pri_rank;
}

static struct crt_rank_mapping *
crt_rank_mapping_init(d_rank_t key, d_rank_t value)
{
	struct crt_rank_mapping *rm;
	int			rc;

	D_ALLOC_PTR(rm);
	if (!rm) {
		D_ERROR("Failed to allocate rm item\n");
		D_GOTO(out, rm);
	}

	D_INIT_LIST_HEAD(&rm->rm_link);
	rm->rm_ref = 0;
	rm->rm_initialized = 1;

	rc = D_MUTEX_INIT(&rm->rm_mutex, NULL);
	if (rc != 0) {
		D_FREE_PTR(rm);
		D_GOTO(out, rm = NULL);
	}

	rm->rm_key = key;
	rm->rm_value = value;

out:
	return rm;
}

static int
crt_group_secondary_rank_add_internal(struct crt_grp_priv *grp_priv,
				d_rank_t sec_rank, d_rank_t prim_rank)
{
	struct crt_rank_mapping *rm_p2s;
	struct crt_rank_mapping *rm_s2p;
	d_list_t		*rlink;
	d_rank_list_t		*prim_membs;
	int			rc = 0;

	/* Verify passed primary rank is valid */
	prim_membs = grp_priv_get_membs(grp_priv->gp_priv_prim);
	if (!d_rank_in_rank_list(prim_membs, prim_rank)) {
		D_ERROR("rank %d is not part of associated primary group %s\n",
			prim_rank, grp_priv->gp_priv_prim->gp_pub.cg_grpid);
		D_GOTO(out, rc = -DER_OOG);
	}

	if (prim_rank == grp_priv->gp_priv_prim->gp_self) {
		D_DEBUG(DB_ALL, "Setting rank %d as self rank for grp %s\n",
			sec_rank, grp_priv->gp_pub.cg_grpid);
		grp_priv->gp_self = sec_rank;
	}

	/* Verify secondary rank isn't already added */
	rlink = d_hash_rec_find(&grp_priv->gp_s2p_table,
				(void *)&sec_rank, sizeof(sec_rank));
	if (rlink != NULL) {
		D_ERROR("Entry for secondary_rank = %d already exists\n",
			sec_rank);
		d_hash_rec_decref(&grp_priv->gp_s2p_table, rlink);
		D_GOTO(out, rc = -DER_EXIST);
	}

	/* Add entry to lookup table. Secondary group table contains ranks */
	rm_s2p = crt_rank_mapping_init(sec_rank, prim_rank);
	if (!rm_s2p) {
		D_ERROR("Failed to allocate entry\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	rm_p2s = crt_rank_mapping_init(prim_rank, sec_rank);
	if (!rm_p2s) {
		D_ERROR("Failed to allocate entry\n");
		crt_rm_destroy(rm_s2p);
		D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = d_hash_rec_insert(&grp_priv->gp_s2p_table,
				&sec_rank, sizeof(sec_rank),
				&rm_s2p->rm_link, true);
	if (rc != 0) {
		D_ERROR("Failed to add entry; rc=%d\n", rc);
		crt_rm_destroy(rm_s2p);
		crt_rm_destroy(rm_p2s);
		D_GOTO(out, rc);
	}

	rc = d_hash_rec_insert(&grp_priv->gp_p2s_table,
				&prim_rank, sizeof(prim_rank),
				&rm_p2s->rm_link, true);
	if (rc != 0) {
		D_ERROR("Failed to add entry; rc=%d\n", rc);
		d_hash_rec_delete(&grp_priv->gp_s2p_table,
				&sec_rank, sizeof(sec_rank));
		crt_rm_destroy(rm_p2s);
		D_GOTO(out, rc);
	}

	/* Add secondary rank to membership list  */
	rc = grp_add_to_membs_list(grp_priv, sec_rank);
	if (rc != 0) {
		d_hash_rec_delete(&grp_priv->gp_s2p_table,
				&sec_rank, sizeof(sec_rank));

		d_hash_rec_delete(&grp_priv->gp_s2p_table,
				&prim_rank, sizeof(prim_rank));
		D_GOTO(out, rc);
	}

out:
	return rc;
}

int
crt_group_secondary_rank_add(crt_group_t *grp, d_rank_t sec_rank,
				d_rank_t prim_rank)
{
	struct crt_grp_priv	*grp_priv;
	int			rc = 0;

	grp_priv = crt_grp_pub2priv(grp);

	if (grp_priv->gp_primary) {
		D_ERROR("Passed group is a primary group\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (grp_priv->gp_priv_prim == NULL) {
		D_ERROR("Associated primary group not found\n");
		D_GOTO(out, rc = -DER_INVAL);
	}


	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	rc = crt_group_secondary_rank_add_internal(grp_priv,
					sec_rank, prim_rank);
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

out:
	return rc;
}


int crt_group_primary_rank_add(crt_context_t ctx, crt_group_t *grp,
			d_rank_t prim_rank, char *uri)
{
	struct crt_grp_priv	*grp_priv;
	int			rc = 0;

	grp_priv = crt_grp_pub2priv(grp);

	if (!grp_priv->gp_primary) {
		D_ERROR("Passed group is not primary group\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_group_primary_add_internal(grp_priv, prim_rank, 0, uri);

out:
	return rc;
}

/*
 * Helper function to return back rank lists of nodes to add,
 * to remove, and index of ranks to be added.
 *
 * Index of ranks to add is used for accessing proper URIs
 * when adding new ranks
 */
static int
crt_group_mod_get(d_rank_list_t *grp_membs, d_rank_list_t *mod_membs,
		crt_group_mod_op_t op, d_rank_list_t **ret_to_add,
		d_rank_list_t **ret_to_remove, uint32_t **ret_idx_to_add)

{
	d_rank_t	rank;
	int		rc = 0;
	d_rank_list_t	*to_add = NULL;
	d_rank_list_t	*to_remove = NULL;
	uint32_t	*idx_to_add = NULL;
	int		i;

	D_ASSERT(grp_membs != NULL);
	D_ASSERT(mod_membs != NULL);
	D_ASSERT(ret_to_add != NULL);
	D_ASSERT(ret_to_remove != NULL);
	D_ASSERT(ret_idx_to_add != NULL);

	/* At most we will remove all members from old group */
	to_remove = d_rank_list_alloc(grp_membs->rl_nr);

	/* At most we will add all members from new group */
	to_add = d_rank_list_alloc(mod_membs->rl_nr);

	if (to_remove == NULL || to_add == NULL) {
		D_ERROR("Failed to allocate lists\n");
		D_GOTO(cleanup, rc = -DER_NOMEM);
	}

	/* Array will have at most 'mod_membs' elements */
	D_ALLOC_ARRAY(idx_to_add, mod_membs->rl_nr);
	if (!idx_to_add) {
		D_ERROR("Failed to allocate array\n");
		D_GOTO(cleanup, rc = -DER_NOMEM);
	}

	to_add->rl_nr = 0;
	to_remove->rl_nr = 0;
	/* Build to_add and to_remove lists based on op specified */
	if (op == CRT_GROUP_MOD_OP_REPLACE) {
		/*
		 * Replace:
		 * If rank exists in mod_membs but not in grp_membs - add it
		 * If rank exists in grp_membs but not in mod_membs - remove it
		 * Otherwise leave rank unchanged
		 */

		/* Build list of new ranks to add */
		for (i = 0; i < mod_membs->rl_nr; i++) {
			rank = mod_membs->rl_ranks[i];

			if (d_rank_in_rank_list(grp_membs, rank) == false) {
				idx_to_add[to_add->rl_nr] = i;
				to_add->rl_ranks[to_add->rl_nr++] = rank;
			}
		}

		/* Build list of ranks to remove */
		for (i = 0; i < grp_membs->rl_nr; i++) {
			rank = grp_membs->rl_ranks[i];

			if (d_rank_in_rank_list(mod_membs, rank) == false)
				to_remove->rl_ranks[to_remove->rl_nr++] = rank;
		}
	} else if (op == CRT_GROUP_MOD_OP_ADD) {
		/* Build list of ranks to add; nothing to remove */
		for (i = 0; i < mod_membs->rl_nr; i++) {
			rank = mod_membs->rl_ranks[i];

			if (d_rank_in_rank_list(grp_membs, rank) == false) {
				idx_to_add[to_add->rl_nr] = i;
				to_add->rl_ranks[to_add->rl_nr++] = rank;
			}
		}

		to_remove->rl_nr = 0;

	} else if (op == CRT_GROUP_MOD_OP_REMOVE) {
		/* Build list of ranks to remove; nothing to add */
		for (i = 0; i < mod_membs->rl_nr; i++) {
			rank = mod_membs->rl_ranks[i];

			if (d_rank_in_rank_list(grp_membs, rank) == true) {
				to_remove->rl_ranks[to_remove->rl_nr++] = rank;
			}
		}

		to_add->rl_nr = 0;
	} else {
		D_ERROR("Should never get here\n");
		D_ASSERT(0);
	}

	if (to_add->rl_nr == 0 && to_remove->rl_nr == 0)
		D_WARN("Membership unchanged. No modification performed\n");

	*ret_to_add = to_add;
	*ret_to_remove = to_remove;
	*ret_idx_to_add = idx_to_add;

	return rc;

cleanup:
	if (to_add != NULL)
		d_rank_list_free(to_add);

	if (to_remove != NULL)
		d_rank_list_free(to_remove);

	D_FREE(idx_to_add);
	return rc;
}

/*
 * 'uris' is an array of uris; expected to be of size ranks->rl_nr * num_ctxs
 * In the case of single provider num_ctxs=1,
 * For multi-provider support contexts for each provider are to be passed
 * In multi-provider support uris should be formed as:
 * [uri0 for provider0]
 * [uri1 for provider0]
 * ....
 * [uriX for provider0]
 * [uri0 for provider1]
 * [uri1 for provider1]
 * ...
 * [uriX for provider1]
 * [uri0 for provider2]
 * etc...
 */
int
crt_group_primary_modify(crt_group_t *grp, crt_context_t *ctxs, int num_ctxs,
			d_rank_list_t *ranks, char **uris,
			crt_group_mod_op_t op, uint32_t version)
{
	struct crt_grp_priv	*grp_priv;
	d_rank_list_t		*grp_membs;
	d_rank_list_t		*to_remove;
	d_rank_list_t		*to_add;
	uint32_t		*uri_idx;
	d_rank_t		rank;
	int			k;
	int			i;
	int			rc = 0;

	grp_priv = crt_grp_pub2priv(grp);

	if (grp_priv == NULL) {
		D_ERROR("Failed to get grp_priv\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (!grp_priv->gp_primary) {
		D_ERROR("Passed group is not primary\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (op >= CRT_GROUP_MOD_OP_COUNT) {
		D_ERROR("Invalid operation %d\n", op);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (ranks == NULL || ranks->rl_nr == 0 || ranks->rl_ranks == NULL) {
		D_ERROR("Modification has no members\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (op != CRT_GROUP_MOD_OP_REMOVE && uris == NULL) {
		D_ERROR("URI array is null\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);

	grp_membs = grp_priv_get_membs(grp_priv);

	/* Get back list of nodes to add, to remove and uri index list */
	rc = crt_group_mod_get(grp_membs, ranks, op, &to_add, &to_remove,
			&uri_idx);
	if (rc != 0)
		D_GOTO(unlock, rc);

	/* Add ranks based on to_add list */
	for (i = 0; i < to_add->rl_nr; i++) {
		rank = to_add->rl_ranks[i];

		rc = grp_add_to_membs_list(grp_priv, rank);
		if (rc != 0) {
			D_ERROR("grp_add_to_memb_list %d failed; rc=%d\n",
				rank, rc);
			D_GOTO(cleanup, rc);
		}

		/* TODO: Change for multi-provider support */
		for (k = 0; k < CRT_SRV_CONTEXT_NUM; k++) {
			rc = grp_lc_uri_insert_internal_locked(grp_priv,
				k, rank, 0, uris[uri_idx[i]]);

			if (rc != 0)
				D_GOTO(cleanup, rc);
		}
	}

	/* Remove ranks based on to_remove list */
	for (i = 0; i < to_remove->rl_nr; i++) {
		rank = to_remove->rl_ranks[i];
		crt_group_rank_remove_internal(grp_priv, rank);

		/* Remove rank from associated secondary groups */
		crt_grp_remove_from_secondaries(grp_priv, rank);

		/* Remove rank from swim tracking */
		crt_swim_rank_del(grp_priv, rank);
	}

	d_rank_list_free(to_add);
	d_rank_list_free(to_remove);
	D_FREE(uri_idx);

	grp_priv->gp_membs_ver = version;
unlock:
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

out:
	return rc;

cleanup:

	D_ERROR("Failure when adding node %d, rc=%d\n",
		to_add->rl_ranks[i], rc);

	for (k = 0; k < i; k++)
		crt_group_rank_remove_internal(grp_priv, to_add->rl_ranks[k]);

	d_rank_list_free(to_add);
	d_rank_list_free(to_remove);
	D_FREE(uri_idx);

	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	return rc;
}


int
crt_group_secondary_modify(crt_group_t *grp, d_rank_list_t *sec_ranks,
			d_rank_list_t *prim_ranks, crt_group_mod_op_t op,
			uint32_t version)
{
	struct crt_grp_priv	*grp_priv;
	d_rank_list_t		*grp_membs;
	d_rank_list_t		*to_remove;
	d_rank_list_t		*to_add;
	uint32_t		*prim_idx;
	d_rank_t		rank;
	int			k;
	int			i;
	int			rc = 0;

	grp_priv = crt_grp_pub2priv(grp);

	if (grp_priv == NULL) {
		D_ERROR("Failed to get grp_priv\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (grp_priv->gp_primary) {
		D_ERROR("Passed group is primary\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (op >= CRT_GROUP_MOD_OP_COUNT) {
		D_ERROR("Invalid operation %d\n", op);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (sec_ranks == NULL || sec_ranks->rl_nr == 0 ||
		sec_ranks->rl_ranks == NULL) {

		D_ERROR("Modification has no members\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (op != CRT_GROUP_MOD_OP_REMOVE) {
		if (prim_ranks == NULL || prim_ranks->rl_nr == 0 ||
			prim_ranks->rl_ranks == NULL) {
			D_ERROR("Primary rank list is empty\n");
			D_GOTO(out, rc = -DER_INVAL);
		}

		if (sec_ranks->rl_nr != prim_ranks->rl_nr) {
			D_ERROR("Prim list size=%d differs from sec=%d\n",
				prim_ranks->rl_nr, sec_ranks->rl_nr);
			D_GOTO(out, rc = -DER_INVAL);
		}
	}

	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);

	grp_membs = grp_priv_get_membs(grp_priv);

	/* Get back list of nodes to add, to remove and primary rank list */
	rc = crt_group_mod_get(grp_membs, sec_ranks, op, &to_add, &to_remove,
			&prim_idx);
	if (rc != 0)
		D_GOTO(unlock, rc);

	/* Add ranks based on to_add list */
	for (i = 0; i < to_add->rl_nr; i++) {
		rc = crt_group_secondary_rank_add_internal(grp_priv,
					to_add->rl_ranks[i],
					prim_ranks->rl_ranks[prim_idx[i]]);
		if (rc != 0)
			D_GOTO(cleanup, rc);
	}

	/* Remove ranks based on to_remove list */
	for (i = 0; i < to_remove->rl_nr; i++) {
		rank = to_remove->rl_ranks[i];
		crt_group_rank_remove_internal(grp_priv, rank);
	}

	d_rank_list_free(to_add);
	d_rank_list_free(to_remove);
	D_FREE(prim_idx);

	grp_priv->gp_membs_ver = version;
unlock:
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

out:
	return rc;

cleanup:

	D_ERROR("Failure when adding rank %d, rc=%d\n",
		to_add->rl_ranks[i], rc);

	for (k = 0; k < i; k++)
		crt_group_rank_remove_internal(grp_priv, to_add->rl_ranks[k]);

	d_rank_list_free(to_add);
	d_rank_list_free(to_remove);
	D_FREE(prim_idx);

	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	return rc;
}
