/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __UT_STDIO_MOCK_H__
#define __UT_STDIO_MOCK_H__

#include <stdio.h>

#define EXPECT_VFPRINTF(STREAM, FMT, CHECK_OUTPUT, OUTPUT, RC, ERRNO)                              \
	do {                                                                                       \
		expect_value(__wrap_vfprintf, stream, STREAM);                                     \
		expect_string(__wrap_vfprintf, fmt, FMT);                                          \
		if (CHECK_OUTPUT) {                                                                \
			expect_string(__wrap_vfprintf, output, OUTPUT);                            \
		}                                                                                  \
		will_return(__wrap_vfprintf, CHECK_OUTPUT);                                        \
		will_return(__wrap_vfprintf, RC);                                                  \
		if ((RC) < 0) {                                                                    \
			will_return(__wrap_vfprintf, ERRNO);                                       \
		}                                                                                  \
	} while (0)

#define EXPECT_D_ASPRINTF2(FMT, PTR, RC)                                                           \
	do {                                                                                       \
		expect_string(__wrap_d_asprintf2, fmt, FMT);                                       \
		will_return(__wrap_d_asprintf2, PTR);                                              \
		will_return(__wrap_d_asprintf2, RC);                                               \
	} while (0)

#endif /* __UT_STDIO_MOCK_H__ */
