/**
 * (C) Copyright 2017 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * rdb: KVSs
 *
 * This file implements an LRU cache of rdb_kvs objects, each of which maps a
 * path to the matching dbtree handle. The cache enables us to have at most one
 * handle per tree, while potentially provides better path lookup performance.
 */
#define D_LOGFAC	DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include "rdb_internal.h"
#include "rdb_layout.h"

static int rdb_kvs_lookup_internal(struct rdb *db, const rdb_path_t *path,
				   bool alloc, struct rdb_kvs **kvs);

struct rdb_kvs_open_arg {
	daos_handle_t	deo_base;
	daos_handle_t	deo_parent;
};

static int
rdb_kvs_open_path_cb(daos_iov_t *key, void *varg)
{
	struct rdb_kvs_open_arg	       *arg = varg;
	daos_handle_t			parent;
	daos_handle_t			child;
	int				rc;

	if (daos_handle_is_inval(arg->deo_parent)) {
		/* First key. */
		parent = arg->deo_base;
		if (key->iov_len == 0)
			key = &rdb_attr_root;
	} else {
		parent = arg->deo_parent;
	}
	rc = rdb_open_tree(parent, key, &child);
	if (rc != 0)
		return rc;

	/* Prepare deo_parent for next key lookup. */
	if (!daos_handle_is_inval(arg->deo_parent))
		dbtree_close(arg->deo_parent);
	arg->deo_parent = child;
	return 0;
}

/* Open the KVS corresponding to path, which is not in the cache. */
static int
rdb_kvs_open_path(struct rdb *db, const rdb_path_t *path, daos_handle_t *hdl)
{
	rdb_path_t		p = *path;
	struct rdb_kvs	       *kvs = NULL;
	struct rdb_kvs_open_arg	arg;
	int			rc;

	/* See if we can find a cache hit for a prefix of the path. */
	while (rdb_path_pop(&p) == 0 && p.iov_len > 0) {
		rc = rdb_kvs_lookup_internal(db, &p, false /* alloc */, &kvs);
		if (rc == 0)
			break;
		else if (rc != -DER_NONEXIST)
			return rc;
	};

	/* Walk through the keys after "p". */
	D_DEBUG(DB_ANY, "walking path %zu from kvs %p\n", p.iov_len, kvs);
	p.iov_buf += p.iov_len;
	p.iov_buf_len -= p.iov_len;
	p.iov_len = path->iov_len - p.iov_len;
	D_ASSERT(p.iov_len > 0);
	arg.deo_base = kvs == NULL ? db->d_attr : kvs->de_hdl;
	arg.deo_parent = DAOS_HDL_INVAL;
	rc = rdb_path_iterate(&p, rdb_kvs_open_path_cb, &arg);
	if (kvs != NULL)
		rdb_kvs_put(db, kvs);
	if (rc != 0) {
		if (!daos_handle_is_inval(arg.deo_parent))
			dbtree_close(arg.deo_parent);
		return rc;
	}

	D_DEBUG(DB_ANY, "got kvs handle "DF_U64"\n", arg.deo_parent.cookie);
	*hdl = arg.deo_parent;
	return 0;
}

static inline struct rdb_kvs *
rdb_kvs_obj(struct daos_llink *entry)
{
	return container_of(entry, struct rdb_kvs, de_entry);
}

static int
rdb_kvs_alloc_ref(void *key, unsigned int ksize, void *varg,
		  struct daos_llink **link)
{
	struct rdb     *db = varg;
	struct rdb_kvs *kvs;
	void	       *buf;
	int		rc;

	D_ALLOC_PTR(kvs);
	if (kvs == NULL)
		D_GOTO(err, rc = -DER_NOMEM);
	D_INIT_LIST_HEAD(&kvs->de_list);

	/* kvs->de_path */
	D_ALLOC(buf, ksize);
	if (buf == NULL)
		D_GOTO(err_kvs, rc = -DER_NOMEM);
	memcpy(buf, key, ksize);
	daos_iov_set(&kvs->de_path, buf, ksize);

	/* kvs->de_hdl */
	rc = rdb_kvs_open_path(db, &kvs->de_path, &kvs->de_hdl);
	if (rc != 0)
		D_GOTO(err_path, rc);

	D_DEBUG(DB_ANY, DF_DB": created %p len %u\n", DP_DB(db), kvs, ksize);
	*link = &kvs->de_entry;
	return 0;

err_path:
	D_FREE(kvs->de_path.iov_buf);
err_kvs:
	D_FREE_PTR(kvs);
err:
	return rc;
}

static void
rdb_kvs_free_ref(struct daos_llink *llink)
{
	struct rdb_kvs *kvs = rdb_kvs_obj(llink);

	D_DEBUG(DB_ANY, "freeing %p "DF_U64"\n", kvs, kvs->de_hdl.cookie);
	D_ASSERT(d_list_empty(&kvs->de_list));
	dbtree_close(kvs->de_hdl);
	D_FREE(kvs->de_path.iov_buf);
	D_FREE_PTR(kvs);
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

static struct daos_llink_ops rdb_kvs_cache_ops = {
	.lop_alloc_ref	= rdb_kvs_alloc_ref,
	.lop_free_ref	= rdb_kvs_free_ref,
	.lop_cmp_keys	= rdb_kvs_cmp_keys
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

static int
rdb_kvs_lookup_internal(struct rdb *db, const rdb_path_t *path, bool alloc,
			struct rdb_kvs **kvs)
{
	struct daos_llink      *entry;
	int			rc;

	D_DEBUG(DB_ANY, DF_DB": looking up "DF_IOV": alloc=%d\n", DP_DB(db),
		DP_IOV(path), alloc);
	rc = daos_lru_ref_hold(db->d_kvss, path->iov_buf, path->iov_len,
			       alloc ? db : NULL, &entry);
	if (rc != 0)
		return rc;
	*kvs = rdb_kvs_obj(entry);
	return 0;
}

int
rdb_kvs_lookup(struct rdb *db, const rdb_path_t *path, struct rdb_kvs **kvs)
{
	return rdb_kvs_lookup_internal(db, path, true /* alloc */, kvs);
}

void
rdb_kvs_put(struct rdb *db, struct rdb_kvs *kvs)
{
	daos_lru_ref_release(db->d_kvss, &kvs->de_entry);
}

void
rdb_kvs_evict(struct rdb *db, struct rdb_kvs *kvs)
{
	daos_lru_ref_evict(&kvs->de_entry);
}
