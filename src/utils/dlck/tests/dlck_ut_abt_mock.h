/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DLCK_UT_ABT_MOCK_H__
#define __DLCK_UT_ABT_MOCK_H__

#include <abt.h>

#include "../dlck_engine.h"

/*
 * Shared Argobots mocks and the dlck_abt.c replacements. Link dlck_ut_abt_mock.c and enable the
 * wraps (-Wl,--wrap=ABT_*) for the ABT calls controlled by the test binary.
 */

/** Sentinel handles installed and checked by the ABT mocks. */
extern const ABT_mutex  Mock_mutex_handle;
extern const ABT_thread Mock_thread;

#define EXPECT_ABT_THREAD_JOIN(THREAD, RC)                                                         \
	do {                                                                                       \
		expect_value(__wrap_ABT_thread_join, thread, THREAD);                              \
		will_return(__wrap_ABT_thread_join, RC);                                           \
	} while (0)

#define EXPECT_ABT_THREAD_FREE(HANDLE, RC)                                                         \
	do {                                                                                       \
		expect_value(__wrap_ABT_thread_free, handle, HANDLE);                              \
		will_return(__wrap_ABT_thread_free, RC);                                           \
	} while (0)

#define EXPECT_ABT_POOL_IS_EMPTY(POOL, IS_EMPTY)                                                   \
	do {                                                                                       \
		expect_value(__wrap_ABT_pool_is_empty, pool, POOL);                                \
		will_return(__wrap_ABT_pool_is_empty, IS_EMPTY);                                   \
	} while (0)

#define EXPECT_ABT_EVENTUAL_CREATE(RC, EVENTUAL)                                                   \
	do {                                                                                       \
		will_return(__wrap_ABT_eventual_create, RC);                                       \
		if ((RC) == ABT_SUCCESS) {                                                         \
			will_return(__wrap_ABT_eventual_create, EVENTUAL);                         \
		}                                                                                  \
	} while (0)

#define EXPECT_ABT_EVENTUAL_FREE(HANDLE, RC)                                                       \
	do {                                                                                       \
		expect_value(__wrap_ABT_eventual_free, handle, HANDLE);                            \
		will_return(__wrap_ABT_eventual_free, RC);                                         \
	} while (0)

#define EXPECT_ABT_EVENTUAL_SET(EVENTUAL, RC)                                                      \
	do {                                                                                       \
		expect_value(__wrap_ABT_eventual_set, eventual, EVENTUAL);                         \
		will_return(__wrap_ABT_eventual_set, RC);                                          \
	} while (0)

#define EXPECT_ABT_EVENTUAL_TEST(EVENTUAL, RC, IS_READY)                                           \
	do {                                                                                       \
		expect_value(__wrap_ABT_eventual_test, eventual, EVENTUAL);                        \
		will_return(__wrap_ABT_eventual_test, RC);                                         \
		if ((RC) == ABT_SUCCESS) {                                                         \
			will_return(__wrap_ABT_eventual_test, IS_READY);                           \
		}                                                                                  \
	} while (0)

/** dlck_abt.c replacements */

#define EXPECT_DLCK_XSTREAM_CREATE(XS, RC, POOL)                                                   \
	do {                                                                                       \
		expect_value(dlck_xstream_create, xs, XS);                                         \
		will_return(dlck_xstream_create, RC);                                              \
		if ((RC) == DER_SUCCESS) {                                                         \
			will_return(dlck_xstream_create, POOL);                                    \
		}                                                                                  \
	} while (0)

#define EXPECT_DLCK_XSTREAM_FREE(XS, RC)                                                           \
	do {                                                                                       \
		expect_value(dlck_xstream_free, xs, XS);                                           \
		will_return(dlck_xstream_free, RC);                                                \
	} while (0)

/**
 * Queue a dlck_ult_create() call. On DER_SUCCESS the mock stores THREAD in the ULT handle
 * and runs the requested function synchronously, so all the expectations the ULT body consumes
 * have to be queued as well.
 */
#define EXPECT_DLCK_ULT_CREATE(POOL, RC, THREAD)                                                   \
	do {                                                                                       \
		expect_value(dlck_ult_create, pool, POOL);                                         \
		will_return(dlck_ult_create, RC);                                                  \
		if ((RC) == DER_SUCCESS) {                                                         \
			will_return(dlck_ult_create, THREAD);                                      \
		}                                                                                  \
	} while (0)

#endif /* __DLCK_UT_ABT_MOCK_H__ */
