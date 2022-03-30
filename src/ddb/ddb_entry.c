/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdio.h>
#include <daos_types.h>
#include <stdarg.h>
#include "ddb_main.h"

static char *
get_input(char *buf, uint32_t buf_len)
{
	return fgets(buf, buf_len, stdin);
}

static int
print_error(const char *fmt, ...)
{
	va_list args;
	int	rc;

	va_start(args, fmt);
	rc = vfprintf(stderr, fmt, args);
	va_end(args);

	return rc;
}

int main(int argc, char *argv[])
{
	int rc;
	struct ddb_io_ft ft = {
		.ddb_print_message = printf,
		.ddb_print_error = print_error,
		.ddb_get_input = get_input,
	};

	rc = ddb_init();
	if (rc != 0) {
		fprintf(stderr, "Error with ddb_init: "DF_RC"\n", DP_RC(rc));
		return -rc;
	}
	rc = ddb_main(&ft, argc, argv);
	if (rc != 0)
		fprintf(stderr, "Error: "DF_RC"\n", DP_RC(rc));

	ddb_fini();

	return -rc;
}
