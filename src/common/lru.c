/* Copyright 2016-2022 Intel Corporation
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
 * 4. All publications or advertising materials mentioning features or use of
 *    this software are asked, but not required, to acknowledge that it was
 *    developed by Intel Corporation and credit the contributors.
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
/*
 * This file is part of DAOS
 */
#define D_LOGFAC	DD_FAC(common)

#include <pthread.h>
#include <daos/common.h>
#include <gurt/list.h>
#include <gurt/hash.h>
#include <daos/lru.h>

static inline struct daos_llink*
link2llink(d_list_t *link)
{
	return container_of(link, struct daos_llink, ll_link);
}

static void
lru_hop_rec_addref(struct d_hash_table *htable, d_list_t *link)
{
	struct daos_llink *llink = link2llink(link);

	llink->ll_ref++;
}

static bool
lru_hop_rec_decref(struct d_hash_table *htable, d_list_t *link)
{
	struct daos_llink *llink = link2llink(link);

	D_ASSERT(llink->ll_ref > 0);
	llink->ll_ref--;
	/* Delete from hash only if no more references */
	return llink->ll_ref == 0;
}

static bool
lru_hop_key_cmp(struct d_hash_table *htable, d_list_t *link,
		const void *key, unsigned int ksize)
{
	struct daos_llink *llink = link2llink(link);

	if (llink->ll_evicted)
		return false; /* nobody should use it */
	else
		return llink->ll_ops->lop_cmp_keys(key, ksize, llink);
}

static uint32_t
lru_hop_rec_hash(struct d_hash_table *htable, d_list_t *link)
{
	struct daos_llink *llink = link2llink(link);

	return llink->ll_ops->lop_rec_hash(llink);
}

static void
lru_hop_rec_free(struct d_hash_table *htable, d_list_t *link)
{
	struct daos_llink *llink = link2llink(link);

	llink->ll_ops->lop_free_ref(llink);
}

static d_hash_table_ops_t lru_ops = {
	.hop_key_cmp		= lru_hop_key_cmp,
	.hop_rec_hash		= lru_hop_rec_hash,
	.hop_rec_addref		= lru_hop_rec_addref,
	.hop_rec_decref		= lru_hop_rec_decref,
	.hop_rec_free		= lru_hop_rec_free,
};

int
daos_lru_cache_create(int bits, uint32_t feats,
		      struct daos_llink_ops *ops,
		      struct daos_lru_cache **lcache_pp)
{
	struct daos_lru_cache	*lcache = NULL;
	int			 rc = 0;

	D_DEBUG(DB_TRACE, "Creating a new LRU cache of size (2^%d)\n", bits);

	if (ops == NULL ||
	    ops->lop_cmp_keys  == NULL ||
	    ops->lop_rec_hash  == NULL ||
	    ops->lop_alloc_ref == NULL ||
	    ops->lop_free_ref  == NULL) {
		D_ERROR("Error missing ops/mandatory-ops for LRU cache\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_ALLOC_PTR(lcache);
	if (lcache == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = d_hash_table_create_inplace(feats | D_HASH_FT_LRU,
					 (uint32_t)max_t(int, 4, bits - 3),
					 NULL, &lru_ops, &lcache->dlc_htable);
	if (rc)
		D_GOTO(out, rc);

	if (bits >= 0)
		lcache->dlc_csize = (1U << bits);
	else /* disable LRU */
		lcache->dlc_csize = 0;

	lcache->dlc_count = 0;
	lcache->dlc_ops = ops;
	D_INIT_LIST_HEAD(&lcache->dlc_lru);

	*lcache_pp = lcache;
	lcache = NULL;
out:
	D_FREE(lcache);
	return rc;
}

void
daos_lru_cache_destroy(struct daos_lru_cache *lcache)
{
	if (lcache == NULL)
		return;

	D_DEBUG(DB_TRACE, "Destroying LRU cache\n");
	d_hash_table_debug(&lcache->dlc_htable);
	d_hash_table_destroy_inplace(&lcache->dlc_htable, true);
	D_FREE(lcache);
}

struct lru_evict_arg {
	daos_lru_cond_cb_t	 cb;
	void			*arg;
	d_list_t		 list;
};

static int
lru_evict_cb(d_list_t *link, void *arg)
{
	struct daos_llink *llink = link2llink(link);
	struct lru_evict_arg *cb_arg = arg;

	if (llink->ll_evicted || cb_arg->cb == NULL ||
	    cb_arg->cb(llink, cb_arg->arg)) {
		llink->ll_evicted = 1;
		if (llink->ll_ref == 1) /* the last refcount */
			d_list_move(&llink->ll_qlink, &cb_arg->list);
	}

	return 0;
}

static void
lru_del_evicted(struct daos_lru_cache *lcache,
		struct daos_llink *llink)
{
	D_ASSERT(llink->ll_ref == 1);
	D_ASSERT(lcache->dlc_count > 0);

	d_hash_rec_delete_at(&lcache->dlc_htable, &llink->ll_link);
	lcache->dlc_count--;
}

void
daos_lru_cache_evict(struct daos_lru_cache *lcache,
		     daos_lru_cond_cb_t cond, void *arg)
{
	struct lru_evict_arg	 cb_arg = { .cb = cond, .arg = arg };
	struct daos_llink	*llink;
	struct daos_llink	*tmp;
	unsigned int		 count = 0;
	int			 rc;

	D_INIT_LIST_HEAD(&cb_arg.list);
	rc = d_hash_table_traverse(&lcache->dlc_htable, lru_evict_cb, &cb_arg);
	D_ASSERT(rc == 0);

	d_list_for_each_entry_safe(llink, tmp, &cb_arg.list, ll_qlink) {
		d_list_del_init(&llink->ll_qlink);
		D_DEBUG(DB_TRACE, "Remove %p from LRU cache\n", llink);
		lru_del_evicted(lcache, llink);
		count++;
	}
	D_DEBUG(DB_TRACE, "Evicted %u items, total count %u of %u\n",
		count, lcache->dlc_count, lcache->dlc_csize);
}

int
daos_lru_ref_hold(struct daos_lru_cache *lcache, void *key,
		  unsigned int key_size, void *create_args,
		  struct daos_llink **llink_pp)
{
	struct daos_llink	*llink;
	d_list_t		*link;
	int			 rc = 0;

	D_ASSERT(lcache != NULL && key != NULL && key_size > 0);
	if (lcache->dlc_ops->lop_print_key)
		lcache->dlc_ops->lop_print_key(key, key_size);

	link = d_hash_rec_find(&lcache->dlc_htable, key, key_size);
	if (link != NULL) {
		llink = link2llink(link);
		D_ASSERT(llink->ll_evicted == 0);
		/* remove busy item from LRU */
		if (!d_list_empty(&llink->ll_qlink))
			d_list_del_init(&llink->ll_qlink);
		D_GOTO(found, rc = 0);
	}

	if (create_args == NULL)
		D_GOTO(out, rc = -DER_NONEXIST);

	/* llink does not exist create one */
	rc = lcache->dlc_ops->lop_alloc_ref(key, key_size, create_args, &llink);
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DB_TRACE, "Inserting %p item into LRU Hash table\n", llink);
	llink->ll_evicted = 0;
	llink->ll_ref	  = 1; /* 1 for caller */
	llink->ll_ops	  = lcache->dlc_ops;
	D_INIT_LIST_HEAD(&llink->ll_qlink);

	rc = d_hash_rec_insert(&lcache->dlc_htable, key, key_size,
			       &llink->ll_link, true);
	if (rc) {
		lcache->dlc_ops->lop_free_ref(llink);
		return rc;
	}
	lcache->dlc_count++;
found:
	*llink_pp = llink;
out:
	return rc;
}

void
daos_lru_ref_release(struct daos_lru_cache *lcache, struct daos_llink *llink)
{
	D_ASSERT(lcache != NULL && llink != NULL && llink->ll_ref > 1);
	D_ASSERT(d_list_empty(&llink->ll_qlink));

	llink->ll_ref--;
	if (llink->ll_ref == 1) { /* the last refcount */
		if (lcache->dlc_csize == 0)
			llink->ll_evicted = 1;

		if (llink->ll_evicted) {
			lru_del_evicted(lcache, llink);
		} else {
			D_ASSERT(d_list_empty(&llink->ll_qlink));
			d_list_add(&llink->ll_qlink, &lcache->dlc_lru);
		}
	}

	while (!d_list_empty(&lcache->dlc_lru)) {
		llink = d_list_entry(lcache->dlc_lru.prev, struct daos_llink,
				     ll_qlink);
		if (lcache->dlc_count < lcache->dlc_csize)
			break; /* within threshold and no old item */

		d_list_del_init(&llink->ll_qlink);
		lru_del_evicted(lcache, llink);
	}
}
