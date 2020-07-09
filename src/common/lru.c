/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
/*
 * This file is part of DAOSM
 *
 * common/lru.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define D_LOGFAC	DD_FAC(common)

#include <pthread.h>
#include <daos/common.h>
#include <gurt/list.h>
#include <gurt/hash.h>
#include <daos/lru.h>

static struct daos_llink*
hash2lru_link(d_list_t *blink)
{
	return container_of(blink, struct daos_llink, ll_hlink);
}

static void
lru_hop_rec_addref(struct d_hash_table *lr_htab, d_list_t *rlink)
{
	hash2lru_link(rlink)->ll_ref++;
}

static bool
lru_hop_rec_decref(struct d_hash_table *lr_htab, d_list_t *rlink)
{

       struct daos_llink *llink = hash2lru_link(rlink);

       D_ASSERT(llink->ll_ref > 0);
       llink->ll_ref--;
       /* Delete from hash only if no more references */
       return llink->ll_ref == 0;
}

static bool
lru_hop_key_cmp(struct d_hash_table *lr_htab, d_list_t *rlink,
		const void *key, unsigned int ksize)
{
	struct daos_llink *llink = hash2lru_link(rlink);

	if (llink->ll_evicted)
		return false; /* nobody should use it */
	else
		return llink->ll_ops->lop_cmp_keys(key, ksize, llink);
}

static uint32_t
lru_hop_rec_hash(struct d_hash_table *htable, d_list_t *link)
{
	struct daos_llink *llink = hash2lru_link(link);

	return llink->ll_ops->lop_rec_hash(llink);
}

static void
lru_hop_rec_free(struct d_hash_table *lr_htab, d_list_t *rlink)
{
	struct daos_llink *llink = hash2lru_link(rlink);

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
		      struct daos_lru_cache **lcache)
{
	struct daos_lru_cache	*lru_cache = NULL;
	int			rc = 0;

	D_DEBUG(DB_TRACE, "Creating a new LRU cache of size (2^%d)\n",
		bits);

	if (ops == NULL ||
	    ops->lop_cmp_keys == NULL ||
	    ops->lop_rec_hash  == NULL ||
	    ops->lop_alloc_ref == NULL ||
	    ops->lop_free_ref == NULL) {
		D_ERROR("Error missing ops/mandatory-ops for LRU cache\n");
		return -DER_INVAL;
	}

	D_ALLOC_PTR(lru_cache);
	if (lru_cache == NULL)
		return -DER_NOMEM;

	rc = d_hash_table_create_inplace(feats, max(4, bits - 3),
					 NULL, &lru_ops,
					 &lru_cache->dlc_htable);
	if (rc)
		D_GOTO(exit, rc = -DER_NOMEM);

	if (bits >= 0)
		lru_cache->dlc_csize = (1 << bits);
	else /* disable LRU */
		lru_cache->dlc_csize = 0;

	lru_cache->dlc_ops = ops;

	D_INIT_LIST_HEAD(&lru_cache->dlc_idle_list);
	D_INIT_LIST_HEAD(&lru_cache->dlc_busy_list);

	*lcache = lru_cache;
exit:
	if (rc != 0)
		D_FREE(lru_cache);

	return rc;
}

void
daos_lru_cache_destroy(struct daos_lru_cache *lcache)
{

	D_DEBUG(DB_TRACE, "Destroying LRU cache\n");
	/**
	 * Cannot destroy if either lcache is NULL or
	 * if there are busy references.
	 */
	D_DEBUG(DB_TRACE, "refs_held :%u\n", lcache->dlc_busy_nr);
	D_ASSERTF(lcache->dlc_busy_nr == 0, "busy=%d", lcache->dlc_busy_nr);

	d_hash_table_debug(&lcache->dlc_htable);
	d_hash_table_destroy_inplace(&lcache->dlc_htable, true);
	D_FREE(lcache);
}

void
daos_lru_cache_evict(struct daos_lru_cache *lcache,
		     daos_lru_cond_cb_t cond, void *args)
{
	struct daos_llink *llink;
	struct daos_llink *tmp;
	unsigned int	   cntr;

	cntr = 0;
	d_list_for_each_entry(llink, &lcache->dlc_busy_list, ll_qlink) {
		if (cond == NULL || cond(llink, args)) {
			/* will be evicted later in daos_lru_ref_release */
			daos_lru_ref_evict(llink);
			cntr++;
		}
	}
	D_DEBUG(DB_TRACE, "Marked %d busy items as evicted\n", cntr);

	cntr = 0;
	d_list_for_each_entry_safe(llink, tmp, &lcache->dlc_idle_list,
				   ll_qlink) {
		if (cond == NULL || cond(llink, args)) {
			d_list_del_init(&llink->ll_qlink);
			d_hash_rec_delete_at(&lcache->dlc_htable,
					     &llink->ll_hlink);
			lcache->dlc_idle_nr--;
			cntr++;
		}
	}
	D_DEBUG(DB_TRACE, "Evicted %d items from idle list\n", cntr);
}

static struct daos_llink *
lru_fast_search(struct daos_lru_cache *lcache, d_list_t *head,
		void *key, unsigned int key_size)
{
	struct daos_llink *llink;

	if (d_list_empty(head))
		return NULL;

	llink = d_list_entry(head->next, struct daos_llink, ll_qlink);
	if (llink->ll_evicted)
		return NULL;

	if (llink->ll_ops->lop_cmp_keys(key, key_size, llink)) {
		D_DEBUG(DB_TRACE, "Found item on the %s list.\n",
			head == &lcache->dlc_busy_list ? "busy" : "idle");

		llink->ll_ref++; /* +1 for caller */
		return llink;
	}
	return NULL;
}

struct daos_llink *
lru_hash_search(struct daos_lru_cache *lcache, void *key,
		unsigned int key_size)
{
	d_list_t	*hlink;

	hlink = d_hash_rec_find(&lcache->dlc_htable, key, key_size);
	if (hlink == NULL)
		return NULL;

	D_DEBUG(DB_TRACE, "Found in the cache hash table\n");
	return hash2lru_link(hlink);
}

static inline void
lru_mark_busy(struct daos_lru_cache *lcache, struct daos_llink *llink)
{
	/**
	 * This reference is about to get busy, lets move it from the idle
	 * list to busy list, and change counters for the cache.
	 */
	D_DEBUG(DB_TRACE, "Ref to get busy held: %u, filled :%u\n",
		lcache->dlc_busy_nr, lcache->dlc_idle_nr);

	if (d_list_empty(&llink->ll_qlink)) { /* new item */
		d_list_add(&llink->ll_qlink, &lcache->dlc_busy_list);
	} else {
		lcache->dlc_idle_nr--;
		d_list_move(&llink->ll_qlink, &lcache->dlc_busy_list);
	}
	lcache->dlc_busy_nr++;
}

int
daos_lru_ref_hold(struct daos_lru_cache *lcache, void *key,
		  unsigned int key_size, void *create_args,
		  struct daos_llink **rlink)
{
	struct daos_llink *llink;
	int		   rc;

	D_ASSERT(lcache != NULL && key != NULL && key_size > 0);
	if (lcache->dlc_ops->lop_print_key)
		lcache->dlc_ops->lop_print_key(key, key_size);

	llink = lru_fast_search(lcache, &lcache->dlc_busy_list, key, key_size);
	if (llink)
		D_GOTO(found, rc = 0);

	llink = lru_fast_search(lcache, &lcache->dlc_idle_list, key, key_size);
	if (llink)
		D_GOTO(found, rc = 0);

	llink = lru_hash_search(lcache, key, key_size);
	if (llink)
		D_GOTO(found, rc = 0);

	if (!create_args)
		D_GOTO(out, rc = -DER_NONEXIST);

	D_DEBUG(DB_TRACE, "Entry not found adding it to LRU\n");
	/* llink does not exist create one */
	rc = lcache->dlc_ops->lop_alloc_ref(key, key_size, create_args, &llink);
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DB_TRACE, "Inserting into LRU Hash table\n");
	llink->ll_evicted = 0;
	llink->ll_ref	  = 1; /* 1 for caller */
	llink->ll_ops	  = lcache->dlc_ops;
	D_INIT_LIST_HEAD(&llink->ll_qlink);

	rc = d_hash_rec_insert(&lcache->dlc_htable, key, key_size,
			       &llink->ll_hlink, true);
	D_ASSERT(rc == 0);
found:
	if (llink->ll_ref == 2) /* 1 for hash, 1 for the first holder */
		lru_mark_busy(lcache, llink);

	*rlink = llink;
out:
	return rc;
}

void
daos_lru_ref_release(struct daos_lru_cache *lcache, struct daos_llink *llink)
{
	D_ASSERT(lcache != NULL && llink != NULL && llink->ll_ref > 1);
	D_DEBUG(DB_TRACE, "Releasing item %p, ref=%d\n", llink, llink->ll_ref);

	llink->ll_ref--;
	if (llink->ll_ref == 1) { /* the last refcount */
		D_DEBUG(DB_TRACE, "busy: %u, idle: %u\n",
			lcache->dlc_busy_nr, lcache->dlc_idle_nr);

		D_ASSERT(lcache->dlc_busy_nr > 0);
		lcache->dlc_busy_nr--;

		if (llink->ll_evicted) {
			D_DEBUG(DB_TRACE, "Evict %p from LRU cache\n", llink);
			d_list_del_init(&llink->ll_qlink);
			/* be freed within hash callback */
			d_hash_rec_delete_at(&lcache->dlc_htable,
					     &llink->ll_hlink);
		} else {
			D_DEBUG(DB_TRACE,
				"Moving %p to the idle list\n", llink);
			lcache->dlc_idle_nr++;
			d_list_move(&llink->ll_qlink, &lcache->dlc_idle_list);
		}
	}

	while (lcache->dlc_idle_nr != 0 &&
	       (lcache->dlc_busy_nr + lcache->dlc_idle_nr >=
		lcache->dlc_csize)) {
		D_DEBUG(DB_TRACE, "Evicting from object cache :%d, %d\n",
			lcache->dlc_idle_nr, lcache->dlc_busy_nr);

		/** evict from the tail of the list */
		D_ASSERT(!d_list_empty(&lcache->dlc_idle_list));
		llink = container_of(lcache->dlc_idle_list.prev,
				     struct daos_llink, ll_qlink);

		d_list_del_init(&llink->ll_qlink);
		lcache->dlc_idle_nr--;
		/* NB. hash entry free could yield */
		d_hash_rec_delete_at(&lcache->dlc_htable, &llink->ll_hlink);
	}
	D_DEBUG(DB_TRACE, "Done releasing reference\n");
}
