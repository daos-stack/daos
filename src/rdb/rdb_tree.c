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
 * rdb: Trees
 *
 * This file implements an LRU cache of rdb_tree objects, each of which maps a
 * path to the matching dbtree handle. The cache enables us to have at most one
 * handle per tree, while potentially provides better path lookup performance.
 */

#define DDSUBSYS DDFAC(rdb)

#include <daos_srv/rdb.h>

#include "rdb_internal.h"
#include "rdb_layout.h"

static int rdb_tree_lookup_internal(struct rdb *db, const rdb_path_t *path,
				    bool alloc, struct rdb_tree **tree);

struct rdb_tree_open_arg {
	daos_handle_t	deo_base;
	daos_handle_t	deo_parent;
};

static int
rdb_tree_open_path_cb(daos_iov_t *key, void *varg)
{
	struct rdb_tree_open_arg       *arg = varg;
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

/* Open the tree corresponding to path, which is not in the cache. */
static int
rdb_tree_open_path(struct rdb *db, const rdb_path_t *path, daos_handle_t *hdl)
{
	rdb_path_t			p = *path;
	struct rdb_tree		       *tree = NULL;
	struct rdb_tree_open_arg	arg;
	int				rc;

	/* See if we can find a cache hit for a prefix of the path. */
	while (rdb_path_pop(&p) == 0 && p.iov_len > 0) {
		rc = rdb_tree_lookup_internal(db, &p, false /* alloc */,
					      &tree);
		if (rc == 0)
			break;
		else if (rc != -DER_NONEXIST)
			return rc;
	};

	/* Walk through the keys after "p". */
	D_DEBUG(DB_ANY, "walking path %zu from tree %p\n", p.iov_len, tree);
	p.iov_buf += p.iov_len;
	p.iov_buf_len -= p.iov_len;
	p.iov_len = path->iov_len - p.iov_len;
	D__ASSERT(p.iov_len > 0);
	arg.deo_base = tree == NULL ? db->d_attr : tree->de_hdl;
	arg.deo_parent = DAOS_HDL_INVAL;
	rc = rdb_path_iterate(&p, rdb_tree_open_path_cb, &arg);
	if (tree != NULL)
		rdb_tree_put(db, tree);
	if (rc != 0) {
		if (!daos_handle_is_inval(arg.deo_parent))
			dbtree_close(arg.deo_parent);
		return rc;
	}

	D_DEBUG(DB_ANY, "got tree handle "DF_U64"\n", arg.deo_parent.cookie);
	*hdl = arg.deo_parent;
	return 0;
}

static inline struct rdb_tree *
rdb_tree_obj(struct daos_llink *entry)
{
	return container_of(entry, struct rdb_tree, de_entry);
}

static int
rdb_tree_alloc_ref(void *key, unsigned int ksize, void *varg,
		   struct daos_llink **link)
{
	struct rdb	       *db = varg;
	struct rdb_tree	       *tree;
	void		       *buf;
	int			rc;

	D__ALLOC_PTR(tree);
	if (tree == NULL)
		D__GOTO(err, rc = -DER_NOMEM);
	D_INIT_LIST_HEAD(&tree->de_list);

	/* tree->de_path */
	D__ALLOC(buf, ksize);
	if (buf == NULL)
		D__GOTO(err_tree, rc = -DER_NOMEM);
	memcpy(buf, key, ksize);
	daos_iov_set(&tree->de_path, buf, ksize);

	/* tree->de_hdl */
	rc = rdb_tree_open_path(db, &tree->de_path, &tree->de_hdl);
	if (rc != 0)
		D__GOTO(err_path, rc);

	D_DEBUG(DB_ANY, DF_DB": created %p len %u\n", DP_DB(db), tree,
		ksize);
	*link = &tree->de_entry;
	return 0;

err_path:
	D__FREE(tree->de_path.iov_buf, tree->de_path.iov_buf_len);
err_tree:
	D__FREE_PTR(tree);
err:
	return rc;
}

static void
rdb_tree_free_ref(struct daos_llink *llink)
{
	struct rdb_tree *tree = rdb_tree_obj(llink);

	D_DEBUG(DB_ANY, "freeing %p "DF_U64"\n", tree, tree->de_hdl.cookie);
	D__ASSERT(d_list_empty(&tree->de_list));
	dbtree_close(tree->de_hdl);
	D__FREE(tree->de_path.iov_buf, tree->de_path.iov_buf_len);
	D__FREE_PTR(tree);
}

static bool
rdb_tree_cmp_keys(const void *key, unsigned int ksize, struct daos_llink *llink)
{
	struct rdb_tree *tree = rdb_tree_obj(llink);

	if (ksize != tree->de_path.iov_len)
		return false;
	if (memcmp(key, tree->de_path.iov_buf, ksize) != 0)
		return false;
	return true;
}

static struct daos_llink_ops rdb_tree_cache_ops = {
	.lop_alloc_ref	= rdb_tree_alloc_ref,
	.lop_free_ref	= rdb_tree_free_ref,
	.lop_cmp_keys	= rdb_tree_cmp_keys
};

int
rdb_tree_cache_create(struct daos_lru_cache **cache)
{
	return daos_lru_cache_create(5 /* bits */, D_HASH_FT_NOLOCK /* feats */,
				     &rdb_tree_cache_ops, cache);
}

void
rdb_tree_cache_destroy(struct daos_lru_cache *cache)
{
	daos_lru_cache_destroy(cache);
}

static int
rdb_tree_lookup_internal(struct rdb *db, const rdb_path_t *path, bool alloc,
			 struct rdb_tree **tree)
{
	struct daos_llink      *entry;
	int			rc;

	D_DEBUG(DB_ANY, DF_DB": looking up "DF_IOV": alloc=%d\n", DP_DB(db),
		DP_IOV(path), alloc);
	rc = daos_lru_ref_hold(db->d_trees, path->iov_buf, path->iov_len,
			       alloc ? db : NULL, &entry);
	if (rc != 0)
		return rc;
	*tree = rdb_tree_obj(entry);
	return 0;
}

/* Look up path in db's tree cache. */
int
rdb_tree_lookup(struct rdb *db, const rdb_path_t *path, struct rdb_tree **tree)
{
	return rdb_tree_lookup_internal(db, path, true /* alloc */, tree);
}

void
rdb_tree_put(struct rdb *db, struct rdb_tree *tree)
{
	daos_lru_ref_release(db->d_trees, &tree->de_entry);
}

void
rdb_tree_evict(struct rdb *db, struct rdb_tree *tree)
{
	daos_lru_ref_evict(&tree->de_entry);
}
