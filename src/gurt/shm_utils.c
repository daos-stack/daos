/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <errno.h>
#include <pthread.h>

#include <gurt/common.h>
#include <gurt/shm_utils.h>

/* the attribute set for mutex located inside shared memory */
extern pthread_mutexattr_t  d_shm_mutex_attr;

int
shm_mutex_init(d_shm_mutex_t *mutex)
{
	return pthread_mutex_init((pthread_mutex_t *)mutex, &d_shm_mutex_attr);
}

int
shm_mutex_lock(d_shm_mutex_t *mutex, bool *pre_owner_dead)
{
	int rc;

	if (pre_owner_dead)
		*pre_owner_dead = false;

	rc = pthread_mutex_lock((pthread_mutex_t *)mutex);

	if (rc == 0)
		return rc;

	if (rc != EOWNERDEAD)
		return rc;

	/* error EOWNERDEAD. */
	if (pre_owner_dead)
		*pre_owner_dead = true;
	rc = pthread_mutex_consistent((pthread_mutex_t *)mutex);
	if (rc) {
		DS_ERROR(rc, "pthread_mutex_consistent() failed");
		return rc;
	}
	rc = pthread_mutex_unlock((pthread_mutex_t *)mutex);
	if (rc) {
		DS_ERROR(rc, "pthread_mutex_unlock() failed after pthread_mutex_consistent()");
		return rc;
	}
	/* now try lock again */
	return pthread_mutex_lock((pthread_mutex_t *)mutex);
}

int
shm_mutex_unlock(d_shm_mutex_t *mutex)
{
	return pthread_mutex_unlock((pthread_mutex_t *)mutex);
}

int
shm_mutex_destroy(d_shm_mutex_t *mutex)
{
	return pthread_mutex_destroy((pthread_mutex_t *)mutex);
}
