/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf_metadata_concurrency.h"
#include "../metadata/metadata_misc.h"
#include "../ocf_queue_priv.h"

int ocf_metadata_concurrency_init(struct ocf_metadata_lock *metadata_lock)
{
	int err = 0;
	unsigned lru_iter;
	unsigned part_iter;
	unsigned global_iter;

	for (lru_iter = 0; lru_iter < OCF_NUM_LRU_LISTS; lru_iter++)
		env_rwlock_init(&metadata_lock->lru[lru_iter]);

	for (global_iter = 0; global_iter < OCF_NUM_GLOBAL_META_LOCKS;
			global_iter++) {
		err = env_rwsem_init(&metadata_lock->global[global_iter].sem);
		if (err)
			goto global_err;
	}

	for (part_iter = 0; part_iter < OCF_USER_IO_CLASS_MAX; part_iter++) {
		err = env_spinlock_init(&metadata_lock->partition[part_iter]);
		if (err)
			goto partition_err;
	}

	return err;

partition_err:
	while (part_iter--)
		env_spinlock_destroy(&metadata_lock->partition[part_iter]);

global_err:
	while (global_iter--)
		env_rwsem_destroy(&metadata_lock->global[global_iter].sem);

	while (lru_iter--)
		env_rwlock_destroy(&metadata_lock->lru[lru_iter]);

	return err;
}

void ocf_metadata_concurrency_deinit(struct ocf_metadata_lock *metadata_lock)
{
	unsigned i;

	for (i = 0; i < OCF_USER_IO_CLASS_MAX; i++)
		env_spinlock_destroy(&metadata_lock->partition[i]);

	for (i = 0; i < OCF_NUM_LRU_LISTS; i++)
		env_rwlock_destroy(&metadata_lock->lru[i]);

	for (i = 0; i < OCF_NUM_GLOBAL_META_LOCKS; i++)
		env_rwsem_destroy(&metadata_lock->global[i].sem);
}

int ocf_metadata_concurrency_attached_init(
		struct ocf_metadata_lock *metadata_lock, ocf_cache_t cache,
		uint32_t hash_table_entries, uint32_t colision_table_pages)
{
	uint32_t i;
	int err = 0;

	metadata_lock->hash = env_vzalloc(sizeof(env_rwsem) *
			hash_table_entries);
	metadata_lock->collision_pages = env_vzalloc(sizeof(env_rwsem) *
			colision_table_pages);
	if (!metadata_lock->hash ||
			!metadata_lock->collision_pages) {
		env_vfree(metadata_lock->hash);
		env_vfree(metadata_lock->collision_pages);
		metadata_lock->hash = NULL;
		metadata_lock->collision_pages = NULL;
		return -OCF_ERR_NO_MEM;
	}

	for (i = 0; i < hash_table_entries; i++) {
		err = env_rwsem_init(&metadata_lock->hash[i]);
		if (err)
			 break;
	}
	if (err) {
		while (i--)
			env_rwsem_destroy(&metadata_lock->hash[i]);
		env_vfree(metadata_lock->hash);
		metadata_lock->hash = NULL;
		ocf_metadata_concurrency_attached_deinit(metadata_lock);
		return err;
	}


	for (i = 0; i < colision_table_pages; i++) {
		err = env_rwsem_init(&metadata_lock->collision_pages[i]);
		if (err)
			break;
	}
	if (err) {
		while (i--)
			env_rwsem_destroy(&metadata_lock->collision_pages[i]);
		env_vfree(metadata_lock->collision_pages);
		metadata_lock->collision_pages = NULL;
		ocf_metadata_concurrency_attached_deinit(metadata_lock);
		return err;
	}

	metadata_lock->cache = cache;
	metadata_lock->num_hash_entries = hash_table_entries;
	metadata_lock->num_collision_pages = colision_table_pages;

	return 0;
}

void ocf_metadata_concurrency_attached_deinit(
		struct ocf_metadata_lock *metadata_lock)
{
	uint32_t i;

	if (metadata_lock->hash) {
		for (i = 0; i < metadata_lock->num_hash_entries; i++)
			env_rwsem_destroy(&metadata_lock->hash[i]);
		env_vfree(metadata_lock->hash);
		metadata_lock->hash = NULL;
		metadata_lock->num_hash_entries = 0;
	}

	if (metadata_lock->collision_pages) {
		for (i = 0; i < metadata_lock->num_collision_pages; i++)
			env_rwsem_destroy(&metadata_lock->collision_pages[i]);
		env_vfree(metadata_lock->collision_pages);
		metadata_lock->collision_pages = NULL;
		metadata_lock->num_collision_pages = 0;
	}
}

void ocf_metadata_start_exclusive_access(
		struct ocf_metadata_lock *metadata_lock)
{
	unsigned i;

	for (i = 0; i < OCF_NUM_GLOBAL_META_LOCKS; i++) {
		env_rwsem_down_write(&metadata_lock->global[i].sem);
	}
}

int ocf_metadata_try_start_exclusive_access(
		struct ocf_metadata_lock *metadata_lock)
{
	unsigned i;
	int error;

	for (i = 0; i < OCF_NUM_GLOBAL_META_LOCKS; i++) {
		error =  env_rwsem_down_write_trylock(&metadata_lock->global[i].sem);
		if (error)
			break;
	}

	if (error) {
		while (i--) {
			env_rwsem_up_write(&metadata_lock->global[i].sem);
		}
	}

	return error;
}

void ocf_metadata_end_exclusive_access(
		struct ocf_metadata_lock *metadata_lock)
{
	unsigned i;

	for (i = OCF_NUM_GLOBAL_META_LOCKS; i > 0; i--)
	        env_rwsem_up_write(&metadata_lock->global[i - 1].sem);
}

/* lock_idx determines which of underlying R/W locks is acquired for read. The goal
   is to spread calls across all available underlying locks to reduce contention
   on one single RW semaphor primitive. Technically any value is correct, but
   picking wisely would allow for higher read througput:
   * free running per-cpu counter sounds good,
   * for rarely excercised code paths (e.g. management) any value would do.
*/
void ocf_metadata_start_shared_access(
		struct ocf_metadata_lock *metadata_lock,
		unsigned lock_idx)
{
        env_rwsem_down_read(&metadata_lock->global[lock_idx].sem);
}

int ocf_metadata_try_start_shared_access(
		struct ocf_metadata_lock *metadata_lock,
		unsigned lock_idx)
{
	return env_rwsem_down_read_trylock(&metadata_lock->global[lock_idx].sem);
}

void ocf_metadata_end_shared_access(struct ocf_metadata_lock *metadata_lock,
		unsigned lock_idx)
{
        env_rwsem_up_read(&metadata_lock->global[lock_idx].sem);
}

/* NOTE: Calling 'naked' lock/unlock requires caller to hold global metadata
	 shared (aka read) lock
   NOTE: Using 'naked' variants to lock multiple hash buckets is prone to
	 deadlocks if not locking in the the order of increasing hash bucket
	 number. Preffered way to lock multiple hash buckets is to use
	 request lock rountines ocf_req_hash_(un)lock_(rd/wr).
*/
static inline void ocf_hb_id_naked_lock(
		struct ocf_metadata_lock *metadata_lock,
		ocf_cache_line_t hash, int rw)
{
	ENV_BUG_ON(hash >= metadata_lock->num_hash_entries);

	if (rw == OCF_METADATA_WR)
		env_rwsem_down_write(&metadata_lock->hash[hash]);
	else if (rw == OCF_METADATA_RD)
		env_rwsem_down_read(&metadata_lock->hash[hash]);
	else
		ENV_BUG();
}

static inline void ocf_hb_id_naked_unlock(
		struct ocf_metadata_lock *metadata_lock,
		ocf_cache_line_t hash, int rw)
{
	ENV_BUG_ON(hash >= metadata_lock->num_hash_entries);

	if (rw == OCF_METADATA_WR)
		env_rwsem_up_write(&metadata_lock->hash[hash]);
	else if (rw == OCF_METADATA_RD)
		env_rwsem_up_read(&metadata_lock->hash[hash]);
	else
		ENV_BUG();
}

static int ocf_hb_id_naked_trylock(struct ocf_metadata_lock *metadata_lock,
		ocf_cache_line_t hash, int rw)
{
	int result = -1;

	ENV_BUG_ON(hash >= metadata_lock->num_hash_entries);

	if (rw == OCF_METADATA_WR) {
		result = env_rwsem_down_write_trylock(
				&metadata_lock->hash[hash]);
	} else if (rw == OCF_METADATA_RD) {
		result = env_rwsem_down_read_trylock(
				&metadata_lock->hash[hash]);
	} else {
		ENV_BUG();
	}


	return result;
}

bool ocf_hb_cline_naked_trylock_wr(struct ocf_metadata_lock *metadata_lock,
		uint32_t core_id, uint64_t core_line)
{
	ocf_cache_line_t hash = ocf_metadata_hash_func(metadata_lock->cache,
			core_line, core_id);

	return (0 == ocf_hb_id_naked_trylock(metadata_lock, hash,
				OCF_METADATA_WR));
}

bool ocf_hb_cline_naked_trylock_rd(struct ocf_metadata_lock *metadata_lock,
		uint32_t core_id, uint64_t core_line)
{
	ocf_cache_line_t hash = ocf_metadata_hash_func(metadata_lock->cache,
			core_line, core_id);

	return (0 == ocf_hb_id_naked_trylock(metadata_lock, hash,
				OCF_METADATA_RD));
}

void ocf_hb_cline_naked_unlock_rd(struct ocf_metadata_lock *metadata_lock,
		uint32_t core_id, uint64_t core_line)
{
	ocf_cache_line_t hash = ocf_metadata_hash_func(metadata_lock->cache,
			core_line, core_id);

	ocf_hb_id_naked_unlock(metadata_lock, hash, OCF_METADATA_RD);
}

void ocf_hb_cline_naked_unlock_wr(struct ocf_metadata_lock *metadata_lock,
		uint32_t core_id, uint64_t core_line)
{
	ocf_cache_line_t hash = ocf_metadata_hash_func(metadata_lock->cache,
			core_line, core_id);

	ocf_hb_id_naked_unlock(metadata_lock, hash, OCF_METADATA_WR);
}

/* common part of protected hash bucket lock routines */
static inline void ocf_hb_id_prot_lock_common(
		struct ocf_metadata_lock *metadata_lock,
		uint32_t lock_idx, ocf_cache_line_t hash, int rw)
{
	ocf_metadata_start_shared_access(metadata_lock, lock_idx);
	ocf_hb_id_naked_lock(metadata_lock, hash, rw);
}

/* common part of protected hash bucket unlock routines */
static inline void ocf_hb_id_prot_unlock_common(
		struct ocf_metadata_lock *metadata_lock,
		uint32_t lock_idx, ocf_cache_line_t hash, int rw)
{
	ocf_hb_id_naked_unlock(metadata_lock, hash, rw);
	ocf_metadata_end_shared_access(metadata_lock, lock_idx);
}

/* NOTE: caller can lock at most one hash bucket at a time using protected
	variants of lock routines. */
void ocf_hb_cline_prot_lock_wr(struct ocf_metadata_lock *metadata_lock,
		uint32_t lock_idx, uint32_t core_id, uint64_t core_line)
{
	ocf_cache_line_t hash = ocf_metadata_hash_func(metadata_lock->cache,
			core_line, core_id);

	ocf_hb_id_prot_lock_common(metadata_lock, lock_idx,
			hash, OCF_METADATA_WR);
}

void ocf_hb_cline_prot_unlock_wr(struct ocf_metadata_lock *metadata_lock,
		uint32_t lock_idx, uint32_t core_id, uint64_t core_line)
{
	ocf_cache_line_t hash = ocf_metadata_hash_func(metadata_lock->cache,
			core_line, core_id);

	ocf_hb_id_prot_unlock_common(metadata_lock, lock_idx,
			hash, OCF_METADATA_WR);
}

void ocf_hb_cline_prot_lock_rd(struct ocf_metadata_lock *metadata_lock,
		uint32_t lock_idx, uint32_t core_id, uint64_t core_line)
{
	ocf_cache_line_t hash = ocf_metadata_hash_func(metadata_lock->cache,
			core_line, core_id);

	ocf_hb_id_prot_lock_common(metadata_lock, lock_idx,
			hash, OCF_METADATA_RD);
}

void ocf_hb_cline_prot_unlock_rd(struct ocf_metadata_lock *metadata_lock,
		uint32_t lock_idx, uint32_t core_id, uint64_t core_line)
{
	ocf_cache_line_t hash = ocf_metadata_hash_func(metadata_lock->cache,
			core_line, core_id);

	ocf_hb_id_prot_unlock_common(metadata_lock, lock_idx,
			hash, OCF_METADATA_RD);
}

void ocf_hb_id_prot_lock_wr(struct ocf_metadata_lock *metadata_lock,
		unsigned lock_idx, ocf_cache_line_t hash)
{
	ocf_hb_id_prot_lock_common(metadata_lock, lock_idx, hash,
			OCF_METADATA_WR);
}

void ocf_hb_id_prot_unlock_wr(struct ocf_metadata_lock *metadata_lock,
		unsigned lock_idx, ocf_cache_line_t hash)
{
	ocf_hb_id_prot_unlock_common(metadata_lock, lock_idx, hash,
			OCF_METADATA_WR);
}

/* number of hash entries */
#define _NUM_HASH_ENTRIES req->cache->metadata.lock.num_hash_entries

/* true if hashes are monotonic */
#define _IS_MONOTONIC(req) (req->map[0].hash + req->core_line_count <= \
		_NUM_HASH_ENTRIES)

/* minimal hash value */
#define _MIN_HASH(req) (_IS_MONOTONIC(req) ? req->map[0].hash : 0)

/* maximal hash value */
#define _MAX_HASH(req) (_IS_MONOTONIC(req) ? \
		req->map[req->core_line_count - 1].hash : \
		_NUM_HASH_ENTRIES - 1)

/* number of unique hash values in request */
#define _HASH_COUNT(req) OCF_MIN(req->core_line_count, _NUM_HASH_ENTRIES)

/* true if there is a gap in hash values */
#define _HAS_GAP(req) (_MAX_HASH(req) - _MIN_HASH(req) + 1 > _HASH_COUNT(req))

/* gap size */
#define _GAP_VAL(req) ((_MAX_HASH(req) - _MIN_HASH(req) + 1) - _HASH_COUNT(req))

/* hash value after which there is a gap */
#define _GAP_START(req) req->map[req->core_line_count - 1].hash

/* get next hash value */
#define _HASH_NEXT(req, hash) (hash + 1 + \
		((_HAS_GAP(req) && hash == _GAP_START(req)) ? _GAP_VAL(req) : 0))

/*
 * Iterate over hash buckets for all core lines in the request in ascending hash
 * bucket value order. Each hash bucket is visited only once.
 *
 * @hash stores hash values for each iteration
 *
 * Example hash iteration order for _NUM_HASH_ENTRIES == 5:
 *   Request hashes			Iteration order
 *   [2, 3, 4]				[2, 3, 4]
 *   [2, 3, 4, 0]		 	[0, 2, 3, 4]
 *   [2, 3, 4, 0, 1, 2, 3, 4, 0, 1]   	[0, 1, 2, 3, 4]
 *   [4, 0]				[0, 4]
 *   [0, 1, 2, 3, 4, 0, 1]		[0, 1, 2, 3, 4]
 *
 */
#define for_each_req_hash_asc(req, hash) \
	for (hash = _MIN_HASH(req); hash <= _MAX_HASH(req); \
			hash = _HASH_NEXT(req, hash))

/* Returns true if the the given LBA (determined by core_id
 * and core_line) resolves to a hash value that is within the
 * set of hashes for the given request (i.e. after the request
 * hash bucket are locked, the given core line is hash bucket
 * locked as well).
 */
bool ocf_req_hash_in_range(struct ocf_request *req,
		ocf_core_id_t core_id, uint64_t core_line)
{
	ocf_cache_line_t hash = ocf_metadata_hash_func(
			req->cache, core_line, core_id);

	if (!_HAS_GAP(req)) {
		return (hash >= _MIN_HASH(req) &&
				hash <= _MAX_HASH(req));
	}

	return (hash >= _MIN_HASH(req) && hash <= _GAP_START(req)) ||
		(hash > _GAP_START(req) + _GAP_VAL(req) &&
				hash <=  _MAX_HASH(req));
}

void ocf_hb_req_prot_lock_rd(struct ocf_request *req)
{
	ocf_cache_line_t hash;

	ocf_metadata_start_shared_access(&req->cache->metadata.lock,
			req->lock_idx);
	for_each_req_hash_asc(req, hash) {
		ocf_hb_id_naked_lock(&req->cache->metadata.lock, hash,
				OCF_METADATA_RD);
	}
}

void ocf_hb_req_prot_unlock_rd(struct ocf_request *req)
{
	ocf_cache_line_t hash;

	for_each_req_hash_asc(req, hash) {
		ocf_hb_id_naked_unlock(&req->cache->metadata.lock, hash,
				OCF_METADATA_RD);
	}
	ocf_metadata_end_shared_access(&req->cache->metadata.lock,
			req->lock_idx);
}

void ocf_hb_req_prot_lock_wr(struct ocf_request *req)
{
	ocf_cache_line_t hash;

	ocf_metadata_start_shared_access(&req->cache->metadata.lock,
			req->lock_idx);
	for_each_req_hash_asc(req, hash) {
		ocf_hb_id_naked_lock(&req->cache->metadata.lock, hash,
				OCF_METADATA_WR);
	}
}

void ocf_hb_req_prot_lock_upgrade(struct ocf_request *req)
{
	ocf_cache_line_t hash;

	for_each_req_hash_asc(req, hash) {
		ocf_hb_id_naked_unlock(&req->cache->metadata.lock, hash,
				OCF_METADATA_RD);
	}
	for_each_req_hash_asc(req, hash) {
		ocf_hb_id_naked_lock(&req->cache->metadata.lock, hash,
				OCF_METADATA_WR);
	}
}

void ocf_hb_req_prot_unlock_wr(struct ocf_request *req)
{
	ocf_cache_line_t hash;

	for_each_req_hash_asc(req, hash) {
		ocf_hb_id_naked_unlock(&req->cache->metadata.lock, hash,
				OCF_METADATA_WR);
	}
	ocf_metadata_end_shared_access(&req->cache->metadata.lock,
			req->lock_idx);
}

void ocf_collision_start_shared_access(struct ocf_metadata_lock *metadata_lock,
		uint32_t page)
{
	env_rwsem_down_read(&metadata_lock->collision_pages[page]);
}

void ocf_collision_end_shared_access(struct ocf_metadata_lock *metadata_lock,
		uint32_t page)
{
	env_rwsem_up_read(&metadata_lock->collision_pages[page]);
}

void ocf_collision_start_exclusive_access(struct ocf_metadata_lock *metadata_lock,
		uint32_t page)
{
	env_rwsem_down_write(&metadata_lock->collision_pages[page]);
}

void ocf_collision_end_exclusive_access(struct ocf_metadata_lock *metadata_lock,
		uint32_t page)
{
	env_rwsem_up_write(&metadata_lock->collision_pages[page]);
}
