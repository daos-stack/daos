/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DLCK_UT_MEM_MOCK_H__
#define __DLCK_UT_MEM_MOCK_H__

/*
 * Shared memory-allocation mocks. Link dlck_ut_mem_mock.c and enable the d_calloc/d_free wraps
 * (-Wl,--wrap=d_calloc -Wl,--wrap=d_free) to use them.
 */

/** Queue a d_calloc() call returning PTR; NULL simulates an allocation failure. */
#define EXPECT_D_CALLOC(NMEMB, SIZE, PTR)                                                          \
	do {                                                                                       \
		expect_value(__wrap_d_calloc, nmemb, NMEMB);                                       \
		expect_value(__wrap_d_calloc, size, SIZE);                                         \
		will_return(__wrap_d_calloc, PTR);                                                 \
	} while (0)

#endif /* __DLCK_UT_MEM_MOCK_H__ */
