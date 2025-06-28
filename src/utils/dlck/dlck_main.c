/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdlib.h>
#include <stdio.h>

#include <gurt/common.h>

#include "dlck_cmds.h"
#include "dlck_args.h"

static const dlck_cmd_func dlck_cmds[] = DLCK_CMDS_FUNCS;

int
main(int argc, char *argv[])
{
	struct dlck_args args = {0};
	int              rc;

	dlck_args_parse(argc, argv, &args);

	D_ASSERT(args.common.cmd >= 0);
	D_ASSERT(args.common.cmd < ARRAY_SIZE(dlck_cmds));

	args.out.printf = printf;

	rc = dlck_cmds[args.common.cmd](&args);

	return rc;
}
