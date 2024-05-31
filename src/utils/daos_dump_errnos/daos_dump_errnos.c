/*
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Small utility to dump all descriptions of errcodes
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <cart/types.h>
#include <cart/api.h>

int
main(int argc, char **argv)
{
	const char *str;
	int         i;

	if (argc == 1) {
		for (i = DER_SUCCESS; i < DER_LAST_VALID; i++) {
			str = d_errstr(-i);
			if (strcmp("DER_UNKNOWN", str))
				printf("%d = %s\n", -i, d_errstr(-i));
		}

		return 0;
	}

	i = atoi(argv[1]);

	if (i > 0) {
		printf("Errnos are negative numbers, changing\n");
		i = -i;
	}

	printf("%d = %s\n", i, d_errstr(i));

	return 0;
}
