/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DLCK_UT_MEM_MOCK_H__
#define __DLCK_UT_MEM_MOCK_H__

#define EXPECT_D_CALLOC(NMEMB, SIZE, PTR)                                                          \
	do {                                                                                       \
		expect_value(__wrap_d_calloc, nmemb, NMEMB);                                       \
		expect_value(__wrap_d_calloc, size, SIZE);                                         \
		will_return(__wrap_d_calloc, PTR);                                                 \
	} while (0)

#endif /* __DLCK_UT_MEM_MOCK_H__ */
