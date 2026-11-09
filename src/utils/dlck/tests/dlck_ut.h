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
 * cmocka_set_test_filter(). Without an argument the full suite runs. On error the macro returns 1
 * from the calling function.
 *
 * \note The caller has to include cmocka.h (with its prerequisites) before this header is used.
 *
 * \param[in]	argc	Standard argument count.
 * \param[in]	argv	Standard argument list.
 */
#if CMOCKA_FILTER_SUPPORTED
#define DLCK_UT_FILTER(argc, argv)                                                                 \
	do {                                                                                       \
		if ((argc) > 1) {                                                                  \
			char _filter[256];                                                         \
			int  _rc = snprintf(_filter, sizeof(_filter), "*%s*", (argv)[1]);          \
                                                                                                   \
			if (_rc > 0 && (size_t)_rc < sizeof(_filter)) {                            \
				cmocka_set_test_filter(_filter);                                   \
			} else {                                                                   \
				fprintf(stderr, "ERROR: Test filter '%s' cannot be applied.\n",    \
					(argv)[1]);                                                \
				return 1;                                                          \
			}                                                                          \
		}                                                                                  \
	} while (0)
#else
#define DLCK_UT_FILTER(argc, argv)                                                                 \
	do {                                                                                       \
		(void)(argc);                                                                      \
		(void)(argv);                                                                      \
	} while (0)
#endif

#endif /* __DLCK_UT_H__ */
