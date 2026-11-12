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
extern const ABT_mutex    Mock_mutex_handle;
extern const ABT_pool     Mock_pool;
extern const ABT_thread   Mock_thread;
extern const ABT_eventual Mock_eventual;

#define EXPECT_ABT_THREAD_JOIN(THREAD, ABT_RC)                                                     \
	do {                                                                                       \
		expect_value(__wrap_ABT_thread_join, thread, THREAD);                              \
		will_return(__wrap_ABT_thread_join, ABT_RC);                                       \
	} while (0)

#define EXPECT_ABT_THREAD_FREE(HANDLE, ABT_RC)                                                     \
	do {                                                                                       \
		expect_value(__wrap_ABT_thread_free, handle, HANDLE);                              \
		will_return(__wrap_ABT_thread_free, ABT_RC);                                       \
	} while (0)

#define EXPECT_ABT_POOL_IS_EMPTY(POOL, IS_EMPTY)                                                   \
	do {                                                                                       \
		expect_value(__wrap_ABT_pool_is_empty, pool, POOL);                                \
		will_return(__wrap_ABT_pool_is_empty, IS_EMPTY);                                   \
	} while (0)

#define EXPECT_ABT_EVENTUAL_CREATE(ABT_RC, EVENTUAL)                                               \
	do {                                                                                       \
		will_return(__wrap_ABT_eventual_create, ABT_RC);                                   \
		if ((ABT_RC) == ABT_SUCCESS) {                                                     \
			will_return(__wrap_ABT_eventual_create, EVENTUAL);                         \
		}                                                                                  \
	} while (0)

#define EXPECT_ABT_EVENTUAL_FREE(HANDLE, ABT_RC)                                                   \
	do {                                                                                       \
		expect_value(__wrap_ABT_eventual_free, handle, HANDLE);                            \
		will_return(__wrap_ABT_eventual_free, ABT_RC);                                     \
	} while (0)

#define EXPECT_ABT_EVENTUAL_SET(EVENTUAL, ABT_RC)                                                  \
	do {                                                                                       \
		expect_value(__wrap_ABT_eventual_set, eventual, EVENTUAL);                         \
		will_return(__wrap_ABT_eventual_set, ABT_RC);                                      \
	} while (0)

#define EXPECT_ABT_EVENTUAL_TEST(EVENTUAL, ABT_RC, IS_READY)                                       \
	do {                                                                                       \
		expect_value(__wrap_ABT_eventual_test, eventual, EVENTUAL);                        \
		will_return(__wrap_ABT_eventual_test, ABT_RC);                                     \
		if ((ABT_RC) == ABT_SUCCESS) {                                                     \
			will_return(__wrap_ABT_eventual_test, IS_READY);                           \
		}                                                                                  \
	} while (0)

/** dlck_abt.c replacements */

#define EXPECT_XS_CREATE(XS, RC, POOL)                                                             \
	do {                                                                                       \
		expect_value(dlck_xstream_create, xs, XS);                                         \
		will_return(dlck_xstream_create, RC);                                              \
		if ((RC) == DER_SUCCESS) {                                                         \
			will_return(dlck_xstream_create, POOL);                                    \
		}                                                                                  \
	} while (0)

#define EXPECT_XS_FREE(XS, RC)                                                                     \
	do {                                                                                       \
		expect_value(dlck_xstream_free, xs, XS);                                           \
		will_return(dlck_xstream_free, RC);                                                \
	} while (0)

/**
 * Queue a dlck_ult_create() call. On DER_SUCCESS the mock stores Mock_thread in the ULT handle
 * and runs the requested function synchronously, so all the expectations the ULT body consumes
 * have to be queued as well.
 */
#define EXPECT_ULT_CREATE(POOL, RC)                                                                \
	do {                                                                                       \
		expect_value(dlck_ult_create, pool, POOL);                                         \
		will_return(dlck_ult_create, RC);                                                  \
	} while (0)

#endif /* __DLCK_UT_ABT_MOCK_H__ */
