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

int main(int argc, char **argv)
{
	const char *str;
	int i;

	if (argc == 1) {
		for (i = 1000; i < 2150; i++) {
			str = d_errstr(-i);
			if (strcmp("DER_UNKNOWN", str))
				printf("%d = %s\n", -i, d_errstr(-i));
		}

		return 0;
	}
	

	i = atoi(argv[1]);
	printf("%d = %s\n", i, d_errstr(i));

	return 0;
}
