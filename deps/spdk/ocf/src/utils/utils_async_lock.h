/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __UTILS_ASYNC_LOCK_H__
#define __UTILS_ASYNC_LOCK_H__

#include "ocf_env.h"

struct ocf_async_lock {
	struct list_head waiters;
	env_spinlock waiters_lock;
	uint32_t rd;
	uint32_t wr;
	uint32_t waiter_priv_size;
};

typedef struct ocf_async_lock *ocf_async_lock_t;

typedef struct ocf_async_lock_waiter *ocf_async_lock_waiter_t;

typedef void (*ocf_async_lock_end_t)(ocf_async_lock_waiter_t waiter, int error);

int ocf_async_lock_init(ocf_async_lock_t lock, uint32_t waiter_priv_size);

void ocf_async_lock_deinit(ocf_async_lock_t lock);

ocf_async_lock_waiter_t ocf_async_lock_new_waiter(ocf_async_lock_t lock,
		ocf_async_lock_end_t cmpl);

ocf_async_lock_t ocf_async_lock_waiter_get_lock(ocf_async_lock_waiter_t waiter);

void *ocf_async_lock_waiter_get_priv(ocf_async_lock_waiter_t waiter);

void ocf_async_lock(ocf_async_lock_waiter_t waiter);

int ocf_async_trylock(struct ocf_async_lock *lock);

void ocf_async_unlock(struct ocf_async_lock *lock);

void ocf_async_read_lock(ocf_async_lock_waiter_t waiter);

int ocf_async_read_trylock(struct ocf_async_lock *lock);

void ocf_async_read_unlock(struct ocf_async_lock *lock);

bool ocf_async_is_locked(struct ocf_async_lock *lock);

#endif /* __UTILS_ASYNC_LOCK_H__ */
