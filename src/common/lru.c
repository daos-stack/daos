/**
 * (C) Copyright 2016 Intel Corporation.
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
#define DD_SUBSYS	DD_FAC(common)

#include <pthread.h>
#include <daos/common.h>
#include <daos/list.h>
#include <daos/hash.h>
#include <daos/lru.h>

static struct daos_llink*
hash2lru_link(daos_list_t *blink)
{
	return container_of(blink, struct daos_llink, ll_hlink);
}

static void
lru_hop_rec_addref(struct dhash_table *lr_htab, daos_list_t *rlink)
{
	hash2lru_link(rlink)->ll_ref++;
}

static bool
lru_hop_rec_decref(struct dhash_table *lr_htab, daos_list_t *rlink)
{

       struct daos_llink *llink = hash2lru_link(rlink);

       D_ASSERT(llink->ll_ref > 0);
       llink->ll_ref--;
       /* Delete from hash only if no more references */
       return llink->ll_ref == 0;
}

static bool
lru_hop_key_cmp(struct dhash_table *lr_htab, daos_list_t *rlink,
		const void *key, unsigned int ksize)
{
	struct daos_llink *llink = hash2lru_link(rlink);

	return llink->ll_ops->lop_cmp_keys(key, ksize, llink);
}

static void
lru_hop_rec_free(struct dhash_table *lr_htab, daos_list_t *rlink)
{
	struct daos_llink *llink = hash2lru_link(rlink);

	llink->ll_ops->lop_free_ref(llink);
}

static dhash_table_ops_t lru_ops = {
	.hop_key_cmp		= lru_hop_key_cmp,
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

	D_DEBUG(DF_MISC, "Creating a new LRU cache of size (2^%d)\n",
		bits);

	if (ops == NULL ||
	    ops->lop_cmp_keys == NULL ||
	    ops->lop_alloc_ref == NULL ||
	    ops->lop_free_ref == NULL) {
		D_ERROR("Error missing ops/mandatory-ops for LRU cache\n");
		return -DER_INVAL;
	}

	D_ALLOC_PTR(lru_cache);
	if (lru_cache == NULL)
		return -DER_NOMEM;

	rc = dhash_table_create_inplace(feats, max(4, bits - 3),
					NULL, &lru_ops,
					&lru_cache->dlc_htable);
	if (rc)
		D_GOTO(exit, rc = -DER_NOMEM);

	if (bits >= 0)
		lru_cache->dlc_csize = (1 << bits);
	else /* disable LRU */
		lru_cache->dlc_csize = 0;

	lru_cache->dlc_ops = ops;

	DAOS_INIT_LIST_HEAD(&lru_cache->dlc_idle_list);
	DAOS_INIT_LIST_HEAD(&lru_cache->dlc_busy_list);

	*lcache = lru_cache;
exit:
	if (rc != 0)
		D_FREE_PTR(lru_cache);

	return rc;
}

void
daos_lru_cache_destroy(struct daos_lru_cache *lcache)
{

	D_DEBUG(DF_MISC, "Destroying LRU cache\n");
	/**
	 * Cannot destroy if either lcache is NULL or
	 * if there are busy references.
	 */
	D_DEBUG(DF_VOS2, "refs_held :%u\n", lcache->dlc_busy_nr);
	D_ASSERT(lcache && (lcache->dlc_busy_nr == 0));

	dhash_table_debug(&lcache->dlc_htable);
	dhash_table_destroy_inplace(&lcache->dlc_htable, true);
	D_FREE_PTR(lcache);
}

void
daos_lru_cache_evict(struct daos_lru_cache *lcache,
		     daos_lru_cond_cb_t cond, void *args)
{
	struct daos_llink *llink;
	struct daos_llink *tmp;
	unsigned int	   cntr;

	cntr = 0;
	daos_list_for_each_entry(llink, &lcache->dlc_busy_list, ll_qlink) {
		if (cond == NULL || cond(llink, args))
			/* will be evicted later in daos_lru_ref_release */
			daos_lru_ref_evict(llink);
	}
	D_DEBUG(DF_MISC, "Marked %d busy items as evicted\n", cntr);

	cntr = 0;
	daos_list_for_each_entry_safe(llink, tmp, &lcache->dlc_idle_list,
				      ll_qlink) {
		if (cond == NULL || cond(llink, args)) {
			daos_list_del_init(&llink->ll_qlink);
			dhash_rec_delete_at(&lcache->dlc_htable,
					    &llink->ll_hlink);
			lcache->dlc_idle_nr--;
			cntr++;
		}
	}
	D_DEBUG(DF_MISC, "Evicted %d items from idle list\n", cntr);
}

static struct daos_llink *
lru_fast_search(struct daos_lru_cache *lcache, daos_list_t *head,
		void *key, unsigned int key_size)
{
	struct daos_llink *llink;

	if (daos_list_empty(head))
		return NULL;

	llink = daos_list_entry(head->next, struct daos_llink, ll_qlink);
	if (llink->ll_ops->lop_cmp_keys(key, key_size, llink)) {
		D_DEBUG(DF_MISC, "Found item on the %s list.\n",
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
	daos_list_t	*hlink;

	hlink = dhash_rec_find(&lcache->dlc_htable, key, key_size);
	if (hlink == NULL)
		return NULL;

	D_DEBUG(DF_MISC, "Found in the cache hash table\n");
	return hash2lru_link(hlink);
}

static inline void
lru_mark_busy(struct daos_lru_cache *lcache, struct daos_llink *llink)
{
	/**
	 * This reference is about to get busy, lets move it from the idle
	 * list to busy list, and change counters for the cache.
	 */
	D_DEBUG(DF_MISC, "Ref to get busy held: %u, filled :%u\n",
		lcache->dlc_busy_nr, lcache->dlc_idle_nr);

	if (daos_list_empty(&llink->ll_qlink)) { /* new item */
		daos_list_add(&llink->ll_qlink, &lcache->dlc_busy_list);
	} else {
		lcache->dlc_idle_nr--;
		daos_list_move(&llink->ll_qlink, &lcache->dlc_busy_list);
	}
	lcache->dlc_busy_nr++;
}

int
daos_lru_ref_hold(struct daos_lru_cache *lcache, void *key,
		  unsigned int key_size, void *args,
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

	D_DEBUG(DF_MISC, "Entry not found adding it to LRU\n");
	/* llink does not exist create one */
	rc = lcache->dlc_ops->lop_alloc_ref(key, key_size, args, &llink);
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DF_MISC, "Inserting into LRU Hash table\n");
	llink->ll_evicted = 0;
	llink->ll_ref	  = 1; /* 1 for caller */
	llink->ll_ops	  = lcache->dlc_ops;
	DAOS_INIT_LIST_HEAD(&llink->ll_qlink);

	rc = dhash_rec_insert(&lcache->dlc_htable, key, key_size,
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
	D_DEBUG(DF_MISC, "Releasing item %p, ref=%d\n", llink, llink->ll_ref);

	llink->ll_ref--;
	if (llink->ll_ref == 1) { /* the last refcount */
		D_DEBUG(DF_MISC, "busy: %u, idle: %u\n",
			lcache->dlc_busy_nr, lcache->dlc_idle_nr);

		D_ASSERT(lcache->dlc_busy_nr > 0);
		lcache->dlc_busy_nr--;

		if (llink->ll_evicted) {
			D_DEBUG(DF_MISC, "Evict %p from LRU cache\n", llink);
			daos_list_del_init(&llink->ll_hlink);
			/* be freed within hash callback */
			dhash_rec_delete_at(&lcache->dlc_htable,
					    &llink->ll_hlink);
		} else {
			D_DEBUG(DF_MISC, "Moving %p to the idle list\n", llink);
			lcache->dlc_idle_nr++;
			daos_list_move(&llink->ll_qlink,
				       &lcache->dlc_idle_list);
		}
	}

	while (lcache->dlc_idle_nr != 0 &&
	       (lcache->dlc_busy_nr + lcache->dlc_idle_nr >=
		lcache->dlc_csize)) {
		D_DEBUG(DF_MISC, "Evicting from object cache :%d, %d\n",
			lcache->dlc_idle_nr, lcache->dlc_busy_nr);

		/** evict from the tail of the list */
		D_ASSERT(!daos_list_empty(&lcache->dlc_idle_list));
		llink = container_of(lcache->dlc_idle_list.prev,
				     struct daos_llink, ll_qlink);

		daos_list_del_init(&llink->ll_qlink);
		dhash_rec_delete_at(&lcache->dlc_htable, &llink->ll_hlink);
		lcache->dlc_idle_nr--;
	}
	D_DEBUG(DF_MISC, "Done releasing reference\n");
}
