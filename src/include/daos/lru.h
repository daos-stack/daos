/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * In-memory LRU cache for DAOS
 */
#ifndef __DAOS_LRU_H__
#define __DAOS_LRU_H__
#include <gurt/list.h>
#include <gurt/hash.h>
#include <daos/common.h>

struct daos_llink;

struct daos_llink_ops {
	/** Mandatory: lru reference free callback */
	void	 (*lop_free_ref)(struct daos_llink *llink);
	/** Mandatory: If ref not found, allocate and return ref */
	int	 (*lop_alloc_ref)(void *key, unsigned int ksize,
				  void *args, struct daos_llink **link);
	/** Mandatory: Compare keys callback for LRU */
	bool	 (*lop_cmp_keys)(const void *key, unsigned int ksize,
				 struct daos_llink *link);
	/** Mandatory: Get key's hash callback for LRU */
	uint32_t (*lop_rec_hash)(struct daos_llink *link);
	/** Optional print_key function for debugging */
	void	 (*lop_print_key)(void *key, unsigned int ksize);
};

struct daos_llink {
	d_list_t		 ll_link;	/**< LRU hash link */
	d_list_t		 ll_qlink;	/**< Temp link for traverse */
	uint32_t		 ll_ref;	/**< refcount for this ref */
	uint32_t		 ll_evicted:1;	/**< has been evicted */
	struct daos_llink_ops	*ll_ops;	/**< ops to maintain refs */
};

/**
 * LRU cache implementation using d_hash_table and d_list_t
 */
struct daos_lru_cache {
	uint32_t		 dlc_csize;	/**< Provided cache size */
	uint32_t		 dlc_count;	/**< count of refs in cache */
	d_list_t		 dlc_lru;	/**< list head of LRU */
	struct d_hash_table	 dlc_htable;	/**< Hash table for all refs */
	struct daos_llink_ops	*dlc_ops;	/**< ops to maintain refs */
};

/**
 * Create a DAOS LRU cache
 * This function creates an LRU cache in DRAM
 *
 * \param[in]  bits		power2(bits) is the size of the LRU cache
 * \param[in]  feats		Feature bits for DHASH, see DHASH_FT_*
 * \param[in]  ops		DAOS LRU callbacks
 * \param[out] lcache		Newly created LRU cache
 *
 * \return		0 on success and negative on failure.
 */
int
daos_lru_cache_create(int bits, uint32_t feats,
		      struct daos_llink_ops *ops,
		      struct daos_lru_cache **lcache);

/**
 * Destroy an LRU cache
 * This function destroys and LRU cache
 *
 * \param[in] lcache		LRU cache reference
 */
void
daos_lru_cache_destroy(struct daos_lru_cache *lcache);

typedef bool (*daos_lru_cond_cb_t)(struct daos_llink *llink, void *arg);

/**
 * Evict LRU items that can match the condition @cond.
 * All items will be evicted if @cond is NULL.
 *
 * \param[in] lcache		DAOS LRU cache
 * \param[in] cond		the condition callback
 * \param[in] arg		arguments for the @cond
 */
void
daos_lru_cache_evict(struct daos_lru_cache *lcache,
		     daos_lru_cond_cb_t cond, void *arg);

/**
 * Find a ref in the cache \a lcache and take its reference.
 * if reference is not found add it.
 *
 * \param[in] lcache		DAOS LRU cache
 * \param[in] key		Key to take reference of
 * \param[in] ksize		Size of the key
 * \param[in] create_args	Optional, arguments required for allocation
 *				of LRU item.
 *				If user wants a find-only operation, then NULL
 *				should be passed in. User can pass in any
 *				non-zero value as \a create_args if creation
 *				is required but args is not.
 * \param[out] llink		DAOS LRU link
 */
int
daos_lru_ref_hold(struct daos_lru_cache *lcache, void *key, unsigned int ksize,
		  void *create_args, struct daos_llink **llink);

/**
 * Release a reference from the cache
 *
 * \param[in] lcache		DAOS LRU cache
 * \param[in] llink		DAOS LRU link to be released
 */
void
daos_lru_ref_release(struct daos_lru_cache *lcache, struct daos_llink *llink);

/**
 * Flush old items from LRU.
 *
 * \param[in] lcache		DAOS LRU cache
 */
void
daos_lru_ref_flush(struct daos_lru_cache *lcache);

/**
 * Evict the item from LRU after releasing the last refcount on it.
 *
 * \param[in] lcache		DAOS LRU cache
 * \param[in] llink		DAOS LRU item to be evicted
 */
static inline void
daos_lru_ref_evict(struct daos_lru_cache *lcache, struct daos_llink *llink)
{
	llink->ll_evicted = 1;
	d_hash_rec_evict_at(&lcache->dlc_htable, &llink->ll_link);
}

/**
 * Check if a LRU element has been evicted or not
 *
 * \param[in] llink		DAOS LRU item to check
 */
static inline bool
daos_lru_ref_evicted(struct daos_llink *llink)
{
	return llink->ll_evicted;
}

/**
 * Increase a usage reference to the LRU element
 *
 * \param[in] llink		DAOS LRU item
 */
static inline void
daos_lru_ref_add(struct daos_llink *llink)
{
	llink->ll_ref++;
}

#endif
