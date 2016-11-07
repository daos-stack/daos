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

#include <daos/common.h>

#include <crt_util/list.h>
#include <crt_util/hash.h>

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
	/** Optional print_key function for debugging */
	void	(*lop_print_key)(void *key, unsigned int ksize);
};

struct daos_llink {
	/* LRU hash link */
	crt_list_t		ll_hlink;
	/* LRU queue link */
	crt_list_t		ll_qlink;
	/* Ref count for this reference */
	unsigned int		ll_ref;
	/**
	 * ops to allocate and free reference
	 * for this llink.
	 */
	struct daos_llink_ops	*ll_ops;
};

/**
 * LRU cache implementation using dhash_table
 * and crt_list_t
 */
struct daos_lru_cache {
	/* Provided cache size */
	uint32_t		dlc_csize;
	/* Entries Filled */
	uint32_t		dlc_idle_nr;
	/* unique references held */
	uint32_t		dlc_refs_held;
	/* Queue head, holds idle refs (no refcnt) */
	crt_list_t		dlc_idle_list;
	/* Holds all refs but needs lookup */
	struct dhash_table	dlc_htable;
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

/**
 * Find a ref in the cache \a lcache and take its reference.
 * if reference is not found add it.
 *
 * \param lcache	[IN]	DAOS LRU cache
 * \param key		[IN]	Key to take reference of
 * \param ksize		[IN]	Size of the key
 * \param args		[IN]	Additional arguments required
 *				(For use in alloc cb (optional))
 * \param llink		[OUT]	DAOS LRU link
 */
int
daos_lru_ref_hold(struct daos_lru_cache *lcache, void *key, unsigned int ksize,
		  void *args, struct daos_llink **rlink);

/**
 * Release a reference from the cache and maintain a idle LRU list
 *
 * \param lcache	[IN]	DAOS LRU cache
 * \param llink		[IN]	DAOS LRU link
 */
void
daos_lru_ref_release(struct daos_lru_cache *lcache, struct daos_llink *llink);

#endif
