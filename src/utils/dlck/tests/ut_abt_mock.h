/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __UT_ABT_MOCK_H__
#define __UT_ABT_MOCK_H__

#include <abt.h>

/*
 * Shared Argobots mocks. Link ut_abt_mock.c and enable the wraps (-Wl,--wrap=ABT_*) for the ABT
 * calls controlled by the test binary.
 */

/** Sentinel handles installed and checked by the ABT mocks. */
extern const ABT_mutex  Mock_mutex_handle;
extern const ABT_thread Mock_thread;

#define EXPECT_ABT_THREAD_JOIN(THREAD, UT_ABT_RC)                                                  \
	do {                                                                                       \
		expect_value(__wrap_ABT_thread_join, thread, THREAD);                              \
		will_return(__wrap_ABT_thread_join, UT_ABT_RC);                                    \
	} while (0)

#define EXPECT_ABT_THREAD_FREE(HANDLE, UT_ABT_RC)                                                  \
	do {                                                                                       \
		expect_value(__wrap_ABT_thread_free, handle, HANDLE);                              \
		will_return(__wrap_ABT_thread_free, UT_ABT_RC);                                    \
	} while (0)

#define EXPECT_ABT_POOL_IS_EMPTY(POOL, UT_ABT_RC, IS_EMPTY)                                        \
	do {                                                                                       \
		expect_value(__wrap_ABT_pool_is_empty, pool, POOL);                                \
		will_return(__wrap_ABT_pool_is_empty, UT_ABT_RC);                                  \
		if ((UT_ABT_RC) == ABT_SUCCESS) {                                                  \
			will_return(__wrap_ABT_pool_is_empty, IS_EMPTY);                           \
		}                                                                                  \
	} while (0)

#define EXPECT_ABT_EVENTUAL_CREATE(UT_ABT_RC, EVENTUAL)                                            \
	do {                                                                                       \
		will_return(__wrap_ABT_eventual_create, UT_ABT_RC);                                \
		if ((UT_ABT_RC) == ABT_SUCCESS) {                                                  \
			will_return(__wrap_ABT_eventual_create, EVENTUAL);                         \
		}                                                                                  \
	} while (0)

#define EXPECT_ABT_EVENTUAL_FREE(HANDLE, UT_ABT_RC)                                                \
	do {                                                                                       \
		expect_value(__wrap_ABT_eventual_free, handle, HANDLE);                            \
		will_return(__wrap_ABT_eventual_free, UT_ABT_RC);                                  \
	} while (0)

#define EXPECT_ABT_EVENTUAL_SET(EVENTUAL, UT_ABT_RC)                                               \
	do {                                                                                       \
		expect_value(__wrap_ABT_eventual_set, eventual, EVENTUAL);                         \
		will_return(__wrap_ABT_eventual_set, UT_ABT_RC);                                   \
	} while (0)

#define EXPECT_ABT_EVENTUAL_TEST(EVENTUAL, UT_ABT_RC, IS_READY)                                    \
	do {                                                                                       \
		expect_value(__wrap_ABT_eventual_test, eventual, EVENTUAL);                        \
		will_return(__wrap_ABT_eventual_test, UT_ABT_RC);                                  \
		if ((UT_ABT_RC) == ABT_SUCCESS) {                                                  \
			will_return(__wrap_ABT_eventual_test, IS_READY);                           \
		}                                                                                  \
	} while (0)

#endif /* __UT_ABT_MOCK_H__ */
