/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/common.h>
#include "dfs_internal.h"

/** protect against concurrent dfs_init/fini calls */
static pthread_mutex_t	module_lock = PTHREAD_MUTEX_INITIALIZER;

/** refcount on how many times dfs_init has been called */
static int		module_initialized;

/** hashtable for pool open handles */
static struct d_hash_table *poh_hash;

/** hashtable for container open handles */
static struct d_hash_table *coh_hash;

static inline struct dfs_mnt_hdls*
hdl_obj(d_list_t *rlink)
{
	return container_of(rlink, struct dfs_mnt_hdls, entry);
}

static bool
key_cmp(struct d_hash_table *htable, d_list_t *rlink, const void *key, unsigned int ksize)
{
	struct dfs_mnt_hdls *hdl = hdl_obj(rlink);

	return (strncmp(hdl->value, key, ksize) == 0);
}

static void
rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	hdl_obj(rlink)->ref++;
}

static bool
rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfs_mnt_hdls *hdl = hdl_obj(rlink);

	D_ASSERT(hdl->ref > 0);
	hdl->ref--;
	return (hdl->ref == 0);
}

static void
rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfs_mnt_hdls	*hdl = hdl_obj(rlink);
	int			rc;

	D_ASSERT(d_hash_rec_unlinked(&hdl->entry));
	D_ASSERT(hdl->ref == 0);

	if (hdl->type == DFS_H_POOL) {
		rc = daos_pool_disconnect(hdl->handle, NULL);
		if (rc)
			D_ERROR("daos_pool_connect() Failed "DF_RC"\n", DP_RC(rc));
	} else if (hdl->type == DFS_H_CONT) {
		rc = daos_cont_close(hdl->handle, NULL);
		if (rc)
			D_ERROR("daos_cont_close() Failed "DF_RC"\n", DP_RC(rc));
	} else {
		D_ASSERT(0);
	}
	D_FREE(hdl);
}

static uint32_t
rec_hash(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfs_mnt_hdls *hdl = hdl_obj(rlink);

	return d_hash_string_u32(hdl->value, strlen(hdl->value));
}

static d_hash_table_ops_t hdl_hash_ops = {
	.hop_key_cmp = key_cmp,
	.hop_rec_addref = rec_addref,
	.hop_rec_decref = rec_decref,
	.hop_rec_free = rec_free,
	.hop_rec_hash = rec_hash
};

bool
dfs_is_init()
{
	D_MUTEX_LOCK(&module_lock);
	if (module_initialized > 0) {
		D_MUTEX_UNLOCK(&module_lock);
		return true;
	}
	D_MUTEX_UNLOCK(&module_lock);
	return false;
}

int
dfs_init(void)
{
	int	rc;

	D_MUTEX_LOCK(&module_lock);
	if (module_initialized > 0) {
		/** already initialized, report success */
		module_initialized++;
		D_GOTO(unlock, rc = 0);
	}

	rc = daos_init();
	if (rc)
		D_GOTO(unlock, rc = daos_der2errno(rc));

	rc = d_hash_table_create(D_HASH_FT_EPHEMERAL | D_HASH_FT_LRU, 4, NULL, &hdl_hash_ops,
				 &poh_hash);
	if (rc) {
		D_ERROR("Failed to init pool handle hash "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_fini, rc = daos_der2errno(rc));
	}

	rc = d_hash_table_create(D_HASH_FT_EPHEMERAL | D_HASH_FT_LRU, 4, NULL, &hdl_hash_ops,
				 &coh_hash);
	if (rc) {
		D_ERROR("Failed to init container handle hash "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_poh, rc = daos_der2errno(rc));
	}

	module_initialized++;
	D_GOTO(unlock, rc = 0);

out_poh:
	d_hash_table_destroy(poh_hash, false);
out_fini:
	daos_fini();
unlock:
	D_MUTEX_UNLOCK(&module_lock);
	return rc;
}

int
dfs_fini(void)
{
	d_list_t	*rlink;
	int		rc;

	D_MUTEX_LOCK(&module_lock);
	if (module_initialized == 0) {
		/** calling fini without init, report an error */
		D_GOTO(unlock, rc = EINVAL);
	} else if (module_initialized > 1) {
		module_initialized--;
		D_GOTO(unlock, rc = 0);
	}

	while (1) {
		rlink = d_hash_rec_first(coh_hash);
		if (rlink == NULL)
			break;

		d_hash_rec_decref(coh_hash, rlink);
	}
	d_hash_table_destroy(coh_hash, false);
	coh_hash = NULL;

	while (1) {
		rlink = d_hash_rec_first(poh_hash);
		if (rlink == NULL)
			break;

		d_hash_rec_decref(poh_hash, rlink);
	}
	d_hash_table_destroy(poh_hash, false);
	poh_hash = NULL;

	rc = daos_fini();
	if (rc)
		D_GOTO(unlock, rc = daos_der2errno(rc));

	module_initialized = 0;
unlock:
	D_MUTEX_UNLOCK(&module_lock);
	return rc;
}

struct dfs_mnt_hdls *
dfs_hdl_lookup(const char *str, int type, const char *pool)
{
	d_list_t	*rlink = NULL;

	if (type == DFS_H_POOL) {
		rlink = d_hash_rec_find(poh_hash, str, strlen(str) + 1);
	} else if (type == DFS_H_CONT) {
		char		cont_pool[DAOS_PROP_LABEL_MAX_LEN * 2 + 2];
		daos_size_t	len;

		D_ASSERT(pool);
		len = snprintf(cont_pool, DAOS_PROP_LABEL_MAX_LEN * 2 + 2, "%s/%s", pool, str);
		D_ASSERT(len == strlen(pool) + strlen(str) + 1);
		rlink = d_hash_rec_find(coh_hash, cont_pool, len + 1);
	} else {
		D_ASSERT(0);
	}
	if (rlink == NULL)
		return NULL;

	return hdl_obj(rlink);
}

void
dfs_hdl_release(struct dfs_mnt_hdls *hdl)
{
	if (hdl->type == DFS_H_POOL)
		d_hash_rec_decref(poh_hash, &hdl->entry);
	if (hdl->type == DFS_H_CONT)
		d_hash_rec_decref(coh_hash, &hdl->entry);
}

int
dfs_hdl_insert(const char *str, int type, const char *pool, daos_handle_t *oh,
	       struct dfs_mnt_hdls **_hdl)
{
	struct dfs_mnt_hdls	*hdl;
	d_list_t		*rlink;
	int			rc = 0;

	D_ALLOC_PTR(hdl);
	if (hdl == NULL)
		return ENOMEM;

	hdl->type = type;
	hdl->handle.cookie = oh->cookie;
	hdl->ref = 2;
	hdl->value[DAOS_PROP_LABEL_MAX_LEN] = 0;

	if (type == DFS_H_POOL) {
		strncpy(hdl->value, str, DAOS_PROP_LABEL_MAX_LEN + 1);
		rlink = d_hash_rec_find_insert(poh_hash, hdl->value, strlen(hdl->value) + 1,
					       &hdl->entry);
		if (rlink != &hdl->entry) {
			/** handle EEXIST case */
			rc = daos_pool_disconnect(*oh, NULL);
			if (rc)
				D_GOTO(err_free, rc = daos_der2errno(rc));
			D_FREE(hdl);
			hdl = container_of(rlink, struct dfs_mnt_hdls, entry);
			oh->cookie = hdl->handle.cookie;
		}
	} else if (type == DFS_H_CONT) {
		char		cont_pool[DAOS_PROP_LABEL_MAX_LEN * 2 + 1];
		daos_size_t	len;

		D_ASSERT(pool);
		len = snprintf(cont_pool, strlen(pool) + strlen(str) + 2, "%s/%s", pool, str);
		D_ASSERT(len == strlen(pool) + strlen(str) + 1);
		strncpy(hdl->value, cont_pool, len + 1);
		rlink = d_hash_rec_find_insert(coh_hash, hdl->value, len + 1, &hdl->entry);
		if (rlink != &hdl->entry) {
			/** handle EEXIST case */
			rc = daos_cont_close(*oh, NULL);
			if (rc)
				D_GOTO(err_free, rc = daos_der2errno(rc));
			D_FREE(hdl);
			hdl = container_of(rlink, struct dfs_mnt_hdls, entry);
			oh->cookie = hdl->handle.cookie;
		}
	} else {
		D_ASSERT(0);
	}

	*_hdl = hdl;
	return 0;
err_free:
	D_FREE(hdl);
	return rc;
}

int
dfs_hdl_cont_destroy(const char *pool, const char *cont, bool force)
{
	d_list_t		*rlink = NULL;
	struct dfs_mnt_hdls	*hdl;
	char			cont_pool[DAOS_PROP_LABEL_MAX_LEN * 2 + 1];
	daos_size_t		len;

	if (coh_hash == NULL)
		return 0;

	D_ASSERT(pool);
	len = snprintf(cont_pool, strlen(pool) + strlen(cont) + 2, "%s/%s", pool, cont);
	D_ASSERT(len == strlen(pool) + strlen(cont) + 1);

	if (force) {
		if (!d_hash_rec_delete(coh_hash, cont_pool, len + 1))
			return ENOENT;
	}

	rlink = d_hash_rec_find(coh_hash, cont_pool, len + 1);
	if (rlink == NULL)
		return ENOENT;

	hdl = hdl_obj(rlink);
	if (hdl->ref > 2) {
		D_ERROR("Container handle is still open or DFS mount still connected\n");
		return EBUSY;
	}
	d_hash_rec_decref(coh_hash, rlink);
	d_hash_rec_decref(coh_hash, rlink);
	return 0;
}

void
dfs_free_sb_layout(daos_iod_t *iods[])
{
	D_FREE(*iods);
}
