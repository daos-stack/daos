/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It implements the main group APIs.
 */
#define D_LOGFAC	DD_FAC(grp)

#include <sys/types.h>
#include <sys/stat.h>
#include "crt_internal.h"

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
	D_FREE(li);
}

struct crt_lookup_item *
crt_li_link2ptr(d_list_t *rlink)
{
	D_ASSERT(rlink != NULL);
	return container_of(rlink, struct crt_lookup_item, li_link);
}

static uint32_t
li_op_key_hash(struct d_hash_table *hhtab, const void *key, unsigned int ksize)
{
	D_ASSERT(ksize == sizeof(d_rank_t));

	return (uint32_t)(*(const uint32_t *)key
			  & ((1U << CRT_LOOKUP_CACHE_BITS) - 1));
}

static bool
li_op_key_cmp(struct d_hash_table *hhtab, d_list_t *rlink,
	      const void *key, unsigned int ksize)
{
	struct crt_lookup_item *li = crt_li_link2ptr(rlink);

	D_ASSERT(ksize == sizeof(d_rank_t));

	return li->li_rank == *(d_rank_t *)key;
}

static uint32_t
li_op_rec_hash(struct d_hash_table *htable, d_list_t *link)
{
	struct crt_lookup_item *li = crt_li_link2ptr(link);

	return (uint32_t)li->li_rank & ((1U << CRT_LOOKUP_CACHE_BITS) - 1);
}

static void
li_op_rec_addref(struct d_hash_table *hhtab, d_list_t *rlink)
{
	struct crt_lookup_item *li = crt_li_link2ptr(rlink);

	D_ASSERT(li->li_initialized);
	atomic_fetch_add(&li->li_ref, 1);
}

static bool
li_op_rec_decref(struct d_hash_table *hhtab, d_list_t *rlink)
{
	struct crt_lookup_item *li = crt_li_link2ptr(rlink);

	D_ASSERT(li->li_initialized);
	return atomic_fetch_sub(&li->li_ref, 1) == 1;
}

static void
li_op_rec_free(struct d_hash_table *hhtab, d_list_t *rlink)
{
	crt_li_destroy(crt_li_link2ptr(rlink));
}

static d_hash_table_ops_t lookup_table_ops = {
	.hop_key_hash		= li_op_key_hash,
	.hop_key_cmp		= li_op_key_cmp,
	.hop_rec_hash		= li_op_rec_hash,
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

static uint32_t
rm_op_key_hash(struct d_hash_table *hhtab, const void *key, unsigned int ksize)
{
	D_ASSERT(ksize == sizeof(d_rank_t));

	return (uint32_t)(*(const uint32_t *)key
			  & ((1U << CRT_LOOKUP_CACHE_BITS) - 1));
}

static bool
rm_op_key_cmp(struct d_hash_table *hhtab, d_list_t *rlink,
	      const void *key, unsigned int ksize)
{
	struct crt_rank_mapping *rm = crt_rm_link2ptr(rlink);

	D_ASSERT(ksize == sizeof(d_rank_t));

	return rm->rm_key == *(d_rank_t *)key;
}

static uint32_t
rm_op_rec_hash(struct d_hash_table *htable, d_list_t *link)
{
	struct crt_rank_mapping *rm = crt_rm_link2ptr(link);

	return (uint32_t)rm->rm_key & ((1U << CRT_LOOKUP_CACHE_BITS) - 1);
}

static void
rm_op_rec_addref(struct d_hash_table *hhtab, d_list_t *rlink)
{
	struct crt_rank_mapping *rm = crt_rm_link2ptr(rlink);

	D_ASSERT(rm->rm_initialized);
	atomic_fetch_add(&rm->rm_ref, 1);
}

static bool
rm_op_rec_decref(struct d_hash_table *hhtab, d_list_t *rlink)
{
	struct crt_rank_mapping *rm = crt_rm_link2ptr(rlink);

	D_ASSERT(rm->rm_initialized);
	return atomic_fetch_sub(&rm->rm_ref, 1) == 1;
}

static void
crt_rm_destroy(struct crt_rank_mapping *rm)
{
	D_ASSERT(rm != NULL);
	D_ASSERT(rm->rm_ref == 0);
	D_ASSERT(rm->rm_initialized == 1);

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

static uint32_t
ui_op_key_hash(struct d_hash_table *hhtab, const void *key, unsigned int ksize)
{
	D_ASSERT(ksize == sizeof(d_rank_t));

	return (uint32_t)(*(const uint32_t *)key
			  & ((1U << CRT_LOOKUP_CACHE_BITS) - 1));
}

static bool
ui_op_key_cmp(struct d_hash_table *hhtab, d_list_t *rlink,
	      const void *key, unsigned int ksize)
{
	struct crt_uri_item *ui = crt_ui_link2ptr(rlink);

	D_ASSERT(ksize == sizeof(d_rank_t));

	return ui->ui_rank == *(d_rank_t *)key;
}

static uint32_t
ui_op_rec_hash(struct d_hash_table *htable, d_list_t *link)
{
	struct crt_uri_item *ui = crt_ui_link2ptr(link);

	return (uint32_t)ui->ui_rank & ((1U << CRT_LOOKUP_CACHE_BITS) - 1);
}

static void
ui_op_rec_addref(struct d_hash_table *hhtab, d_list_t *rlink)
{
	struct crt_uri_item *ui = crt_ui_link2ptr(rlink);

	D_ASSERT(ui->ui_initialized);
	atomic_fetch_add(&ui->ui_ref, 1);
}

static bool
ui_op_rec_decref(struct d_hash_table *hhtab, d_list_t *rlink)
{
	struct crt_uri_item *ui = crt_ui_link2ptr(rlink);

	D_ASSERT(ui->ui_initialized);
	return atomic_fetch_sub(&ui->ui_ref, 1) == 1;
}

static void
crt_ui_destroy(struct crt_uri_item *ui)
{
	int	i;

	D_ASSERT(ui != NULL);
	D_ASSERT(ui->ui_ref == 0);
	D_ASSERT(ui->ui_initialized == 1);

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++)
		D_FREE(ui->ui_uri[i]);

	D_FREE(ui);
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

	return atomic_load_relaxed(&ui->ui_uri[tag]);
}

static int
generate_cxi_uris(int prov_type, char *addr, int tag, struct crt_uri_item *ui)
{
	char		tmp_addr[CRT_ADDR_STR_MAX_LEN + 1] = {0};
	int		i, k;
	uint32_t	raw_addr;
	uint32_t	raw_tag0_addr;
	int		rc = 0;
	int		parsed = 0;
	char		*prov_name;

	strncpy(tmp_addr, addr, CRT_ADDR_STR_MAX_LEN);

	parsed = sscanf(tmp_addr, "0x%x", &raw_addr);
	if (parsed != 1) {
		D_ERROR("Failed to parse address '%s'\n", tmp_addr);
		return -DER_INVAL;
	}

	/* TODO: Perform proper parsing of CXI addresses */
	raw_tag0_addr = raw_addr - tag;
	prov_name = crt_provider_name_get(prov_type);

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		char *tag_uri = NULL;

		D_ASPRINTF(tag_uri, "%s://0x%x", prov_name, raw_tag0_addr + i);

		if (tag_uri == NULL) {
			for (k = 0; k < i; k++)
				D_FREE(ui->ui_uri[k]);

			D_FREE(ui);
			D_GOTO(exit, rc = -DER_NOMEM);
		}

		ui->ui_uri[i] = tag_uri;
	}

exit:
	return rc;
}

static int
generate_port_based_uris(int prov_type, char *base_addr, int tag, struct crt_uri_item *ui)
{
	char	tmp_addr[CRT_ADDR_STR_MAX_LEN + 1] = {0};
	int	base_port;
	char	*p;
	int	i, k;
	char	*prov_name;
	int	rc = 0;

	strncpy(tmp_addr, base_addr, CRT_ADDR_STR_MAX_LEN);

	 /*
	 * Port-based providers have form of string:port
	 * Parse both parts out
	 */
	p = strrchr(tmp_addr, ':');
	if (p == NULL) {
		D_ERROR("Badly formed ADDR '%s'\n", tmp_addr);
		D_GOTO(exit, rc = -DER_INVAL);
	}

	/* Split <string> from <port> part in URI */
	*p = '\0';
	p++;
	base_port = atoi(p) - tag;

	if (base_port <= 0) {
		D_ERROR("Failed to parse addr=%s correctly\n", tmp_addr);
		D_GOTO(exit, rc = -DER_INVAL);
	}

	prov_name = crt_provider_name_get(prov_type);

	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		char *tag_uri = NULL;

		D_ASPRINTF(tag_uri, "%s://%s:%d", prov_name, tmp_addr, base_port + i);

		if (tag_uri == NULL) {
			for (k = 0; k < i; k++)
				D_FREE(ui->ui_uri[k]);

			D_FREE(ui);
			D_GOTO(exit, rc = -DER_NOMEM);
		}

		ui->ui_uri[i] = tag_uri;
	}

exit:
	return rc;
}

static inline int
grp_li_uri_set(struct crt_lookup_item *li, int tag, const char *uri)
{
	char			base_addr[CRT_ADDR_STR_MAX_LEN];
	struct crt_uri_item	*ui;
	d_list_t		*rlink;
	struct crt_grp_priv	*grp_priv;
	crt_phy_addr_t		nul_str = NULL;
	crt_phy_addr_t		uri_dup;
	d_rank_t		rank;
	int			rc = 0;
	int			i;
	crt_provider_t		provider;

	rank = li->li_rank;
	grp_priv = li->li_grp_priv;

	rlink = d_hash_rec_find(&grp_priv->gp_uri_lookup_cache,
				(void *)&rank, sizeof(rank));
	if (rlink == NULL) {
		D_ALLOC_PTR(ui);
		if (!ui)
			D_GOTO(exit, rc = -DER_NOMEM);

		D_INIT_LIST_HEAD(&ui->ui_link);
		ui->ui_ref = 0;
		ui->ui_initialized = 1;
		ui->ui_rank = li->li_rank;

		rc = crt_hg_parse_uri(uri, &provider, base_addr);
		if (rc)
			D_GOTO(exit, rc);

		D_DEBUG(DB_NET, "Parsed uri '%s', base_addr='%s' prov=%d\n",
			uri, base_addr, provider);

		if (crt_provider_is_contig_ep(provider)) {
			if (crt_provider_is_port_based(provider)) {
				rc = generate_port_based_uris(provider, base_addr, tag, ui);
			} else if (provider == CRT_PROV_OFI_CXI) {
				rc = generate_cxi_uris(provider, base_addr, tag, ui);
			} else {
				/*
				 * TODO: implement generate_opx_uris() function. Once done OPX
				 * 'contig_ep' setting should be set to true
				 */
				D_ERROR("Unknown provider %d for uri='%s'\n", provider, uri);
				rc = -DER_INVAL;
			}

			if (rc)
				D_GOTO(exit, rc);
		} else {
			D_STRNDUP(ui->ui_uri[tag], uri, CRT_ADDR_STR_MAX_LEN);
			if (!ui->ui_uri[tag]) {
				D_FREE(ui);
				D_GOTO(exit, rc = -DER_NOMEM);
			}
		}

		rc = d_hash_rec_insert(&grp_priv->gp_uri_lookup_cache,
				       &rank, sizeof(rank),
				       &ui->ui_link,
				       true /* exclusive */);
		if (rc != 0) {
			D_ERROR("Entry already present\n");

			if (crt_provider_is_contig_ep(provider)) {
				for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++)
					D_FREE(ui->ui_uri[i]);
			} else {
				D_FREE(ui->ui_uri[tag]);
			}
			D_FREE(ui);
			D_GOTO(exit, rc);
		}
	} else {
		ui = crt_ui_link2ptr(rlink);
		if (ui->ui_uri[tag] == NULL) {
			D_STRNDUP(uri_dup, uri, CRT_ADDR_STR_MAX_LEN);
			if (uri_dup) {
				if (!atomic_compare_exchange(&ui->ui_uri[tag],
							     nul_str, uri_dup))
					D_FREE(uri_dup);
			} else {
				rc = -DER_NOMEM;
			}
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
	.hop_key_hash		= ui_op_key_hash,
	.hop_key_cmp		= ui_op_key_cmp,
	.hop_rec_hash		= ui_op_rec_hash,
	.hop_rec_addref		= ui_op_rec_addref,
	.hop_rec_decref		= ui_op_rec_decref,
	.hop_rec_free		= ui_op_rec_free,
};

static d_hash_table_ops_t rank_mapping_ops = {
	.hop_key_hash		= rm_op_key_hash,
	.hop_key_cmp		= rm_op_key_cmp,
	.hop_rec_hash		= rm_op_rec_hash,
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
			D_ERROR("d_hash_table_create() failed, " DF_RC "\n",
				DP_RC(rc));
			D_GOTO(free_htables, rc);
		}
	}
	grp_priv->gp_lookup_cache = htables;

	rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK,
					 CRT_LOOKUP_CACHE_BITS,
					 NULL, &uri_lookup_table_ops,
					 &grp_priv->gp_uri_lookup_cache);
	if (rc != 0) {
		D_ERROR("d_hash_table_create() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(free_htables, rc);
	}

	return 0;

free_htables:
	for (j = 0; j < i; j++) {
		rc2 = d_hash_table_destroy_inplace(&htables[j],
						   true /* force */);
		if (rc2 != 0)
			D_ERROR("d_hash_table_destroy() failed, " DF_RC "\n",
				DP_RC(rc2));
	}
	D_FREE(htables);
	grp_priv->gp_lookup_cache = NULL;

out:
	if (rc != 0)
		D_ERROR("failed, " DF_RC "\n", DP_RC(rc));

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
			D_ERROR("d_hash_table_destroy() failed, " DF_RC "\n",
				DP_RC(rc2));
			rc = rc ? rc : rc2;
		}
	}
	D_FREE(grp_priv->gp_lookup_cache);

	rc2 = d_hash_table_destroy_inplace(&grp_priv->gp_uri_lookup_cache,
					   true /* force */);
	if (rc2 != 0) {
		D_ERROR("d_hash_table_destroy() failed, " DF_RC "\n",
			DP_RC(rc2));
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
				  uint32_t tag,	const char *uri)
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
crt_grp_lc_uri_insert(struct crt_grp_priv *passed_grp_priv,
		      d_rank_t rank, uint32_t tag, const char *uri)
{
	struct crt_grp_priv	*grp_priv;
	int			 rc = 0;
	int			 i;

	if (tag >= CRT_SRV_CONTEXT_NUM) {
		D_ERROR("tag %d out of range [0, %d].\n",
			tag, CRT_SRV_CONTEXT_NUM - 1);
		return -DER_INVAL;
	}

	grp_priv = passed_grp_priv;

	if (passed_grp_priv->gp_primary == 0) {
		grp_priv = passed_grp_priv->gp_priv_prim;
		rank = crt_grp_priv_get_primary_rank(passed_grp_priv, rank);
	}

	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
		rc = grp_lc_uri_insert_internal_locked(grp_priv, i, rank,
						       tag, uri);
		if (rc != 0) {
			D_ERROR("Insertion failed, " DF_RC "\n", DP_RC(rc));
			D_GOTO(unlock, rc);
		}
	}

unlock:
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

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
	grp_priv = grp_gdata->gg_primary_grp;
	if (grp_priv != NULL) {
		crt_swim_disable_all();
		rc = crt_grp_lc_ctx_invalid(grp_priv, ctx);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_ctx_invalid failed, group %s, "
				"ctx_idx: %d, rc: %d.\n",
				grp_priv->gp_pub.cg_grpid, ctx->cc_idx, rc);
			D_GOTO(out, rc);
		}
	}

	d_list_for_each_entry(grp_priv, &crt_grp_list,
			      gp_link) {
		if (grp_priv->gp_primary == 0)
			continue;

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

	if (crt_provider_is_sep(true, crt_ctx->cc_hg_ctx.chc_provider))
		tag = 0;

	grp_priv = passed_grp_priv;

	if (passed_grp_priv->gp_primary == 0) {
		grp_priv = passed_grp_priv->gp_priv_prim;
		rank = crt_grp_priv_get_primary_rank(passed_grp_priv, rank);
	}

	ctx_idx = crt_ctx->cc_idx;
	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);

	rlink = d_hash_rec_find(&grp_priv->gp_lookup_cache[ctx_idx],
				(void *)&rank, sizeof(rank));
	D_ASSERT(rlink != NULL);
	li = crt_li_link2ptr(rlink);
	D_ASSERT(li->li_grp_priv == grp_priv);
	D_ASSERT(li->li_rank == rank);
	D_ASSERT(li->li_initialized != 0);

	D_MUTEX_LOCK(&li->li_mutex);
	if (li->li_tag_addr[tag] == NULL) {
		li->li_tag_addr[tag] = *hg_addr;
	} else {
		D_INFO("NA address already exits. "
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
void
crt_grp_lc_lookup(struct crt_grp_priv *grp_priv, int ctx_idx,
		  d_rank_t rank, uint32_t tag,
		  crt_phy_addr_t *uri, hg_addr_t *hg_addr)
{
	struct crt_lookup_item	*li;
	d_list_t		*rlink;
	struct crt_grp_priv	*default_grp_priv;
	crt_provider_t		provider;

	D_ASSERT(grp_priv != NULL);

	D_ASSERT(tag < CRT_SRV_CONTEXT_NUM);
	D_ASSERT(uri != NULL || hg_addr != NULL);
	D_ASSERT(ctx_idx >= 0 && ctx_idx < CRT_SRV_CONTEXT_NUM);

	provider = crt_gdata.cg_primary_prov;

	/* TODO: Derive from context */
	if (crt_provider_is_sep(true, provider))
		tag = 0;

	default_grp_priv = grp_priv;
	if (grp_priv->gp_primary == 0) {
		default_grp_priv = grp_priv->gp_priv_prim;

		/* convert subgroup rank to primary group rank */
		rank = crt_grp_priv_get_primary_rank(grp_priv, rank);
	}

	D_RWLOCK_RDLOCK(&default_grp_priv->gp_rwlock);
	rlink = d_hash_rec_find(&default_grp_priv->gp_lookup_cache[ctx_idx],
				(void *)&rank, sizeof(rank));
	if (rlink != NULL) {
		li = crt_li_link2ptr(rlink);
		D_ASSERT(li->li_grp_priv == default_grp_priv);
		D_ASSERT(li->li_rank == rank);
		D_ASSERT(li->li_initialized != 0);

		if (uri != NULL)
			*uri = grp_li_uri_get(li, tag);

		if (hg_addr == NULL)
			D_ASSERT(uri != NULL);
		else if (li->li_tag_addr[tag] != NULL)
			*hg_addr = li->li_tag_addr[tag];
		d_hash_rec_decref(&default_grp_priv->gp_lookup_cache[ctx_idx],
				  rlink);
		D_GOTO(out, 0);
	} else {
		D_DEBUG(DB_ALL, "Entry for rank=%d not found\n", rank);
	}
	D_RWLOCK_UNLOCK(&default_grp_priv->gp_rwlock);

	if (uri)
		*uri = NULL;
	if (hg_addr)
		*hg_addr = NULL;

	return;

out:
	D_RWLOCK_UNLOCK(&default_grp_priv->gp_rwlock);
	return;
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
		    crt_group_id_t grp_id, bool primary_grp)
{
	struct crt_grp_priv	*grp_priv;
	struct crt_swim_membs	*csm;
	int			 rc = 0;

	D_ASSERT(grp_priv_created != NULL);
	D_ASSERT(grp_id != NULL && strlen(grp_id) > 0 &&
		 strlen(grp_id) < CRT_GROUP_ID_MAX_LEN);

	D_ALLOC_PTR(grp_priv);
	if (grp_priv == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_INIT_LIST_HEAD(&grp_priv->gp_sec_list);
	D_INIT_LIST_HEAD(&grp_priv->gp_link);
	grp_priv->gp_primary = primary_grp;
	D_STRNDUP(grp_priv->gp_pub.cg_grpid, grp_id, CRT_GROUP_ID_MAX_LEN + 1);
	if (grp_priv->gp_pub.cg_grpid == NULL)
		D_GOTO(out_grp_priv, rc = -DER_NOMEM);

	csm = &grp_priv->gp_membs_swim;
	csm->csm_target = CRT_SWIM_TARGET_INVALID;

	rc = D_SPIN_INIT(&csm->csm_lock, PTHREAD_PROCESS_PRIVATE);
	if (rc)
		D_GOTO(out_grpid, rc);

	grp_priv->gp_size = 0;
	grp_priv->gp_refcount = 1;
	rc = D_RWLOCK_INIT(&grp_priv->gp_rwlock, NULL);
	if (rc)
		D_GOTO(out_swim_lock, rc);

	*grp_priv_created = grp_priv;
	return rc;

out_swim_lock:
	D_SPIN_DESTROY(&csm->csm_lock);
out_grpid:
	D_FREE(grp_priv->gp_pub.cg_grpid);
out_grp_priv:
	D_FREE(grp_priv);
out:
	return rc;
}


void
crt_grp_priv_destroy(struct crt_grp_priv *grp_priv)
{
	struct crt_context	*ctx;
	int			i;
	int			rc = 0;

	if (grp_priv == NULL)
		return;

	if (grp_priv->gp_primary) {
		for (i = 0; i < CRT_SRV_CONTEXT_NUM; i++) {
			ctx = crt_context_lookup_locked(i);
			if (ctx == NULL)
				continue;

			rc = crt_grp_ctx_invalid(ctx, true);
			if (rc != 0) {
				D_ERROR("crt_grp_ctx_invalid failed, rc: %d.\n",
					rc);
			}
		}
	}

	crt_grp_lc_destroy(grp_priv);
	d_list_del_init(&grp_priv->gp_link);

	/* remove from group list */
	D_RWLOCK_WRLOCK(&crt_grp_list_rwlock);
	crt_grp_del_locked(grp_priv);
	D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);

	crt_swim_rank_del_all(grp_priv);
	D_SPIN_DESTROY(&grp_priv->gp_membs_swim.csm_lock);

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
			struct crt_grp_priv_sec	*entry;
			bool			found = false;

			D_RWLOCK_WRLOCK(&grp_priv_prim->gp_rwlock);
			d_list_for_each_entry(entry,
					      &grp_priv_prim->gp_sec_list,
					      gps_link) {
				if (entry->gps_priv == grp_priv) {
					found = true;
					break;
				}
			}

			if (found) {
				d_list_del(&entry->gps_link);
				D_FREE(entry);
			}
			D_RWLOCK_UNLOCK(&grp_priv_prim->gp_rwlock);
		}
		d_hash_table_destroy_inplace(&grp_priv->gp_p2s_table, true);
		d_hash_table_destroy_inplace(&grp_priv->gp_s2p_table, true);
	}

	D_FREE(grp_priv->gp_psr_phy_addr);
	D_FREE(grp_priv->gp_pub.cg_grpid);

	D_RWLOCK_DESTROY(&grp_priv->gp_rwlock);
	D_FREE(grp_priv);
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
		/* to lookup default primary group handle */
		grp_priv = grp_gdata->gg_primary_grp;
		goto out;
	}

	if (crt_validate_grpid(grp_id) != 0) {
		D_ERROR("grp_id contains invalid characters or is too long\n");
		goto out;
	}

	if (crt_grp_id_identical(grp_gdata->gg_primary_grp->gp_pub.cg_grpid,
				 grp_id)) {
		grp_priv = grp_gdata->gg_primary_grp;
		goto out;
	}

	/* check list of groups */
	D_RWLOCK_RDLOCK(&crt_grp_list_rwlock);
	grp_priv = crt_grp_lookup_locked(grp_id);
	if (grp_priv == NULL)
		D_DEBUG(DB_TRACE, "group non-exist (%s).\n", grp_id);
	D_RWLOCK_UNLOCK(&crt_grp_list_rwlock);

out:
	return (grp_priv == NULL) ? NULL : &grp_priv->gp_pub;
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

	grp_priv = crt_grp_pub2priv(grp);
	*rank = grp_priv->gp_self;

	if (*rank == CRT_NO_RANK) {
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

	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
	rlink = d_hash_rec_find(&grp_priv->gp_p2s_table,
				(void *)&rank_in, sizeof(rank_in));
	if (!rlink) {
		D_ERROR("Rank=%d not part of the group\n", rank_in);
		D_GOTO(unlock, rc = -DER_OOG);
	}

	rm = crt_rm_link2ptr(rlink);
	*rank_out = rm->rm_value;

	d_hash_rec_decref(&grp_priv->gp_p2s_table, rlink);

unlock:
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

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

	if (subgrp == NULL) {
		D_ERROR("Invalid argument: subgrp is NULL.\n");
		return -DER_INVAL;
	}

	if (rank_out == NULL) {
		D_ERROR("Invalid argument: rank_out is NULL.\n");
		return -DER_INVAL;
	}

	grp_priv = container_of(subgrp, struct crt_grp_priv, gp_pub);

	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
	*rank_out = crt_grp_priv_get_primary_rank(grp_priv, rank_in);
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	return rc;
}

int
crt_group_size(crt_group_t *grp, uint32_t *size)
{
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

	grp_priv = crt_grp_pub2priv(grp);
	*size = grp_priv->gp_size;

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
		grp_priv = grp_gdata->gg_primary_grp;
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

	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
	*version = grp_priv->gp_membs_ver;
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

out:
	return rc;
}

int
crt_group_version_set(crt_group_t *grp, uint32_t version)
{
	struct crt_grp_priv	*grp_priv;
	int			 rc = 0;

	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}

	grp_priv = crt_grp_pub2priv(grp);
	if (!grp_priv) {
		D_ERROR("Invalid group\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	grp_priv->gp_membs_ver = version;
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

out:
	return rc;
}

static int
crt_primary_grp_init(crt_group_id_t grpid)
{
	struct crt_grp_gdata	*grp_gdata;
	struct crt_grp_priv	*grp_priv = NULL;
	crt_group_id_t		 pri_grpid;
	bool			 is_service;
	int			 rc = 0;

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);

	is_service = crt_is_service();
	pri_grpid = (grpid != NULL) ? grpid : CRT_DEFAULT_GRPID;

	rc = crt_grp_priv_create(&grp_priv, pri_grpid, true);
	if (rc != 0) {
		D_ERROR("crt_grp_priv_create failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	D_ASSERT(grp_priv != NULL);

	if (is_service) {
		grp_priv->gp_self = CRT_NO_RANK;
		grp_priv->gp_size = 0;
	} else {
		grp_priv->gp_size = 1;
		grp_priv->gp_self = 0;
	}

	rc = grp_priv_init_membs(grp_priv, grp_priv->gp_size);
	if (rc != 0) {
		D_ERROR("grp_priv_init_membs() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	grp_gdata->gg_primary_grp = grp_priv;

	rc = crt_grp_lc_create(grp_priv);
	if (rc != 0) {
		D_ERROR("crt_grp_lc_create() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

out:
	if (rc == 0) {
		D_DEBUG(DB_TRACE, "primary group %s, gp_size %d, gp_self %d.\n",
			grp_priv->gp_pub.cg_grpid, grp_priv->gp_size,
			grp_priv->gp_self);
	} else {
		D_ERROR("failed, " DF_RC "\n", DP_RC(rc));
		if (grp_priv != NULL)
			crt_grp_priv_decref(grp_priv);
	}

	return rc;
}

static void
crt_primary_grp_fini(void)
{
	struct crt_grp_priv	*grp_priv;
	struct crt_grp_gdata	*grp_gdata;

	grp_gdata = crt_gdata.cg_grp;
	D_ASSERT(grp_gdata != NULL);

	grp_priv = grp_gdata->gg_primary_grp;
	crt_grp_priv_decref(grp_priv);
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
	default_grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	if (strncmp(ul_in->ul_grp_id, default_grp_priv->gp_pub.cg_grpid,
		    CRT_GROUP_ID_MAX_LEN) == 0) {
		grp_priv = default_grp_priv;
		D_DEBUG(DB_TRACE, "ul_grp_id %s matches with gg_primary_grp"
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

	if (unlikely(ul_in->ul_tag >= CRT_SRV_CONTEXT_NUM)) {
		D_WARN("Looking up invalid tag %d of rank %d "
		       "in group %s (%d)\n",
		       ul_in->ul_tag, ul_in->ul_rank,
		       grp_priv->gp_pub.cg_grpid, grp_priv->gp_size);
		D_GOTO(out, rc = -DER_INVAL);
	}

	grp_priv_primary = default_grp_priv;

	if (grp_priv->gp_primary == 0)
		grp_priv_primary = grp_priv->gp_priv_prim;

	/* convert the requested rank to global rank */
	g_rank = crt_grp_priv_get_primary_rank(grp_priv, ul_in->ul_rank);

	/* step 0, if I am the final target, reply with URI */
	if (g_rank == grp_priv_primary->gp_self) {
		rc = crt_self_uri_get(ul_in->ul_tag, &tmp_uri);
		if (rc != DER_SUCCESS)
			D_ERROR("crt_self_uri_get(tag: %d) failed, "
				"rc %d\n", ul_in->ul_tag, rc);
		ul_out->ul_uri = tmp_uri;
		ul_out->ul_tag = ul_in->ul_tag;
		if (crt_gdata.cg_use_sensors)
			d_tm_inc_counter(crt_gdata.cg_uri_self, 1);
		D_GOTO(out, rc);
	}

	/* step 1, lookup the URI in the local cache */
	crt_grp_lc_lookup(grp_priv_primary, crt_ctx->cc_idx, g_rank,
			  ul_in->ul_tag, &cached_uri, NULL);
	ul_out->ul_uri = cached_uri;
	ul_out->ul_tag = ul_in->ul_tag;

	if (ul_out->ul_uri != NULL) {
		if (crt_gdata.cg_use_sensors)
			d_tm_inc_counter(crt_gdata.cg_uri_other, 1);
		D_GOTO(out, rc);
	}

	/* If this server does not know rank:0 then return error */
	if (ul_in->ul_tag == 0)
		D_GOTO(out, rc = -DER_OOG);

	/**
	 * step 2, if rank:tag is not found, lookup rank:tag=0
	 */
	ul_out->ul_tag = 0;
	crt_grp_lc_lookup(grp_priv_primary, crt_ctx->cc_idx,
			  g_rank, 0, &cached_uri, NULL);
	ul_out->ul_uri = cached_uri;
	if (ul_out->ul_uri == NULL)
		D_GOTO(out, rc = -DER_OOG);

out:
	if (should_decref)
		crt_grp_priv_decref(grp_priv);
	ul_out->ul_rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send failed, rc: %d, opc: %#x.\n",
			rc, rpc_req->cr_opc);
	D_FREE(tmp_uri);
}

int
crt_group_attach(crt_group_id_t srv_grpid, crt_group_t **attached_grp)
{
	struct crt_grp_priv	*grp_priv;
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

out:
	if (rc != 0)
		D_ERROR("crt_group_attach failed, rc: %d.\n", rc);
	return rc;
}

int
crt_group_detach(crt_group_t *attached_grp)
{
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

	grp_priv = crt_grp_pub2priv(attached_grp);

	crt_grp_priv_decref(grp_priv);
out:
	return rc;
}

int
crt_grp_init(crt_group_id_t grpid)
{
	struct crt_grp_gdata	*grp_gdata;
	int			 rc = 0;

	D_ASSERT(crt_gdata.cg_grp_inited == 0);
	D_ASSERT(crt_gdata.cg_grp == NULL);

	D_ALLOC_PTR(grp_gdata);
	if (grp_gdata == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = D_RWLOCK_INIT(&grp_gdata->gg_rwlock, NULL);
	if (rc != 0)
		D_GOTO(out, rc);

	crt_gdata.cg_grp = grp_gdata;

	rc = crt_primary_grp_init(grpid);
	if (rc != 0) {
		D_RWLOCK_DESTROY(&grp_gdata->gg_rwlock);
		D_GOTO(out, rc);
	}

	crt_gdata.cg_grp_inited = 1;

out:
	if (rc != 0) {
		D_FREE(grp_gdata);
		crt_gdata.cg_grp = NULL;
	}
	return rc;
}

void
crt_grp_fini(void)
{
	struct crt_grp_gdata	*grp_gdata;

	D_ASSERT(crt_gdata.cg_grp_inited == 1);
	D_ASSERT(crt_gdata.cg_grp != NULL);
	grp_gdata = crt_gdata.cg_grp;

	crt_primary_grp_fini();

	D_RWLOCK_DESTROY(&grp_gdata->gg_rwlock);
	D_FREE(grp_gdata);
	crt_gdata.cg_grp = NULL;
	crt_gdata.cg_grp_inited = 0;
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
	mode_t		 old_mode;

	if (filename == NULL) {
		D_ERROR("filename can't be NULL.\n");
		return NULL;
	}

	D_ASPRINTF(*filename, "%s/%s", crt_attach_prefix, template);
	if (*filename == NULL)
		return NULL;
	D_ASSERT(*filename != NULL);

	/** Ensure the temporary file is created with proper permissions to
	 *  limit security risk.
	 */
	old_mode = umask(S_IWGRP | S_IWOTH);

	tmp_fd = mkstemp(*filename);
	umask(old_mode);

	if (tmp_fd == -1) {
		D_ERROR("mkstemp() failed on %s, error: %s.\n",
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
		D_ERROR("path can't be NULL\n");
		return -DER_INVAL;
	}

	if (strlen(path) >= CRT_MAX_ATTACH_PREFIX) {
		D_ERROR("specified path must be fewer than %d characters\n", CRT_MAX_ATTACH_PREFIX);
		return -DER_INVAL;
	}

	rc = stat(path, &buf);
	if (rc != 0) {
		D_ERROR("bad path specified: %s\n", path);
		return d_errno2der(errno);
	}

	if (!S_ISDIR(buf.st_mode)) {
		D_ERROR("not a directory: %s\n", path);
		return -DER_NOTDIR;
	}

	strncpy(crt_attach_prefix, path, CRT_MAX_ATTACH_PREFIX - 1);

	return 0;
}

int
crt_nr_secondary_remote_tags_set(int idx, int num_tags)
{
	struct crt_prov_gdata *prov_data;

	D_DEBUG(DB_ALL, "secondary_idx=%d num_tags=%d\n", idx, num_tags);

	if (idx != 0) {
		D_ERROR("Only idx=0 is currently supported\n");
		return -DER_NONEXIST;
	}

	if ((crt_gdata.cg_prov_gdata_secondary == NULL) ||
	    (idx >= crt_gdata.cg_num_secondary_provs)) {
		D_ERROR("Secondary providers not initialized\n");
		return -DER_NONEXIST;
	}

	if (num_tags <= 0) {
		D_ERROR("Invalid number of tags: %d\n", num_tags);
		return -DER_INVAL;
	}

	prov_data = &crt_gdata.cg_prov_gdata_secondary[idx];
	prov_data->cpg_num_remote_tags = num_tags;

	return DER_SUCCESS;
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
	if (!crt_is_service() || !grp_priv->gp_primary) {
		D_ERROR("Can only save config info for primary service grp.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rank = grp_priv->gp_self;

	addr = crt_gdata.cg_prov_gdata_primary.cpg_addr;

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

	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
	membs = grp_priv_get_membs(grp_priv);
	locked = true;

	for (i = 0; i < grp_priv->gp_size; i++) {
		char *uri;

		uri = NULL;
		rank = membs->rl_ranks[i];

		rc = crt_rank_uri_get(grp, rank, 0, &uri);
		if (rc != 0) {
			D_ERROR("crt_rank_uri_get(%s, %d) failed "
				"rc: %d.\n", grpid, rank, rc);
			D_GOTO(out, rc);
		}

		D_ASSERT(uri != NULL);

		rc = fprintf(fp, "%d %s\n", rank, uri);

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

	D_DEBUG(DB_ALL, "Group config saved in %s\n", filename);
out:
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
	if (!crt_is_service() || !grp_priv->gp_primary) {
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

	rc = fscanf(fp, "%4s", all_or_self);
	if (rc == EOF) {
		D_ERROR("read from file %s failed (%s).\n",
			filename, strerror(errno));
		D_GOTO(out, rc = d_errno2der(errno));
	}

	D_ALLOC(addr_str, CRT_ADDR_STR_MAX_LEN + 1);
	if (addr_str == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	memset(fmt, 0, 64);
	snprintf(fmt, 64, "%%d %%%ds", CRT_ADDR_STR_MAX_LEN);
	rc = -DER_INVAL;

	while (1) {
		rc = fscanf(fp, fmt, &rank, (char *)addr_str);
		if (rc == EOF) {
			rc = 0;
			break;
		}

		rc = crt_group_primary_add_internal(grp_priv, rank, 0,
						    addr_str);
		if (rc != 0) {
			D_ERROR("crt_group_node_add_internal() failed;"
				" rank=%d uri='%s' rc=%d\n",
				rank, addr_str, rc);
			break;
		}

		if (rank == psr_rank)
			crt_grp_psr_set(grp_priv, rank, addr_str, false);
	}

	/* TODO: PSR selection logic to be changed with CART-688 */
	if (psr_rank != -1)
		crt_grp_psr_set(grp_priv, rank, addr_str, false);

out:
	if (fp)
		fclose(fp);
	D_FREE(filename);
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

int
crt_register_event_cb(crt_event_cb func, void *args)
{
	struct crt_event_cb_priv *cbs_event;
	size_t i, cbs_size;
	int rc = 0;

	D_MUTEX_LOCK(&crt_plugin_gdata.cpg_mutex);

	cbs_size = crt_plugin_gdata.cpg_event_size;
	cbs_event = crt_plugin_gdata.cpg_event_cbs;

	for (i = 0; i < cbs_size; i++) {
		if (cbs_event[i].cecp_func == func &&
		    cbs_event[i].cecp_args == args) {
			D_GOTO(out_unlock, rc = -DER_EXIST);
		}
	}

	for (i = 0; i < cbs_size; i++) {
		if (cbs_event[i].cecp_func == NULL) {
			cbs_event[i].cecp_args = args;
			cbs_event[i].cecp_func = func;
			D_GOTO(out_unlock, rc = 0);
		}
	}

	D_FREE(crt_plugin_gdata.cpg_event_cbs_old);

	crt_plugin_gdata.cpg_event_cbs_old = cbs_event;
	cbs_size += CRT_CALLBACKS_NUM;

	D_ALLOC_ARRAY(cbs_event, cbs_size);
	if (cbs_event == NULL) {
		crt_plugin_gdata.cpg_event_cbs_old = NULL;
		D_GOTO(out_unlock, rc = -DER_NOMEM);
	}

	if (i > 0)
		memcpy(cbs_event, crt_plugin_gdata.cpg_event_cbs_old,
		       i * sizeof(*cbs_event));
	cbs_event[i].cecp_args = args;
	cbs_event[i].cecp_func = func;

	crt_plugin_gdata.cpg_event_cbs  = cbs_event;
	crt_plugin_gdata.cpg_event_size = cbs_size;

out_unlock:
	D_MUTEX_UNLOCK(&crt_plugin_gdata.cpg_mutex);
	return rc;
}

int
crt_unregister_event_cb(crt_event_cb func, void *args)
{
	struct crt_event_cb_priv *cb_event;
	size_t i, cbs_size;
	int rc = -DER_NONEXIST;

	D_MUTEX_LOCK(&crt_plugin_gdata.cpg_mutex);

	cbs_size = crt_plugin_gdata.cpg_event_size;
	cb_event = crt_plugin_gdata.cpg_event_cbs;

	for (i = 0; i < cbs_size; i++) {
		if (cb_event[i].cecp_func == func &&
		    cb_event[i].cecp_args == args) {
			cb_event[i].cecp_func = NULL;
			cb_event[i].cecp_args = NULL;
			D_GOTO(out_unlock, rc = 0);
		}
	}

out_unlock:
	D_FREE(crt_plugin_gdata.cpg_event_cbs_old);

	D_MUTEX_UNLOCK(&crt_plugin_gdata.cpg_mutex);
	return rc;
}

void
crt_trigger_event_cbs(d_rank_t rank, uint64_t incarnation, enum crt_event_source src,
		      enum crt_event_type type)
{
	struct crt_event_cb_priv	*cbs_event;
	size_t				 cbs_size;
	int				 cb_idx;
	crt_event_cb			 cb_func;
	void				*cb_args;

	cbs_event = crt_plugin_gdata.cpg_event_cbs;
	cbs_size = crt_plugin_gdata.cpg_event_size;
	for (cb_idx = 0; cb_idx < cbs_size; cb_idx++) {
		cb_func = cbs_event[cb_idx].cecp_func;
		cb_args = cbs_event[cb_idx].cecp_args;

		if (cb_func != NULL)
			cb_func(rank, incarnation, src, type, cb_args);
	}
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

		crt_grp_lc_lookup(grp_priv, 0, psr_rank, 0, &uri, NULL);
		if (uri == NULL)
			break;

		rc = crt_grp_psr_set(grp_priv, psr_rank, uri, false);
		D_GOTO(out, 0);
	}

	rc = crt_grp_config_psr_load(grp_priv, psr_rank);
	if (rc != 0) {
		D_ERROR("crt_grp_config_psr_load(grp %s, psr_rank %d), "
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
				      struct free_index, fi_link);

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
	if (free_index == NULL)
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

	membs = grp_priv->gp_membs.cgm_list;
	linear_list = grp_priv->gp_membs.cgm_linear_list;

	/* If group size changed - reallocate the list */
	if (!linear_list->rl_ranks ||
	    linear_list->rl_nr != grp_priv->gp_size) {
		linear_list = d_rank_list_realloc(linear_list,
						  grp_priv->gp_size);
		if (linear_list == NULL)
			return -DER_NOMEM;
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
int
grp_add_to_membs_list(struct crt_grp_priv *grp_priv, d_rank_t rank, uint64_t incarnation)
{
	d_rank_list_t	*membs;
	int		index;
	int		first;
	int		i;
	uint32_t	new_amount;
	int		rc = 0;
	int		ret;

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

		membs = d_rank_list_realloc(membs, new_amount);
		if (membs == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		for (i = first; i < first + RANK_LIST_REALLOC_SIZE; i++) {
			membs->rl_ranks[i] = CRT_NO_RANK;
			rc = grp_add_free_index(
				&grp_priv->gp_membs.cgm_free_indices,
				i, true);
			if (rc != -DER_SUCCESS)
				D_GOTO(out, 0);
		}

		index = grp_get_free_index(grp_priv);
	}

	D_ASSERT(index >= 0);

	/* Do not populate swim entries for views and secondary groups */
	if (grp_priv->gp_primary && !grp_priv->gp_view) {
		rc = crt_swim_rank_add(grp_priv, rank, incarnation);
		if (rc) {
			D_ERROR("crt_swim_rank_add() failed: rc=%d\n", rc);
			D_GOTO(out, 0);
		} else {
			membs->rl_ranks[index] = rank;
			grp_priv->gp_size++;
		}
	} else {
		membs->rl_ranks[index] = rank;
		grp_priv->gp_size++;
	}

	/* Regenerate linear list*/
	ret = grp_regen_linear_list(grp_priv);
	if (ret != 0) {
		grp_add_free_index(&grp_priv->gp_membs.cgm_free_indices,
				   index, false);
		membs->rl_ranks[index] = CRT_NO_RANK;
		grp_priv->gp_size--;
	}

	if (ret != 0 && rc == 0)
		rc = ret;
out:
	return rc;
}

static int
crt_group_primary_add_internal(struct crt_grp_priv *grp_priv,
			       d_rank_t rank, int tag, char *uri)
{
	int rc;

	if (!grp_priv->gp_primary) {
		D_ERROR("Only available for primary groups\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_grp_lc_uri_insert(grp_priv, rank, tag, uri);
	if (rc != 0) {
		D_ERROR("crt_grp_lc_uri_insert() failed, " DF_RC "\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Only add node to membership list once, for tag 0 */
	/* TODO: This logic needs to be refactored as part of CART-517 */
	if (tag == 0) {
		D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
		rc = grp_add_to_membs_list(grp_priv, rank, CRT_NO_INCARNATION);
		D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
	}

out:
	return rc;
}

int
crt_rank_self_set(d_rank_t rank, uint32_t group_version_min)
{
	int rc = 0;
	struct crt_grp_priv	*default_grp_priv;
	hg_class_t		*hg_class;
	hg_size_t		size;
	struct crt_context	*ctx;
	char			uri_addr[CRT_ADDR_STR_MAX_LEN] = {'\0'};
	d_list_t		*ctx_list;

	default_grp_priv = crt_gdata.cg_grp->gg_primary_grp;

	D_INFO("Setting self rank to %u and minimum group version to %u\n", rank,
	       group_version_min);

	if (!crt_is_service()) {
		D_WARN("Setting self rank is not supported on client\n");
		return 0;
	}

	if (rank == CRT_NO_RANK) {
		D_ERROR("Self rank should not be %u\n", CRT_NO_RANK);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (group_version_min == 0) {
		D_ERROR("Minimum group version should not be zero\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (default_grp_priv->gp_self != CRT_NO_RANK) {
		D_ERROR("Self rank was already set to %d\n",
			default_grp_priv->gp_self);
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_RWLOCK_WRLOCK(&default_grp_priv->gp_rwlock);
	default_grp_priv->gp_self = rank;
	default_grp_priv->gp_membs_ver_min = group_version_min;
	rc = grp_add_to_membs_list(default_grp_priv, rank, CRT_NO_INCARNATION);
	D_RWLOCK_UNLOCK(&default_grp_priv->gp_rwlock);

	if (rc != 0) {
		D_ERROR("grp_add_to_membs_list() failed; rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	D_RWLOCK_WRLOCK(&crt_gdata.cg_rwlock);

	ctx_list = crt_provider_get_ctx_list(true, crt_gdata.cg_primary_prov);

	d_list_for_each_entry(ctx, ctx_list, cc_link) {
		hg_class =  ctx->cc_hg_ctx.chc_hgcla;

		size = CRT_ADDR_STR_MAX_LEN;
		rc = crt_hg_get_addr(hg_class, uri_addr, &size);
		if (rc != 0) {
			D_ERROR("crt_hg_get_addr() failed; rc=%d\n", rc);
			D_GOTO(unlock, rc);
		}

		rc = crt_grp_lc_uri_insert(default_grp_priv, rank, ctx->cc_idx,
					   uri_addr);
		if (rc != 0) {
			D_ERROR("crt_grp_lc_uri_insert() failed; rc=%d\n",
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
	struct crt_grp_priv	*grp_priv;
	crt_phy_addr_t		uri;
	hg_addr_t		hg_addr;
	int			rc = 0;

	if (uri_str == NULL) {
		D_ERROR("Passed uri_str is NULL\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	grp_priv = crt_grp_pub2priv(group);

	if (!grp_priv->gp_primary) {
		D_ERROR("Only available for primary groups\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (rank == grp_priv->gp_self && crt_is_service())
		return crt_self_uri_get(tag, uri_str);

	crt_grp_lc_lookup(grp_priv, 0, rank, tag, &uri, &hg_addr);
	if (uri == NULL) {
		D_DEBUG(DB_ALL, "uri for %d:%d not found\n", rank, tag);
		D_GOTO(out, rc = -DER_OOG);
	}

	D_STRNDUP(*uri_str, uri, strlen(uri) + 1);
	if (!(*uri_str)) {
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
			rc = grp_regen_linear_list(grp_priv);
			break;
		}
	}

#if 0
	/* CART-829: Remove in progress and pending rpcs to the rank  */
	crt_endpoint_t	tgt_ep;

	tgt_ep.ep_grp = grp;
	tgt_ep.ep_rank = rank;
	/* ep_tag is not used in crt_ep_abort() */
	tgt_ep.ep_tag = 0;
	rc = crt_ep_abort(&tgt_ep);
#endif

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
	struct crt_grp_priv_sec	*entry;
	int			rc;

	/* Note: This function is called with grp_priv lock taken */
	d_list_for_each_entry(entry, &grp_priv->gp_sec_list, gps_link) {
		sec_priv = entry->gps_priv;
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
	if (grp_priv->gp_auto_remove)
		crt_grp_remove_from_secondaries(grp_priv, rank);
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

out:
	if (rc == 0 && grp_priv->gp_primary && !grp_priv->gp_view)
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

	grp_priv = crt_grp_pub2priv(group);

	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	membs = grp_priv->gp_membs.cgm_linear_list;
	rc = d_rank_list_dup(list, membs);
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

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

	rc = crt_grp_priv_create(&grp_priv, srv_grpid, true);
	if (rc != 0) {
		D_ERROR("crt_grp_priv_create(%s) failed, " DF_RC "\n",
			srv_grpid, DP_RC(rc));
		D_GOTO(out, rc);
	}

	grp_priv->gp_size = 0;
	grp_priv->gp_self = CRT_NO_RANK;

	rc = grp_priv_init_membs(grp_priv, grp_priv->gp_size);
	if (rc != 0) {
		D_ERROR("grp_priv_init_membs() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	grp_priv->gp_view = 1;

	rc = crt_grp_lc_create(grp_priv);
	if (rc != 0) {
		D_ERROR("crt_grp_lc_create() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	*ret_grp = &grp_priv->gp_pub;

	D_RWLOCK_WRLOCK(&grp_gdata->gg_rwlock);
	d_list_add_tail(&grp_priv->gp_link, &crt_grp_list);
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

	crt_grp_priv_decref(grp_priv);
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
		D_ERROR("crt_rank_uri_get() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = crt_grp_psr_set(grp_priv, rank, uri, true);
out:
	return rc;
}

int
crt_group_secondary_create(crt_group_id_t grp_name, crt_group_t *primary_grp,
			   d_rank_list_t *ranks, crt_group_t **ret_grp)
{
	struct crt_grp_priv	*grp_priv = NULL;
	struct crt_grp_priv	*grp_priv_prim = NULL;
	struct crt_grp_priv_sec *entry;
	int			rc = 0;
	int			i;

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

	rc = crt_grp_priv_create(&grp_priv, grp_name, false);
	if (rc != 0) {
		D_ERROR("crt_grp_priv_create(%s) failed, " DF_RC "\n",
			grp_name, DP_RC(rc));
		D_GOTO(out, rc);
	}

	grp_priv->gp_size = 0;
	grp_priv->gp_self = CRT_NO_RANK;

	rc = grp_priv_init_membs(grp_priv, grp_priv->gp_size);
	if (rc != 0) {
		D_ERROR("grp_priv_init_membs() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* URI lookup table here stores secondary ranks instead of addrs */
	rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK,
					 CRT_LOOKUP_CACHE_BITS,
					 NULL, &rank_mapping_ops,
					 &grp_priv->gp_p2s_table);
	if (rc != 0) {
		D_ERROR("d_hash_table_create() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK,
					 CRT_LOOKUP_CACHE_BITS,
					 NULL, &rank_mapping_ops,
					 &grp_priv->gp_s2p_table);
	if (rc != 0) {
		D_ERROR("d_hash_table_create() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Record secondary group in the primary group */
	D_ALLOC_PTR(entry);
	if (entry == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	entry->gps_priv = grp_priv;

	D_RWLOCK_WRLOCK(&grp_priv_prim->gp_rwlock);
	d_list_add_tail(&entry->gps_link, &grp_priv_prim->gp_sec_list);
	D_RWLOCK_UNLOCK(&grp_priv_prim->gp_rwlock);

	/*
	 * Record primary group in the secondary group. Note that this field
	 * controls whether crt_grp_priv_destroy attempts to remove this
	 * secondary group from grp_priv_prim->gp_sec_list.
	 */
	grp_priv->gp_priv_prim = grp_priv_prim;

	*ret_grp = &grp_priv->gp_pub;

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

	crt_grp_priv_decref(grp_priv);
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

	D_ALLOC_PTR(rm);
	if (rm == NULL)
		goto out;

	D_INIT_LIST_HEAD(&rm->rm_link);
	rm->rm_key = key;
	rm->rm_value = value;
	rm->rm_ref = 0;
	rm->rm_initialized = 1;

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
	int			rc = 0;

	/*
	 * Set the self rank based on my primary group rank. For simplicity,
	 * assert that my primary group rank must have been set already, since
	 * this is always the case with daos_engine today.
	 */
	D_ASSERT(grp_priv->gp_priv_prim->gp_self != CRT_NO_RANK);
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
	if (rm_s2p == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rm_p2s = crt_rank_mapping_init(prim_rank, sec_rank);
	if (rm_p2s == NULL) {
		crt_rm_destroy(rm_s2p);
		D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = d_hash_rec_insert(&grp_priv->gp_s2p_table,
			       &sec_rank, sizeof(sec_rank),
			       &rm_s2p->rm_link, true);
	if (rc != 0) {
		D_ERROR("Failed to add entry: "DF_RC"\n", DP_RC(rc));
		crt_rm_destroy(rm_p2s);
		crt_rm_destroy(rm_s2p);
		D_GOTO(out, rc);
	}

	rc = d_hash_rec_insert(&grp_priv->gp_p2s_table,
			       &prim_rank, sizeof(prim_rank),
			       &rm_p2s->rm_link, true);
	if (rc != 0) {
		D_ERROR("Failed to add entry: "DF_RC"\n", DP_RC(rc));
		crt_rm_destroy(rm_p2s);
		d_hash_rec_delete_at(&grp_priv->gp_s2p_table, &rm_s2p->rm_link);
		D_GOTO(out, rc);
	}

	/* Add secondary rank to membership list  */
	rc = grp_add_to_membs_list(grp_priv, sec_rank, CRT_NO_INCARNATION);
	if (rc != 0) {
		d_hash_rec_delete_at(&grp_priv->gp_p2s_table, &rm_p2s->rm_link);
		d_hash_rec_delete_at(&grp_priv->gp_s2p_table, &rm_s2p->rm_link);
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
 * Helper function to return back a list of indices (into mod_membs) to add, an
 * optional list of indices (into mod_membs) to check, and a list of ranks to
 * remove.
 */
static int
crt_group_mod_get(d_rank_list_t *grp_membs, d_rank_list_t *mod_membs, crt_group_mod_op_t op,
		  uint32_t **ret_idx_to_add, uint32_t *ret_n_idx_to_add,
		  uint32_t **ret_idx_to_check, uint32_t *ret_n_idx_to_check,
		  d_rank_list_t **ret_to_remove)

{
	d_rank_t	rank;
	int		rc = 0;
	uint32_t	*idx_to_add = NULL;
	uint32_t	n_idx_to_add = 0;
	uint32_t	*idx_to_check = NULL;
	uint32_t	n_idx_to_check = 0;
	d_rank_list_t	*to_remove = NULL;
	int		i;

	D_ASSERT(grp_membs != NULL);
	D_ASSERT(mod_membs != NULL);
	D_ASSERT(ret_idx_to_add != NULL && ret_n_idx_to_add != NULL);
	D_ASSERT(ret_idx_to_check == NULL || ret_n_idx_to_check != NULL);
	D_ASSERT(ret_to_remove != NULL);

	/* At most we will remove all members from old group */
	to_remove = d_rank_list_alloc(grp_membs->rl_nr);

	/* At most we will add or check all members from new group */
	D_ALLOC_ARRAY(idx_to_add, mod_membs->rl_nr);
	if (ret_idx_to_check != NULL)
		D_ALLOC_ARRAY(idx_to_check, mod_membs->rl_nr);

	if (to_remove == NULL || idx_to_add == NULL ||
	    (ret_idx_to_check != NULL && idx_to_check == NULL)) {
		D_ERROR("Failed to allocate lists\n");
		D_GOTO(cleanup, rc = -DER_NOMEM);
	}

	to_remove->rl_nr = 0;
	/* Build idx_to_add, idx_to_check, and to_remove lists based on op specified */
	if (op == CRT_GROUP_MOD_OP_REPLACE) {
		/*
		 * Replace:
		 * If rank exists in mod_membs but not in grp_membs - add it
		 * If rank exists in grp_membs but not in mod_membs - remove it
		 * Otherwise - check it (for SWIM state)
		 */

		/* Build lists of new ranks to add and existing rank to check */
		for (i = 0; i < mod_membs->rl_nr; i++) {
			rank = mod_membs->rl_ranks[i];

			if (d_rank_in_rank_list(grp_membs, rank)) {
				if (idx_to_check != NULL)
					idx_to_check[n_idx_to_check++] = i;
			} else {
				idx_to_add[n_idx_to_add++] = i;
			}
		}

		/* Build list of ranks to remove */
		for (i = 0; i < grp_membs->rl_nr; i++) {
			rank = grp_membs->rl_ranks[i];

			if (!d_rank_in_rank_list(mod_membs, rank))
				to_remove->rl_ranks[to_remove->rl_nr++] = rank;
		}
	} else if (op == CRT_GROUP_MOD_OP_ADD) {
		/* Build list of ranks to add; nothing to remove */
		for (i = 0; i < mod_membs->rl_nr; i++) {
			rank = mod_membs->rl_ranks[i];

			if (d_rank_in_rank_list(grp_membs, rank)) {
				if (idx_to_check != NULL)
					idx_to_check[n_idx_to_check++] = i;
			} else {
				idx_to_add[n_idx_to_add++] = i;
			}
		}
	} else if (op == CRT_GROUP_MOD_OP_REMOVE) {
		/* Build list of ranks to remove; nothing to add */
		for (i = 0; i < mod_membs->rl_nr; i++) {
			rank = mod_membs->rl_ranks[i];

			if (d_rank_in_rank_list(grp_membs, rank))
				to_remove->rl_ranks[to_remove->rl_nr++] = rank;
		}
	} else {
		D_ERROR("Should never get here\n");
		D_ASSERT(0);
	}

	if (n_idx_to_add == 0 && to_remove->rl_nr == 0)
		D_DEBUG(DB_TRACE, "Membership unchanged\n");

	*ret_idx_to_add = idx_to_add;
	*ret_n_idx_to_add = n_idx_to_add;
	if (ret_idx_to_check != NULL) {
		*ret_idx_to_check = idx_to_check;
		*ret_n_idx_to_check = n_idx_to_check;
	}
	*ret_to_remove = to_remove;

	return rc;

cleanup:
	if (idx_to_check != NULL)
		D_FREE(idx_to_check);

	if (idx_to_add != NULL)
		D_FREE(idx_to_add);

	if (to_remove != NULL)
		d_rank_list_free(to_remove);

	return rc;
}

int
crt_group_auto_rank_remove(crt_group_t *grp, bool enable)
{
	struct crt_grp_priv	*grp_priv;
	int			rc = 0;

	grp_priv = crt_grp_pub2priv(grp);
	if (grp_priv == NULL) {
		D_ERROR("Failed to get grp_priv\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* Noop for secondary groups */
	if (!grp_priv->gp_primary)
		D_GOTO(out, 0);

	grp_priv->gp_auto_remove = (enable) ? 1 : 0;

out:
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
crt_group_primary_modify(crt_group_t *grp, crt_context_t *ctxs, int num_ctxs, d_rank_list_t *ranks,
			 uint64_t *incarnations, char **uris, crt_group_mod_op_t op,
			 uint32_t version)
{
	struct crt_grp_priv		*grp_priv;
	d_rank_list_t			*grp_membs;
	d_rank_list_t			*to_remove;
	uint32_t			*idx_to_add;
	uint32_t			n_idx_to_add;
	uint32_t			*idx_to_check;
	uint32_t			n_idx_to_check;
	d_rank_t			rank;
	int				i, k, cb_idx;
	int				rc = 0;
	crt_event_cb			cb_func;
	void				*cb_args;
	struct crt_event_cb_priv	*cbs_event;
	size_t				cbs_size;

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

	if (grp_priv->gp_membs_ver_min == 0) {
		D_INFO("Minimum group version not known yet\n");
		D_GOTO(unlock, rc = -DER_UNINIT);
	}

	if (version < grp_priv->gp_membs_ver_min || version <= grp_priv->gp_membs_ver) {
		D_INFO("Incoming group version too low: incoming=%u min=%u current=%u\n", version,
		       grp_priv->gp_membs_ver_min, grp_priv->gp_membs_ver);
		D_GOTO(unlock, rc = -DER_ALREADY);
	}

	grp_membs = grp_priv_get_membs(grp_priv);

	/* Get back list of nodes to add, to remove and uri index list */
	rc = crt_group_mod_get(grp_membs, ranks, op, &idx_to_add, &n_idx_to_add, &idx_to_check,
			       &n_idx_to_check, &to_remove);
	if (rc != 0)
		D_GOTO(unlock, rc);

	cbs_size = crt_plugin_gdata.cpg_event_size;
	cbs_event = crt_plugin_gdata.cpg_event_cbs;

	/* Add ranks based on idx_to_add list */
	for (i = 0; i < n_idx_to_add; i++) {
		uint32_t	idx = idx_to_add[i];
		uint64_t	incarnation = incarnations[idx];

		rank = ranks->rl_ranks[idx];

		rc = grp_add_to_membs_list(grp_priv, rank, incarnation);
		if (rc != 0) {
			D_ERROR("grp_add_to_memb_list %d failed; rc=%d\n",
				rank, rc);
			D_GOTO(cleanup, rc);
		}

		/* TODO: Change for multi-provider support */
		for (k = 0; k < CRT_SRV_CONTEXT_NUM; k++) {
			rc = grp_lc_uri_insert_internal_locked(grp_priv, k, rank, 0, uris[idx]);

			if (rc != 0)
				D_GOTO(cleanup, rc);
		}

		/* Notify about members being added */
		for (cb_idx = 0; cb_idx < cbs_size; cb_idx++) {
			cb_func = cbs_event[cb_idx].cecp_func;
			cb_args = cbs_event[cb_idx].cecp_args;

			if (cb_func != NULL)
				cb_func(rank, incarnation, CRT_EVS_GRPMOD, CRT_EVT_ALIVE, cb_args);
		}
	}

	/* Remove ranks based on to_remove list */
	for (i = 0; i < to_remove->rl_nr; i++) {
		rank = to_remove->rl_ranks[i];
		crt_group_rank_remove_internal(grp_priv, rank);

		if (grp_priv->gp_auto_remove) {
			/* Remove rank from associated secondary groups */
			crt_grp_remove_from_secondaries(grp_priv, rank);
		}

		/* Notify about members being removed */
		for (cb_idx = 0; cb_idx < cbs_size; cb_idx++) {
			cb_func = cbs_event[cb_idx].cecp_func;
			cb_args = cbs_event[cb_idx].cecp_args;

			if (cb_func != NULL)
				cb_func(rank, CRT_NO_INCARNATION, CRT_EVS_GRPMOD, CRT_EVT_DEAD,
					cb_args);
		}

		/* Remove rank from swim tracking */
		crt_swim_rank_del(grp_priv, rank);
	}

	/* Check SWIM states of ranks based on idx_to_check list */
	for (i = 0; i < n_idx_to_check; i++) {
		uint32_t	idx = idx_to_check[i];
		uint64_t	incarnation = incarnations[idx];

		rank = ranks->rl_ranks[idx];
		rc = crt_swim_rank_check(grp_priv, rank, incarnation);
		if (rc != 0)
			D_ERROR("Failed to check SWIM state of rank %u: "DF_RC"\n", rank,
				DP_RC(rc));
	}

	if (!grp_priv->gp_view && n_idx_to_add > 0)
		crt_swim_rank_shuffle(grp_priv);

	D_FREE(idx_to_add);
	D_FREE(idx_to_check);
	d_rank_list_free(to_remove);

	grp_priv->gp_membs_ver = version;
unlock:
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

out:
	return rc;

cleanup:

	D_ERROR("Failure when adding node %d, rc=%d\n", ranks->rl_ranks[idx_to_add[i]], rc);

	for (k = 0; k < i; k++)
		crt_group_rank_remove_internal(grp_priv, ranks->rl_ranks[idx_to_add[k]]);

	D_FREE(idx_to_add);
	D_FREE(idx_to_check);
	d_rank_list_free(to_remove);

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
	uint32_t		*idx_to_add;
	uint32_t		n_idx_to_add;
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
	rc = crt_group_mod_get(grp_membs, sec_ranks, op, &idx_to_add, &n_idx_to_add,
			       NULL /* ret_idx_to_check */, NULL /* ret_n_idx_to_check */,
			       &to_remove);
	if (rc != 0)
		D_GOTO(unlock, rc);

	/* Add ranks based on idx_to_add list */
	for (i = 0; i < n_idx_to_add; i++) {
		uint32_t idx = idx_to_add[i];

		rc = crt_group_secondary_rank_add_internal(grp_priv, sec_ranks->rl_ranks[idx],
							   prim_ranks->rl_ranks[idx]);
		if (rc != 0)
			D_GOTO(cleanup, rc);
	}

	/* Remove ranks based on to_remove list */
	for (i = 0; i < to_remove->rl_nr; i++) {
		rank = to_remove->rl_ranks[i];
		crt_group_rank_remove_internal(grp_priv, rank);
	}

	D_FREE(idx_to_add);
	d_rank_list_free(to_remove);

	grp_priv->gp_membs_ver = version;
unlock:
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

out:
	return rc;

cleanup:

	D_ERROR("Failure when adding rank %d, rc=%d\n", sec_ranks->rl_ranks[idx_to_add[i]], rc);

	for (k = 0; k < i; k++)
		crt_group_rank_remove_internal(grp_priv, sec_ranks->rl_ranks[idx_to_add[k]]);

	D_FREE(idx_to_add);
	d_rank_list_free(to_remove);

	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	return rc;
}

int
crt_group_psrs_set(crt_group_t *grp, d_rank_list_t *rank_list)
{
	struct crt_grp_priv	*grp_priv;
	struct crt_grp_priv	*prim_grp_priv;
	d_rank_list_t		*copy_rank_list;
	int			i;
	int			rc;

	grp_priv = crt_grp_pub2priv(grp);
	if (grp_priv == NULL) {
		D_ERROR("Failed to lookup grp\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (rank_list == NULL) {
		D_ERROR("Passed rank_list is NULL\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (rank_list->rl_nr == 0) {
		D_ERROR("Passed 0-sized rank_list\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = d_rank_list_dup(&copy_rank_list, rank_list);
	if (rc != 0) {
		D_ERROR("Failed to copy rank list\n");
		D_GOTO(out, rc);
	}

	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
	if (!grp_priv->gp_primary) {
		prim_grp_priv = grp_priv->gp_priv_prim;

		/* Convert all passed secondary ranks to primary */
		for (i = 0; i < copy_rank_list->rl_nr; i++) {
			copy_rank_list->rl_ranks[i] =
				crt_grp_priv_get_primary_rank(
						grp_priv,
						copy_rank_list->rl_ranks[i]);
		}
	} else {
		prim_grp_priv = grp_priv;
	}

	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	D_RWLOCK_WRLOCK(&prim_grp_priv->gp_rwlock);
	if (prim_grp_priv->gp_psr_ranks) {
		D_FREE(prim_grp_priv->gp_psr_ranks);
		prim_grp_priv->gp_psr_ranks = copy_rank_list;
	}
	D_RWLOCK_UNLOCK(&prim_grp_priv->gp_rwlock);
out:
	return rc;
}
