/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DLCK_UT_H__
#define __DLCK_UT_H__

#include <stdio.h>

/**
 * \brief Apply an optional command-line CMocka test filter.
 *
 * When an argument is provided it is wrapped in '*' wildcards and installed via
 * cmocka_set_test_filter(). Without an argument the full suite runs.
 *
 * \note The caller has to include cmocka.h (with its prerequisites) before this header is used.
 *
 * \param[in]	argc	Standard argument count.
 * \param[in]	argv	Standard argument list.
 *
 * \retval 0	Success.
 * \retval 1	The filter cannot be applied.
 */
static inline int
dlck_ut_filter(int argc, char **argv)
{
#if CMOCKA_FILTER_SUPPORTED
	if (argc > 1) {
		char filter[256];
		int  rc = snprintf(filter, sizeof(filter), "*%s*", argv[1]);

		if (rc > 0 && (size_t)rc < sizeof(filter)) {
			cmocka_set_test_filter(filter);
		} else {
			fprintf(stderr, "ERROR: Test filter '%s' cannot be applied.\n", argv[1]);
			return 1;
		}
	}
#else
	(void)argc;
	(void)argv;
#endif
	return 0;
}

#endif /* __DLCK_UT_H__ */
