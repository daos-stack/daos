/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdio.h>
#include <daos_types.h>
#include "ddb_main.h"

int main(int argc, char *argv[])
{
	struct ddb_ctx ctx;
	int rc;

	ddb_ctx_init(&ctx);

	rc = ddb_init();
	if (rc != 0) {
		fprintf(stderr, "Error with ddb_init: "DF_RC"\n", DP_RC(rc));
		return -rc;
	}
	rc = ddb_main(&ctx.dc_io_ft, argc, argv);
	if (rc != 0)
		fprintf(stderr, "Error: "DF_RC"\n", DP_RC(rc));

	ddb_fini();

	return -rc;
}
