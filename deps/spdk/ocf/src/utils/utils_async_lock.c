/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "utils_async_lock.h"

struct ocf_async_lock_waiter {
	struct list_head list;
	ocf_async_lock_t lock;
	bool write_lock;
	ocf_async_lock_end_t cmpl;
};

void _ocf_async_lock_collect_waiters(ocf_async_lock_t lock,
		struct list_head *waiters)
{
	ocf_async_lock_waiter_t iter, temp;

	list_for_each_entry_safe(iter, temp, &lock->waiters, list) {
		if (!iter->write_lock) {
			list_move_tail(&iter->list, waiters);
			lock->rd++;
		} else {
			if (!lock->rd) {
				list_move_tail(&iter->list, waiters);
				lock->wr = 1;
			}
			break;
		}
	}
}

void _ocf_async_lock_run_waiters(struct ocf_async_lock *lock,
		struct list_head *waiters, int status)
{
	ocf_async_lock_waiter_t iter, temp;

	/* TODO: Should we run waiters asynchronously? */

	list_for_each_entry_safe(iter, temp, waiters, list) {
		list_del(&iter->list);
		iter->cmpl(iter, status);
		env_vfree(iter);
	}
}

int ocf_async_lock_init(struct ocf_async_lock *lock, uint32_t waiter_priv_size)
{
	int err = 0;

	err = env_spinlock_init(&lock->waiters_lock);
	if (err)
		return err;

	INIT_LIST_HEAD(&lock->waiters);
	lock->rd = 0;
	lock->wr = 0;
	lock->waiter_priv_size = waiter_priv_size;

	return 0;
}

void ocf_async_lock_deinit(struct ocf_async_lock *lock)
{
	struct list_head waiters;
	ocf_async_lock_waiter_t iter, temp;

	INIT_LIST_HEAD(&waiters);

	env_spinlock_lock(&lock->waiters_lock);
	list_for_each_entry_safe(iter, temp, &lock->waiters, list)
		list_move_tail(&iter->list, &waiters);
	env_spinlock_unlock(&lock->waiters_lock);

	env_spinlock_destroy(&lock->waiters_lock);

	_ocf_async_lock_run_waiters(lock, &waiters, -OCF_ERR_NO_LOCK);
}

ocf_async_lock_waiter_t ocf_async_lock_new_waiter(ocf_async_lock_t lock,
		ocf_async_lock_end_t cmpl)
{
	ocf_async_lock_waiter_t waiter;

	waiter = env_vmalloc(sizeof(*waiter) + lock->waiter_priv_size);
	if (!waiter)
		return NULL;

	waiter->lock = lock;
	waiter->cmpl = cmpl;

	return waiter;
}

ocf_async_lock_t ocf_async_lock_waiter_get_lock(ocf_async_lock_waiter_t waiter)
{
	return waiter->lock;
}

void *ocf_async_lock_waiter_get_priv(ocf_async_lock_waiter_t waiter)
{
	return (void *)waiter + sizeof(*waiter);
}

static int _ocf_async_trylock(struct ocf_async_lock *lock)
{
	if (lock->wr || lock->rd)
		return -OCF_ERR_NO_LOCK;

	lock->wr = 1;
	return 0;
}

void ocf_async_lock(ocf_async_lock_waiter_t waiter)
{
	ocf_async_lock_t lock = waiter->lock;
	int result;

	env_spinlock_lock(&lock->waiters_lock);

	result = _ocf_async_trylock(lock);
	if (!result) {
		env_spinlock_unlock(&lock->waiters_lock);
		waiter->cmpl(waiter, 0);
		env_vfree(waiter);
		return;
	}

	waiter->write_lock = true;
	list_add_tail(&waiter->list, &lock->waiters);

	env_spinlock_unlock(&lock->waiters_lock);
}

int ocf_async_trylock(struct ocf_async_lock *lock)
{
	int result;

	env_spinlock_lock(&lock->waiters_lock);
	result = _ocf_async_trylock(lock);
	env_spinlock_unlock(&lock->waiters_lock);

	return result;
}

void ocf_async_unlock(struct ocf_async_lock *lock)
{
	struct list_head waiters;

	INIT_LIST_HEAD(&waiters);

	env_spinlock_lock(&lock->waiters_lock);

	ENV_BUG_ON(lock->rd);
	ENV_BUG_ON(!lock->wr);

	lock->wr = 0;

	_ocf_async_lock_collect_waiters(lock, &waiters);

	env_spinlock_unlock(&lock->waiters_lock);

	_ocf_async_lock_run_waiters(lock, &waiters, 0);
}

static int _ocf_async_read_trylock(struct ocf_async_lock *lock)
{
	if (lock->wr || !list_empty(&lock->waiters))
		return -OCF_ERR_NO_LOCK;

	lock->rd++;
	return 0;
}

void ocf_async_read_lock(ocf_async_lock_waiter_t waiter)
{
	ocf_async_lock_t lock = waiter->lock;
	int result;

	env_spinlock_lock(&lock->waiters_lock);

	result = _ocf_async_read_trylock(lock);
	if (!result) {
		env_spinlock_unlock(&lock->waiters_lock);
		waiter->cmpl(waiter, 0);
		env_vfree(waiter);
		return;
	}

	waiter->write_lock = false;
	list_add_tail(&waiter->list, &lock->waiters);

	env_spinlock_unlock(&lock->waiters_lock);
}

int ocf_async_read_trylock(struct ocf_async_lock *lock)
{
	int result;

	env_spinlock_lock(&lock->waiters_lock);
	result = _ocf_async_read_trylock(lock);
	env_spinlock_unlock(&lock->waiters_lock);

	return result;
}

void ocf_async_read_unlock(struct ocf_async_lock *lock)
{
	struct list_head waiters;

	INIT_LIST_HEAD(&waiters);

	env_spinlock_lock(&lock->waiters_lock);

	ENV_BUG_ON(!lock->rd);
	ENV_BUG_ON(lock->wr);

	if (--lock->rd) {
		env_spinlock_unlock(&lock->waiters_lock);
		return;
	}

	_ocf_async_lock_collect_waiters(lock, &waiters);

	env_spinlock_unlock(&lock->waiters_lock);

	_ocf_async_lock_run_waiters(lock, &waiters, 0);
}

bool ocf_async_is_locked(struct ocf_async_lock *lock)
{
	bool locked;

	env_spinlock_lock(&lock->waiters_lock);
	locked = lock->rd || lock->wr;
	env_spinlock_unlock(&lock->waiters_lock);

	return locked;
}
