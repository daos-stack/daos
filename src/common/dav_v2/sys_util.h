/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2016-2023, Intel Corporation */

/*
 * sys_util.h -- internal utility wrappers around system functions
 */

#ifndef __DAOS_COMMON_SYS_UTIL_H
#define __DAOS_COMMON_SYS_UTIL_H 1

#include <errno.h>

#include <gurt/common.h>
#include "out.h"

/*
 * util_mutex_init -- os_mutex_init variant that never fails from
 * caller perspective. If os_mutex_init failed, this function aborts
 * the program.
 */
static inline void
util_mutex_init(pthread_mutex_t *m)
{
	int tmp = D_MUTEX_INIT(m, NULL);

	D_ASSERTF(tmp == 0, "!os_mutex_init");
}

/*
 * util_mutex_destroy -- os_mutex_destroy variant that never fails from
 * caller perspective. If os_mutex_destroy failed, this function aborts
 * the program.
 */
static inline void
util_mutex_destroy(pthread_mutex_t *m)
{
	int tmp = D_MUTEX_DESTROY(m);

	D_ASSERTF(tmp == 0, "!os_mutex_destroy");
}

/*
 * util_mutex_lock -- os_mutex_lock variant that never fails from
 * caller perspective. If os_mutex_lock failed, this function aborts
 * the program.
 */
static inline void
util_mutex_lock(pthread_mutex_t *m)
{
	int tmp = D_MUTEX_LOCK(m);

	D_ASSERTF(tmp == 0, "!os_mutex_destroy");
}

/*
 * util_mutex_trylock -- os_mutex_trylock variant that never fails from
 * caller perspective (other than EBUSY). If util_mutex_trylock failed, this
 * function aborts the program.
 * Returns 0 if locked successfully, otherwise returns EBUSY.
 */
static inline int
util_mutex_trylock(pthread_mutex_t *m)
{
	int tmp = D_MUTEX_TRYLOCK(m);

	D_ASSERTF((!tmp || (tmp == -DER_BUSY)), "!os_mutex_trylock");
	return tmp?EBUSY:0;
}

/*
 * util_mutex_unlock -- os_mutex_unlock variant that never fails from
 * caller perspective. If os_mutex_unlock failed, this function aborts
 * the program.
 */
static inline void
util_mutex_unlock(pthread_mutex_t *m)
{
	int tmp = D_MUTEX_UNLOCK(m);

	D_ASSERTF(tmp == 0, "!os_mutex_unlock");
}

#endif /* __DAOS_COMMON_SYS_UTIL_H */
