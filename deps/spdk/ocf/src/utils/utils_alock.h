/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef OCF_UTILS_ALOCK_H_
#define OCF_UTILS_ALOCK_H_

/**
 * @brief Lock result - Lock acquired successfully
 */
#define OCF_LOCK_ACQUIRED		0

/**
 * @brief Lock result - Lock not acquired, lock request added into waiting list
 */
#define OCF_LOCK_NOT_ACQUIRED		1

struct ocf_alock;

/* async request cacheline lock acquisition callback */
typedef void (*ocf_req_async_lock_cb)(struct ocf_request *req);

typedef int (*ocf_cl_lock_fast)(struct ocf_alock *alock,
		struct ocf_request *req, int rw);

typedef int (*ocf_cl_lock_slow)(struct ocf_alock *alock,
		struct ocf_request *req, int rw, ocf_req_async_lock_cb cmpl);

struct ocf_alock_lock_cbs
{
	ocf_cl_lock_fast lock_entries_fast;
	ocf_cl_lock_slow lock_entries_slow;
};

bool ocf_alock_trylock_one_rd(struct ocf_alock *alock,
		ocf_cache_line_t entry);

void ocf_alock_unlock_one_rd(struct ocf_alock *alock,
		const ocf_cache_line_t entry);

bool ocf_alock_trylock_entry_wr(struct ocf_alock *alock,
		ocf_cache_line_t entry);

void ocf_alock_unlock_one_wr(struct ocf_alock *alock,
		const ocf_cache_line_t entry_idx);

int ocf_alock_lock_rd(struct ocf_alock *alock,
		struct ocf_request *req, ocf_req_async_lock_cb cmpl);

int ocf_alock_lock_wr(struct ocf_alock *alock,
		struct ocf_request *req, ocf_req_async_lock_cb cmpl);

bool ocf_alock_waitlist_is_empty(struct ocf_alock *alock,
		ocf_cache_line_t entry);

uint32_t ocf_alock_waitlist_count(struct ocf_alock *alock);

size_t ocf_alock_obj_size(void);

int ocf_alock_init_inplace(struct ocf_alock *self, unsigned num_entries,
		const char* name, struct ocf_alock_lock_cbs *cbs, ocf_cache_t cache);

int ocf_alock_init(struct ocf_alock **self, unsigned num_entries,
		const char* name, struct ocf_alock_lock_cbs *cbs, ocf_cache_t cache);

void ocf_alock_deinit(struct ocf_alock **self);

size_t ocf_alock_size(unsigned num_entries);

bool ocf_alock_is_index_locked(struct ocf_alock *alock,
		struct ocf_request *req, unsigned index);

void ocf_alock_mark_index_locked(struct ocf_alock *alock,
		struct ocf_request *req, unsigned index, bool locked);

bool ocf_alock_lock_one_wr(struct ocf_alock *alock,
		const ocf_cache_line_t entry, ocf_req_async_lock_cb cmpl,
		void *req, uint32_t idx);

bool ocf_alock_lock_one_rd(struct ocf_alock *alock,
		const ocf_cache_line_t entry, ocf_req_async_lock_cb cmpl,
		void *req, uint32_t idx);

void ocf_alock_waitlist_remove_entry(struct ocf_alock *alock,
	struct ocf_request *req, ocf_cache_line_t entry, int i, int rw);

bool ocf_alock_trylock_entry_rd_idle(struct ocf_alock *alock,
		ocf_cache_line_t entry);

#endif
