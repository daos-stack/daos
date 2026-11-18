/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DLCK_UT_H__
#define __DLCK_UT_H__

#include <stdio.h>
#include <stdlib.h>

static inline void
dlck_ut_filter(int argc, char **argv)
{
#if CMOCKA_FILTER_SUPPORTED
	char filter[256];
	int  rc;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <test_filter>\n", argv[0]);
		exit(1);
	}

	rc = snprintf(filter, sizeof(filter), "*%s*", argv[1]);
	if (rc > 0 && (size_t)rc < sizeof(filter)) {
		cmocka_set_test_filter(filter);
	} else {
		fprintf(stderr, "ERROR: Test filter '%s' cannot be applied.\n", argv[1]);
		exit(1);
	}
#else
	(void)argc;
	fprintf(stderr, "usage: %s\n", argv[0]);
	exit(1);
#endif
}

#endif /* __DLCK_UT_H__ */
