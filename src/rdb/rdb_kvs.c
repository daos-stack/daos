/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * rdb: KVSs
 *
 * This file implements an LRU cache of rdb_kvs objects, each of which maps a
 * KVS path to the matching VOS object in the LC at the last index. The cache
 * provides better KVS path lookup performance.
 */

#define D_LOGFAC	DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include "rdb_internal.h"
#include "rdb_layout.h"

struct rdb_kvs_open_arg {
	struct rdb     *deo_db;
	rdb_oid_t	deo_parent;
	uint64_t	deo_index;
};

/* Open key in arg->deo_parent. */
static int
rdb_kvs_open_path_cb(d_iov_t *key, void *varg)
{
	struct rdb_kvs_open_arg	       *arg = varg;
	rdb_oid_t			parent = arg->deo_parent;
	d_iov_t			value;

	if (key->iov_len == 0) {
		D_ASSERTF(parent == RDB_LC_ATTRS, DF_X64"\n", parent);
		key = &rdb_lc_root;
	}
	d_iov_set(&value, &arg->deo_parent, sizeof(arg->deo_parent));
	return rdb_lc_lookup(arg->deo_db->d_lc, arg->deo_index, parent, key,
			     &value);
}

/*
 * Open the KVS corresponding to path, which is not in the cache. Currently,
 * the result is just an object ID, since object handles are not exported.
 */
static int
rdb_kvs_open_path(struct rdb *db, uint64_t index, const rdb_path_t *path,
		  rdb_oid_t *object)
{
	rdb_path_t		p = *path;
	struct rdb_kvs	       *kvs = NULL;
	struct rdb_kvs_open_arg	arg;
	int			rc;

	/* See if we can find a cache hit for a prefix of the path. */
	while (rdb_path_pop(&p) == 0 && p.iov_len > 0) {
		rc = rdb_kvs_lookup(db, &p, index, false /* alloc */, &kvs);
		if (rc == 0)
			break;
		else if (rc != -DER_NONEXIST)
			return rc;
	};

	/* Walk through the keys after "p". */
	D_DEBUG(DB_TRACE, "walking path %zu from kvs %p\n", p.iov_len, kvs);
	p.iov_buf += p.iov_len;
	p.iov_buf_len -= p.iov_len;
	p.iov_len = path->iov_len - p.iov_len;
	D_ASSERT(p.iov_len > 0);
	arg.deo_db = db;
	arg.deo_parent = kvs == NULL ? RDB_LC_ATTRS : kvs->de_object;
	arg.deo_index = index;
	rc = rdb_path_iterate(&p, rdb_kvs_open_path_cb, &arg);
	if (kvs != NULL)
		rdb_kvs_put(db, kvs);
	if (rc != 0)
		return rc;

	D_DEBUG(DB_TRACE, "got kvs handle "DF_X64"\n", arg.deo_parent);
	*object = arg.deo_parent;
	return 0;
}

static inline struct rdb_kvs *
rdb_kvs_obj(struct daos_llink *entry)
{
	return container_of(entry, struct rdb_kvs, de_entry);
}

struct rdb_kvs_alloc_arg {
	struct rdb     *dea_db;
	uint64_t	dea_index;
	bool		dea_alloc;
};

static int
rdb_kvs_alloc_ref(void *key, unsigned int ksize, void *varg,
		  struct daos_llink **link)
{
	struct rdb_kvs_alloc_arg       *arg = varg;
	struct rdb_kvs		       *kvs;
	int				rc;

	if (!arg->dea_alloc) {
		rc = -DER_NONEXIST;
		goto err;
	}

	D_ALLOC(kvs, sizeof(*kvs) + ksize);
	if (kvs == NULL) {
		rc = -DER_NOMEM;
		goto err;
	}

	/* kvs->de_path */
	memcpy(kvs->de_buf, key, ksize);
	d_iov_set(&kvs->de_path, kvs->de_buf, ksize);

	/* kvs->de_object */
	rc = rdb_kvs_open_path(arg->dea_db, arg->dea_index, &kvs->de_path,
			       &kvs->de_object);
	if (rc != 0)
		goto err_kvs;

	D_DEBUG(DB_TRACE, DF_DB": created %p len %u\n", DP_DB(arg->dea_db), kvs,
		ksize);
	*link = &kvs->de_entry;
	return 0;

err_kvs:
	D_FREE(kvs);
err:
	return rc;
}

static void
rdb_kvs_free_ref(struct daos_llink *llink)
{
	struct rdb_kvs *kvs = rdb_kvs_obj(llink);

	D_DEBUG(DB_TRACE, "freeing %p "DF_X64"\n", kvs, kvs->de_object);
	D_FREE(kvs);
}

static bool
rdb_kvs_cmp_keys(const void *key, unsigned int ksize, struct daos_llink *llink)
{
	struct rdb_kvs *kvs = rdb_kvs_obj(llink);

	if (ksize != kvs->de_path.iov_len)
		return false;
	if (memcmp(key, kvs->de_path.iov_buf, ksize) != 0)
		return false;
	return true;
}

static uint32_t
rdb_kvs_rec_hash(struct daos_llink *llink)
{
	struct rdb_kvs *kvs = rdb_kvs_obj(llink);

	return d_hash_string_u32((const char *)kvs->de_path.iov_buf,
				 kvs->de_path.iov_len);
}

static struct daos_llink_ops rdb_kvs_cache_ops = {
	.lop_alloc_ref	= rdb_kvs_alloc_ref,
	.lop_free_ref	= rdb_kvs_free_ref,
	.lop_cmp_keys	= rdb_kvs_cmp_keys,
	.lop_rec_hash	= rdb_kvs_rec_hash,
};

int
rdb_kvs_cache_create(struct daos_lru_cache **cache)
{
	return daos_lru_cache_create(5 /* bits */, D_HASH_FT_NOLOCK /* feats */,
				     &rdb_kvs_cache_ops, cache);
}

void
rdb_kvs_cache_destroy(struct daos_lru_cache *cache)
{
	daos_lru_cache_destroy(cache);
}

void
rdb_kvs_cache_evict(struct daos_lru_cache *cache)
{
	daos_lru_cache_evict(cache, NULL /* cond */, NULL /* args */);
}

int
rdb_kvs_lookup(struct rdb *db, const rdb_path_t *path, uint64_t index,
	       bool alloc, struct rdb_kvs **kvs)
{
	struct rdb_kvs_alloc_arg	arg;
	struct daos_llink	       *entry;
	int				rc;

	D_DEBUG(DB_TRACE, DF_DB": looking up "DF_IOV": alloc=%d\n", DP_DB(db),
		DP_IOV(path), alloc);

	arg.dea_db = db;
	arg.dea_index = index;
	arg.dea_alloc = alloc;
	rc = daos_lru_ref_hold(db->d_kvss, path->iov_buf, path->iov_len, &arg,
			       &entry);
	if (rc != 0)
		return rc;

	*kvs = rdb_kvs_obj(entry);
	return 0;
}

void
rdb_kvs_put(struct rdb *db, struct rdb_kvs *kvs)
{
	daos_lru_ref_release(db->d_kvss, &kvs->de_entry);
}

void
rdb_kvs_evict(struct rdb *db, struct rdb_kvs *kvs)
{
	daos_lru_ref_evict(db->d_kvss, &kvs->de_entry);
}
