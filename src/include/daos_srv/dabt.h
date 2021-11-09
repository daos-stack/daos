/*
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * dabt: DAOS Argobots Helpers
 */

#ifndef DAOS_SRV_DABT_H
#define DAOS_SRV_DABT_H

#define DABT_MUST(func, ...)									\
	do {											\
		int dabt_rc;									\
												\
		dabt_rc = func(__VA_ARGS__);							\
		D_ASSERTF(dabt_rc == ABT_SUCCESS, #func": %d\n", dabt_rc);			\
	} while (0)

#define DABT_THREAD_JOIN(thread)	DABT_MUST(ABT_thread_join, thread)
#define DABT_THREAD_FREE(thread)	DABT_MUST(ABT_thread_free, thread)
#define DABT_MUTEX_FREE(mutex)		DABT_MUST(ABT_mutex_free, mutex)
#define DABT_COND_WAIT(cond, mutex)	DABT_MUST(ABT_cond_wait, cond, mutex)
#define DABT_COND_SIGNAL(cond)		DABT_MUST(ABT_cond_signal, cond)
#define DABT_COND_BROADCAST(cond)	DABT_MUST(ABT_cond_broadcast, cond)
#define DABT_EVENTUAL_SET(eventual, value, nbytes)						\
					DABT_MUST(ABT_eventual_set, eventual, value, nbytes)
#define DABT_EVENTUAL_WAIT(eventual, value)							\
					DABT_MUST(ABT_eventual_wait, eventual, value)
#define DABT_EVENTUAL_FREE(eventual)	DABT_MUST(ABT_eventual_free, eventual)
#define DABT_FUTURE_SET(future, value)	DABT_MUST(ABT_future_set, future, value)

#endif /* DAOS_SRV_DABT_H */
