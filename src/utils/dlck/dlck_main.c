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
	struct dlck_control ctrl = {0};
	int                 rc;

	dlck_args_parse(argc, argv, &ctrl);

	D_ASSERT(ctrl.common.cmd < ARRAY_SIZE(dlck_cmds));
	D_ASSERT(ctrl.common.cmd >= 0);

	ctrl.print.dp_printf = printf;

	rc = dlck_cmds[ctrl.common.cmd](&ctrl);

	dlck_args_free(&ctrl);

	return rc;
}
