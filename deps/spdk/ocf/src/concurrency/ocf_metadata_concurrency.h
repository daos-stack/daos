/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "../ocf_cache_priv.h"
#include "../ocf_space.h"
#include "../ocf_queue_priv.h"

#ifndef __OCF_METADATA_CONCURRENCY_H__
#define __OCF_METADATA_CONCURRENCY_H__

#define OCF_METADATA_RD 0
#define OCF_METADATA_WR 1

static inline unsigned ocf_metadata_concurrency_next_idx(ocf_queue_t q)
{
	return q->lock_idx++ % OCF_NUM_GLOBAL_META_LOCKS;
}

int ocf_metadata_concurrency_init(struct ocf_metadata_lock *metadata_lock);

void ocf_metadata_concurrency_deinit(struct ocf_metadata_lock *metadata_lock);

int ocf_metadata_concurrency_attached_init(
		struct ocf_metadata_lock *metadata_lock, ocf_cache_t cache,
		uint32_t hash_table_entries, uint32_t colision_table_pages);

void ocf_metadata_concurrency_attached_deinit(
		struct ocf_metadata_lock *metadata_lock);

static inline void ocf_metadata_lru_wr_lock(
		struct ocf_metadata_lock *metadata_lock, unsigned ev_list)
{
	env_rwlock_write_lock(&metadata_lock->lru[ev_list]);
}

static inline void ocf_metadata_lru_wr_unlock(
		struct ocf_metadata_lock *metadata_lock, unsigned ev_list)
{
	env_rwlock_write_unlock(&metadata_lock->lru[ev_list]);
}

static inline void ocf_metadata_lru_rd_lock(
		struct ocf_metadata_lock *metadata_lock, unsigned ev_list)
{
	env_rwlock_read_lock(&metadata_lock->lru[ev_list]);
}

static inline void ocf_metadata_lru_rd_unlock(
		struct ocf_metadata_lock *metadata_lock, unsigned ev_list)
{
	env_rwlock_read_unlock(&metadata_lock->lru[ev_list]);
}
static inline void ocf_metadata_lru_wr_lock_all(
		struct ocf_metadata_lock *metadata_lock)
{
	uint32_t i;

	for (i = 0; i < OCF_NUM_LRU_LISTS; i++)
		ocf_metadata_lru_wr_lock(metadata_lock, i);
}

static inline void ocf_metadata_lru_wr_unlock_all(
		struct ocf_metadata_lock *metadata_lock)
{
	uint32_t i;

	for (i = 0; i < OCF_NUM_LRU_LISTS; i++)
		ocf_metadata_lru_wr_unlock(metadata_lock, i);
}

#define OCF_METADATA_LRU_WR_LOCK(cline) \
		ocf_metadata_lru_wr_lock(&cache->metadata.lock, \
				cline % OCF_NUM_LRU_LISTS)

#define OCF_METADATA_LRU_WR_UNLOCK(cline) \
		ocf_metadata_lru_wr_unlock(&cache->metadata.lock, \
				cline % OCF_NUM_LRU_LISTS)

#define OCF_METADATA_LRU_RD_LOCK(cline) \
		ocf_metadata_lru_rd_lock(&cache->metadata.lock, \
				cline % OCF_NUM_LRU_LISTS)

#define OCF_METADATA_LRU_RD_UNLOCK(cline) \
		ocf_metadata_lru_rd_unlock(&cache->metadata.lock, \
				cline % OCF_NUM_LRU_LISTS)


#define OCF_METADATA_LRU_WR_LOCK_ALL() \
	ocf_metadata_lru_wr_lock_all(&cache->metadata.lock)

#define OCF_METADATA_LRU_WR_UNLOCK_ALL() \
	ocf_metadata_lru_wr_unlock_all(&cache->metadata.lock)

static inline void ocf_metadata_partition_lock(
		struct ocf_metadata_lock *metadata_lock,
		ocf_part_id_t part_id)
{
	env_spinlock_lock(&metadata_lock->partition[part_id]);
}

static inline void ocf_metadata_partition_unlock(
		struct ocf_metadata_lock *metadata_lock,
		ocf_part_id_t part_id)
{
	env_spinlock_unlock(&metadata_lock->partition[part_id]);
}

void ocf_metadata_start_exclusive_access(
		struct ocf_metadata_lock *metadata_lock);

int ocf_metadata_try_start_exclusive_access(
		struct ocf_metadata_lock *metadata_lock);

void ocf_metadata_end_exclusive_access(
		struct ocf_metadata_lock *metadata_lock);

int ocf_metadata_try_start_shared_access(
		struct ocf_metadata_lock *metadata_lock,
		unsigned lock_idx);

void ocf_metadata_start_shared_access(
		struct ocf_metadata_lock *metadata_lock,
		unsigned lock_idx);

void ocf_metadata_end_shared_access(
		struct ocf_metadata_lock *metadata_lock,
		unsigned lock_idx);

void ocf_hb_cline_prot_lock_rd(struct ocf_metadata_lock *metadata_lock,
		uint32_t lock_idx, uint32_t core_id, uint64_t core_line);
void ocf_hb_cline_prot_unlock_rd(struct ocf_metadata_lock *metadata_lock,
		uint32_t lock_idx, uint32_t core_id, uint64_t core_line);

void ocf_hb_cline_prot_lock_wr(struct ocf_metadata_lock *metadata_lock,
		uint32_t lock_idx, uint32_t core_id, uint64_t core_line);
void ocf_hb_cline_prot_unlock_wr(struct ocf_metadata_lock *metadata_lock,
		uint32_t lock_idx, uint32_t core_id, uint64_t core_line);

void ocf_hb_id_prot_lock_wr(struct ocf_metadata_lock *metadata_lock,
		unsigned lock_idx, ocf_cache_line_t hash);
void ocf_hb_id_prot_unlock_wr(struct ocf_metadata_lock *metadata_lock,
		unsigned lock_idx, ocf_cache_line_t hash);

/* Caller must hold global metadata read lock when acquiring naked hash bucket
 * lock.
 */
bool ocf_hb_cline_naked_trylock_rd(struct ocf_metadata_lock *metadata_lock,
		uint32_t core_id, uint64_t core_line);
void ocf_hb_cline_naked_unlock_rd(struct ocf_metadata_lock *metadata_lock,
		uint32_t core_id, uint64_t core_line);
bool ocf_hb_cline_naked_trylock_wr(struct ocf_metadata_lock *metadata_lock,
		uint32_t core_id, uint64_t core_line);
void ocf_hb_cline_naked_unlock_wr(struct ocf_metadata_lock *metadata_lock,
		uint32_t core_id, uint64_t core_line);
void ocf_hb_id_naked_lock_wr(struct ocf_metadata_lock *metadata_lock,
		ocf_cache_line_t hash);
void ocf_hb_id_naked_unlock_wr(struct ocf_metadata_lock *metadata_lock,
		ocf_cache_line_t hash);

bool ocf_req_hash_in_range(struct ocf_request *req,
		ocf_core_id_t core_id, uint64_t core_line);

/* lock entire request in deadlock-free manner */
void ocf_hb_req_prot_lock_rd(struct ocf_request *req);
void ocf_hb_req_prot_unlock_rd(struct ocf_request *req);
void ocf_hb_req_prot_lock_wr(struct ocf_request *req);
void ocf_hb_req_prot_unlock_wr(struct ocf_request *req);
void ocf_hb_req_prot_lock_upgrade(struct ocf_request *req);

/* collision table page lock interface */
void ocf_collision_start_shared_access(struct ocf_metadata_lock *metadata_lock,
		uint32_t page);
void ocf_collision_end_shared_access(struct ocf_metadata_lock *metadata_lock,
		uint32_t page);
void ocf_collision_start_exclusive_access(struct ocf_metadata_lock *metadata_lock,
		uint32_t page);
void ocf_collision_end_exclusive_access(struct ocf_metadata_lock *metadata_lock,
		uint32_t page);
#endif
