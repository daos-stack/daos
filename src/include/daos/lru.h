/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * In-memory LRU cache for DAOS
 * daos/lru.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#ifndef __DAOS_LRU_H__
#define __DAOS_LRU_H__
#include <gurt/list.h>
#include <gurt/hash.h>
#include <daos/common.h>

struct daos_llink;
struct daos_llink_ops {
	/** Mandatory: lru reference free callback */
	void	(*lop_free_ref)(struct daos_llink *llink);
	/** Mandatory: If ref not found, allocate and return ref */
	int	(*lop_alloc_ref)(void *key, unsigned int ksize,
				 void *args, struct daos_llink **link);
	/** Mandatory: Compare keys callback for LRU */
	bool	(*lop_cmp_keys)(const void *key, unsigned int ksize,
				struct daos_llink *link);
	/** Mandatory: Get key's hash callback for LRU */
	uint32_t (*lop_rec_hash)(struct daos_llink *link);
	/** Optional print_key function for debugging */
	void	(*lop_print_key)(void *key, unsigned int ksize);
};

struct daos_llink {
	/* LRU hash link */
	d_list_t		ll_hlink;
	/* LRU queue link */
	d_list_t		ll_qlink;
	/* Ref count for this reference */
	unsigned int		ll_ref:30;
	/** has been evicted */
	unsigned int		ll_evicted:1;
	/**
	 * ops to allocate and free reference
	 * for this llink.
	 */
	struct daos_llink_ops	*ll_ops;
};

/**
 * LRU cache implementation using d_hash_table
 * and d_list_t
 */
struct daos_lru_cache {
	/* Provided cache size */
	uint32_t		dlc_csize;
	/* # idle items in the LRU */
	uint32_t		dlc_idle_nr;
	/* # busy items in the LRU (referenced by caller) */
	uint32_t		dlc_busy_nr;
	/* Queue head, holds idle refs (no refcnt) */
	d_list_t		dlc_idle_list;
	/** list head of busy items in the LRU */
	d_list_t		dlc_busy_list;
	/* Holds all refs but needs lookup */
	struct d_hash_table	dlc_htable;
	/* ops to allocate and free reference */
	struct daos_llink_ops	*dlc_ops;
};

/**
 * Create a DAOS LRU cache
 * This function creates an LRU cache in DRAM
 *
 * \param bits		[IN]	power2(bits) is the size
 *				of the LRU cache
 * \feats feats		[IN]	Feature bits for DHASH, see DHASH_FT_*
 * \param ops		[IN]	DAOS LRU callbacks
 * \param lcache	[OUT]	Newly created LRU cache
 *
 * \return			0 on success and negative
 *				on failure.
 */
int
daos_lru_cache_create(int bits, uint32_t feats,
		      struct daos_llink_ops *ops,
		      struct daos_lru_cache **lcache);

/**
 * Destroy an LRU cache
 * This function destroys and LRU cache
 *
 * \param lcache	[IN]	LRU cache reference
 */
void
daos_lru_cache_destroy(struct daos_lru_cache *lcache);

typedef bool (*daos_lru_cond_cb_t)(struct daos_llink *llink, void *args);

/**
 * Evit LRU items that can match the condition @cond. All items will be evicted
 * if @cond is NULL.
 *
 * \param lcache	[IN]	DAOS LRU cache
 * \param cond		[IN]	the condition callback
 * \param args		[IN]	arguments for the @cond
 */
void
daos_lru_cache_evict(struct daos_lru_cache *lcache,
		     daos_lru_cond_cb_t cond, void *args);

/**
 * Find a ref in the cache \a lcache and take its reference.
 * if reference is not found add it.
 *
 * \param lcache	[IN]	DAOS LRU cache
 * \param key		[IN]	Key to take reference of
 * \param ksize		[IN]	Size of the key
 * \param create_args	[IN]	Optional, arguments required for allocation
 *				of LRU item.
 *				If user wants a find-only operation, then NULL
 *				should be passed in. User can pass in any
 *				non-zero value as \a create_args if creation
 *				is required but args is not.
 * \param llink		[OUT]	DAOS LRU link
 */
int
daos_lru_ref_hold(struct daos_lru_cache *lcache, void *key, unsigned int ksize,
		  void *create_args, struct daos_llink **rlink);

/**
 * Release a reference from the cache and maintain a idle LRU list
 *
 * \param lcache	[IN]	DAOS LRU cache
 * \param llink		[IN]	DAOS LRU link
 */
void
daos_lru_ref_release(struct daos_lru_cache *lcache, struct daos_llink *llink);

/**
 * Evict the item from LRU after releasing the last refcount on it.
 *
 * \param llink		[IN]	DAOS LRU item to be evicted.
 */
static inline void
daos_lru_ref_evict(struct daos_llink *llink)
{
	llink->ll_evicted = 1;
}

/**
 * Check if a LRU element has been evicted or not
 *
 * \param llink		[IN]	DAOS LRU item to check
 */
static inline bool
daos_lru_ref_evicted(struct daos_llink *llink)
{
	return llink->ll_evicted;
}

static inline void
daos_lru_ref_add(struct daos_llink *llink)
{
	llink->ll_ref++;
}

#endif
