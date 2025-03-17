/**
 * (C) Copyright 2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_SHM_UTILS_H__
#define __DAOS_SHM_UTILS_H__

/* memory block alignment in shared memory */
#define SHM_MEM_ALIGN 4

typedef pthread_mutex_t d_shm_mutex_t;

/**
 * wrapper of pthread_mutex_init() for a mutex with attribute PTHREAD_MUTEX_ROBUST
 *
 * \param[in] mutex		pointer to metex
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_mutex_init(d_shm_mutex_t *mutex);

/**
 * wrapper of pthread_mutex_lock() for a mutex created with attribute PTHREAD_MUTEX_ROBUST
 *
 * \param[in] mutex		pointer to metex
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_mutex_lock(d_shm_mutex_t *mutex, bool *pre_owner_dead);

/**
 * wrapper of pthread_mutex_unlock().
 *
 * \param[in] mutex		pointer to metex
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_mutex_unlock(d_shm_mutex_t *mutex);

/**
 * wrapper of pthread_mutex_destroy().
 *
 * \param[in] mutex		pointer to metex
 *
 * \return			zero for success. error code otherwise.
 */
int
shm_mutex_destroy(d_shm_mutex_t *mutex);

#endif
