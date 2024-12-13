/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_SHM_UTILS_H__
#define __DAOS_SHM_UTILS_H__

/* memory block alignment in shared memory */
#define SHM_MEM_ALIGN 4

/**
 * wrapper of pthread_mutex_lock() for a mutex created with attribute PTHREAD_MUTEX_ROBUST
 *
 * \param[in] mutex		pointer to metex
 *
 * \return			zero for success. error code otherwise.
*/

int
shm_mutex_lock(pthread_mutex_t *mutex);

/**
 * wrapper of pthread_mutex_unlock(). Used just for clarity.
 *
 * \param[in] mutex		pointer to metex
 *
 * \return			zero for success. error code otherwise.
*/
int
shm_mutex_unlock(pthread_mutex_t *mutex);

#endif
