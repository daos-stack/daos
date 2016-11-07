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

#include <pthread.h>
#include <daos/common.h>
#include <daos/lru.h>

#include <crt_util/list.h>
#include <crt_util/hash.h>

/** Define LRU Idle/Busy refs  */
#define        LRU_IDLE_REF 1
#define        LRU_BUSY_REF 2


static struct daos_llink*
lru_hlink2ptr(crt_list_t *blink)
{
	return container_of(blink, struct daos_llink, ll_hlink);
}

static struct daos_llink*
lru_qlink2ptr(crt_list_t *qlink)
{
	return container_of(qlink, struct daos_llink, ll_qlink);
}

static inline void
lru_idle2busy(struct daos_lru_cache *lcache,
		    struct daos_llink *llink)
{
	/**
	 * This reference is about to get busy
	 * lets unlink from idle list and
	 * increment refcnt
	 */
	D_DEBUG(DF_MISC,
		"Ref to get busy held: %u, filled :%u, refcnt: %u\n",
		lcache->dlc_refs_held, lcache->dlc_idle_nr,
		llink->ll_ref);

	if (llink->ll_ref == LRU_BUSY_REF) {
		crt_list_del(&llink->ll_qlink);
		lcache->dlc_idle_nr--;
		lcache->dlc_refs_held++;
	}

	D_DEBUG(DF_MISC,
		"Ref got busy held: %u, filled :%u, refcnt: %u\n",
		lcache->dlc_refs_held, lcache->dlc_idle_nr,
		llink->ll_ref);
}

static void
lru_hop_rec_addref(struct dhash_table *lr_htab, crt_list_t *rlink)
{
	lru_hlink2ptr(rlink)->ll_ref++;
}

static bool
lru_hop_rec_decref(struct dhash_table *lr_htab, crt_list_t *rlink)
{

       struct daos_llink *llink = lru_hlink2ptr(rlink);

       D_ASSERT(llink->ll_ref > 0);
       llink->ll_ref--;

       /* Delete from hash only if no more references */
       return lru_hlink2ptr(rlink)->ll_ref == 0;
}

static bool
lru_hop_key_cmp(struct dhash_table *lr_htab, crt_list_t *rlink,
		const void *key, unsigned int ksize)
{
	struct daos_llink *llink = lru_hlink2ptr(rlink);
	return llink->ll_ops->lop_cmp_keys(key, ksize, llink);
}

static void
lru_hop_rec_free(struct dhash_table *lr_htab, crt_list_t *rlink)
{
	struct daos_llink *llink = lru_hlink2ptr(rlink);

	if (llink->ll_ops != NULL &&
	    llink->ll_ops->lop_free_ref != NULL)
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

	if (ops == NULL || ops->lop_alloc_ref == NULL ||
	    ops->lop_free_ref == NULL || ops->lop_cmp_keys == NULL) {
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

	lru_cache->dlc_csize = (1 << bits);
	lru_cache->dlc_ops = ops;
	CRT_INIT_LIST_HEAD(&lru_cache->dlc_idle_list);
	*lcache = lru_cache;
exit:
	if (rc != 0 && lru_cache != NULL)
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
	D_DEBUG(DF_VOS2, "refs_held :%u\n", lcache->dlc_refs_held);
	D_ASSERT(lcache && (lcache->dlc_refs_held == 0));

	dhash_table_debug(&lcache->dlc_htable);
	dhash_table_destroy_inplace(&lcache->dlc_htable, true);
	D_FREE_PTR(lcache);
}

int
daos_lru_ref_hold(struct daos_lru_cache *lcache, void *ref_key,
		  unsigned int rk_size, void *args,
		  struct daos_llink **rlink)
{
	int			rc = 0;
	struct daos_llink	*qh_link = NULL;
	crt_list_t		*list_entry = NULL;

	D_ASSERT((lcache != NULL) && (ref_key != NULL) && (rk_size > 0));
	if (lcache->dlc_ops->lop_print_key)
		lcache->dlc_ops->lop_print_key(ref_key, rk_size);

	if (lcache->dlc_idle_nr) {
		D_DEBUG(DF_MISC, "LRU checking the head to locate key\n");
		qh_link = crt_list_entry(lcache->dlc_idle_list.next,
					  struct daos_llink, ll_qlink);
		if (qh_link->ll_ops->lop_cmp_keys(ref_key, rk_size, qh_link)) {
			D_DEBUG(DF_MISC,
				"Found at the beginning of LRU queue\n");
			D_ASSERT(qh_link->ll_ref == LRU_IDLE_REF);
			dhash_rec_addref(&lcache->dlc_htable,
					 &qh_link->ll_hlink);
			D_GOTO(make_busy, 0);
		}
	}

	list_entry = dhash_rec_find(&lcache->dlc_htable,
				    ref_key, rk_size);
	if (list_entry == NULL) {
		D_DEBUG(DF_MISC, "Entry not found adding it to LRU\n");
		/* llink does not exist create one */
		rc = lcache->dlc_ops->lop_alloc_ref(ref_key, rk_size,
						    args, &qh_link);
		if (rc)
			return rc;

		D_DEBUG(DF_MISC, "Inserting into LRU Hash table\n");
		qh_link->ll_ref = LRU_IDLE_REF;
		qh_link->ll_ops = lcache->dlc_ops;
		CRT_INIT_LIST_HEAD(&qh_link->ll_hlink);
		rc = dhash_rec_insert(&lcache->dlc_htable, ref_key, rk_size,
				      &qh_link->ll_hlink, true);
		if (rc) {
			D_ERROR("Error in inserting into LRU hash\n");
			return rc;
		}
		lcache->dlc_refs_held++;
		D_GOTO(exit, 0);
	}

	D_DEBUG(DF_MISC, "Key exists in cache, making it busy\n");
	qh_link = lru_hlink2ptr(list_entry);

make_busy:
	lru_idle2busy(lcache, qh_link);
exit:
	*rlink = qh_link;
	return 0;
}

void
daos_lru_ref_release(struct daos_lru_cache *lcache, struct daos_llink *llink)
{
	crt_list_t		*head;
	struct daos_llink	*tlink;

	D_DEBUG(DF_MISC, "Releasing reference from obj cache link: %p\n",
		llink);
	D_ASSERT((lcache != NULL) && (llink != NULL) &&
		 (llink->ll_ref > LRU_IDLE_REF));
	dhash_rec_decref(&lcache->dlc_htable, &llink->ll_hlink);

	/**
	 * Move it to LRU cache idle list
	 */
	if (llink->ll_ref == LRU_IDLE_REF) {
		D_DEBUG(DF_MISC, "Moving to the LRU Cache idle list\n");
		D_DEBUG(DF_MISC, "Held: %u, Filled: %u, Refcnt: %u\n",
			lcache->dlc_refs_held, lcache->dlc_idle_nr,
			llink->ll_ref);
		crt_list_add(&llink->ll_qlink, &lcache->dlc_idle_list);
		D_ASSERT(lcache->dlc_refs_held > 0);
		lcache->dlc_refs_held--;
		lcache->dlc_idle_nr++;
		D_DEBUG(DF_MISC, "Moved: Held: %u, Filled: %u, Refcnt: %u\n",
			lcache->dlc_refs_held, lcache->dlc_idle_nr,
			llink->ll_ref);
	}

	while (lcache->dlc_idle_nr != 0 &&
	       (lcache->dlc_refs_held + lcache->dlc_idle_nr >=
		lcache->dlc_csize)) {
		D_DEBUG(DF_MISC, "Evicting from object cache :%d, %d\n",
			lcache->dlc_idle_nr, lcache->dlc_refs_held);
		head = &lcache->dlc_idle_list;
		/** evict from the tail of the list */
		tlink = lru_qlink2ptr(head->prev);
		crt_list_del(&tlink->ll_qlink);
		dhash_rec_delete_at(&lcache->dlc_htable,
				    &tlink->ll_hlink);
		lcache->dlc_idle_nr--;
	}
	D_DEBUG(DF_MISC, "Done releasing reference\n");
}
