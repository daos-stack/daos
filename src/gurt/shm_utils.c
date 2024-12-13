/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <errno.h>
#include <pthread.h>

#include <gurt/common.h>
#include <gurt/shm_utils.h>

int
shm_mutex_lock(pthread_mutex_t *mutex)
{
	int rc;

	rc = pthread_mutex_lock(mutex);

	if (rc == 0)
		return rc;

	if (rc != EOWNERDEAD)
		return rc;

	/* error EOWNERDEAD. */
	rc = pthread_mutex_consistent(mutex);
	if (rc) {
		DS_ERROR(rc, "pthread_mutex_consistent() failed");
		return rc;
	}
	rc = pthread_mutex_unlock(mutex);
	if (rc) {
		DS_ERROR(rc, "pthread_mutex_unlock() failed after pthread_mutex_consistent()");
		return rc;
	}
	/* now try lock again */
	return pthread_mutex_lock(mutex);
}

int
shm_mutex_unlock(pthread_mutex_t *mutex)
{
	return pthread_mutex_unlock(mutex);
}
