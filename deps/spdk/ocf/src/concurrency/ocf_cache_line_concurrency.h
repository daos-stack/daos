/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef OCF_CACHE_CONCURRENCY_H_
#define OCF_CACHE_CONCURRENCY_H_

#include "../utils/utils_alock.h"

/**
 * @file ocf_cache_line_concurrency.h
 * @brief OCF cache concurrency module
 */

/**
 * @brief Initialize OCF cache concurrency module
 *
 * @param self - cacheline concurrency private data
 * @param num_clines - cachelines count
 * @param cache - OCF cache instance

 * @return 0 - Initialization successful, otherwise ERROR
 */
int ocf_cache_line_concurrency_init(struct ocf_alock **self,
		unsigned num_clines, struct ocf_cache *cache);

/**
 * @biref De-Initialize  OCF cache concurrency module
 *
 * @param self - cacheline concurrency private data
 */
void ocf_cache_line_concurrency_deinit(
		struct ocf_alock **self);

/**
 * @brief Get number of waiting (suspended) OCF requests in due to cache
 * overlapping
 *
 * @param c - cacheline concurrency private data
 *
 * @return Number of suspended OCF requests
 */
uint32_t ocf_cache_line_concurrency_suspended_no(struct ocf_alock *c);

/**
 * @brief Return memory footprint conusmed by cache concurrency module
 *
 * @param cache - OCF cache instance
 *
 * @return Memory footprint of cache concurrency module
 */
size_t ocf_cache_line_concurrency_size_of(ocf_cache_t cache);

/**
 * @brief Lock OCF request for write access (Lock all cache lines in map info)
 *
 * @param c - cacheline concurrency private data
 * @param req - OCF request
 * @param cmpl - async lock acquisition callback
 *
 * @returns lock acquisition status or negative error code in case of internal
 *		error
 * @retval OCF_LOCK_ACQUIRED - OCF request has been locked and can be processed
 * @retval OCF_LOCK_NOT_ACQUIRED - OCF request lock not acquired, request was
 * added into waiting list. When lock will be acquired @cmpl cllback be called
 */
int ocf_req_async_lock_wr(struct ocf_alock *c,
		struct ocf_request *req, ocf_req_async_lock_cb cmpl);

/**
 * @brief Lock OCF request for read access (Lock all cache lines in map info)
 *
 * @param c - cacheline concurrency private data
 * @param req - OCF request
 * @param cmpl - async lock acquisition callback
 *
 * @returns lock acquisition status or negative error code in case of internal
 *		error
 * @retval OCF_LOCK_ACQUIRED - OCF request has been locked and can be processed
 * @retval OCF_LOCK_NOT_ACQUIRED - OCF request lock not acquired, request was
 * added into waiting list. When lock will be acquired @cmpl callback be called
 */
int ocf_req_async_lock_rd(struct ocf_alock *c,
		struct ocf_request *req, ocf_req_async_lock_cb cmpl);

/**
 * @brief Unlock OCF request from write access
 *
 * @param c - cacheline concurrency private data
 * @param req - OCF request
 */
void ocf_req_unlock_wr(struct ocf_alock *c,
		struct ocf_request *req);

/**
 * @brief Unlock OCF request from read access
 *
 * @param c - cacheline concurrency private data
 * @param req - OCF request
 */
void ocf_req_unlock_rd(struct ocf_alock *c,
		struct ocf_request *req);

/**
 * @brief Unlock OCF request from read or write access
 *
 * @param c - cacheline concurrency private data
 * @param req - OCF request
 */
void ocf_req_unlock(struct ocf_alock *c,
		struct ocf_request *req);

/**
 * @Check if cache line is used.
 *
 * Cache line is used when:
 * 1. It is locked for write or read access
 * or
 * 2. There is set locked bit in metadata
 *
 * @param cache - OCF cache instance
 * @param line - Cache line to be unlocked
 *
 * @retval true - cache line is used
 * @retval false - cache line is not used
 */
bool ocf_cache_line_is_used(struct ocf_alock *c,
		ocf_cache_line_t line);

/**
 * @brief Check if for specified cache line there are waiters
 * on the waiting list
 *
 * @param c - cacheline concurrency private data
 * @param line - Cache line to be checked for waiters
 *
 * @retval true - there are waiters
 * @retval false - No waiters
 */
bool ocf_cache_line_are_waiters(struct ocf_alock *c,
		ocf_cache_line_t line);

/**
 * @brief un_lock request map info entry from from write or read access.
 *
 * @param c - cacheline concurrency private data
 * @param req - OCF request
 * @param entry - request map entry number
 */
void ocf_req_unlock_entry(struct ocf_alock *c,
		struct ocf_request *req, uint32_t entry);

/**
 * @brief Release cache line read lock
 *
 * @param cache - OCF cache instance
 * @param line - Cache line to be unlocked
 */
void ocf_cache_line_unlock_rd(struct ocf_alock *c,
		ocf_cache_line_t line);

/**
 * @brief Attempt to lock cache line for read
 *
 * @param c - cacheline concurrency private data
 * @param line - Cache line to be checked for waiters
 *
 * @retval true - read lock successfully acquired
 * @retval false - failed to acquire read lock
 */
bool ocf_cache_line_try_lock_rd(struct ocf_alock *c,
		ocf_cache_line_t line);

/**
 * @brief Release cache line write lock
 *
 * @param c - cacheline concurrency private data
 * @param line - Cache line to be unlocked
 */
void ocf_cache_line_unlock_wr(struct ocf_alock *c,
		ocf_cache_line_t line);

/**
 * @brief Attempt to lock cache line for write
 *
 * @param c - cacheline concurrency private data
 * @param line - Cache line to be checked for waiters
 *
 * @retval true - write lock successfully acquired
 * @retval false - failed to acquire write lock
 */
bool ocf_cache_line_try_lock_wr(struct ocf_alock *c,
		ocf_cache_line_t line);

/**
 * @brief Get cacheline concurrency context
 *
 * @param cache - cache instance
 * @return cacheline concurrency context
 */
static inline struct ocf_alock *
ocf_cache_line_concurrency(ocf_cache_t cache)
{
	return cache->device->concurrency.cache_line;
}

#endif /* OCF_CONCURRENCY_H_ */
