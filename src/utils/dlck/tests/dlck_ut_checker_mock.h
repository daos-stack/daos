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

int
mock_ck_vprintf(struct checker *ck, const char *fmt, va_list args);

#define EXPECT_MOCK_CK_VPRINTF(CK, FMT, CHECK_OUTPUT, OUTPUT, RC)                                  \
	do {                                                                                       \
		expect_value(mock_ck_vprintf, ck, CK);                                             \
		expect_string(mock_ck_vprintf, fmt, FMT);                                          \
		if (CHECK_OUTPUT) {                                                                \
			expect_string(mock_ck_vprintf, output, OUTPUT);                            \
		}                                                                                  \
		will_return(mock_ck_vprintf, CHECK_OUTPUT);                                        \
		will_return(mock_ck_vprintf, RC);                                                  \
	} while (0)

#endif /* __DLCK_UT_CHECKER_MOCK_H__ */
