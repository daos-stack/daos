/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __UT_DLCK_ABT_MOCK_H__
#define __UT_DLCK_ABT_MOCK_H__

/*
 * Shared dlck_abt.c replacements. Link ut_dlck_abt_mock.c instead of ../dlck_abt.c in the test
 * binary.
 */

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

#endif /* __UT_DLCK_ABT_MOCK_H__ */
