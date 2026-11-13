/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DLCK_UT_CHECKER_MOCK_H__
#define __DLCK_UT_CHECKER_MOCK_H__

#include <stdarg.h>
#include <stdbool.h>

#include <daos/checker.h>

/*
 * Shared mock of the checker's ck_vprintf callback. Link dlck_ut_checker_mock.c and install
 * mock_ck_vprintf in the checker under test to use it.
 */

/** Checks rendered output during the next mock_ck_vprintf call. */
extern bool mock_ck_vprintf_check_output;

int
mock_ck_vprintf(struct checker *ck, const char *fmt, va_list args);

/** Queue one mock_ck_vprintf() call. */
#define EXPECT_MOCK_CK_VPRINTF(CK, FMT, RC)                                                        \
	do {                                                                                       \
		expect_value(mock_ck_vprintf, ck, CK);                                             \
		expect_string(mock_ck_vprintf, fmt, FMT);                                          \
		will_return(mock_ck_vprintf, RC);                                                  \
	} while (0)

#endif /* __DLCK_UT_CHECKER_MOCK_H__ */
