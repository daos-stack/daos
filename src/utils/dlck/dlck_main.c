/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(dlck)

#include <stdlib.h>
#include <stdio.h>

#include <daos_errno.h>
#include <daos/debug.h>
#include <gurt/common.h>

#include "dlck_cmds.h"
#include "dlck_args.h"

static const dlck_cmd_func dlck_cmds[] = DLCK_CMDS_FUNCS;

/**
 * Printf function that flushes output immediately in case DLCK crashes unexpectedly.
 * Intended to ensure no useful diagnostic information is lost due to unflushed buffers.
 */
static int
dlck_printf(const char *fmt, ...)
{
	va_list args;
	int     rc;

	va_start(args, fmt);
	rc = vprintf(fmt, args);
	va_end(args);
	if (rc < 0) {
		D_ERROR("vprintf() failed: %s (%d)\n", strerror(errno), errno);
		return rc;
	}

	rc = fflush(stdout);
	if (rc == EOF) {
		D_ERROR("fflush() failed: %s (%d)\n", strerror(errno), errno);
	}

	return rc;
}

int
main(int argc, char *argv[])
{
	struct dlck_control ctrl = {0};
	int                 rc;

	dlck_args_parse(argc, argv, &ctrl);

	D_ASSERT(ctrl.common.cmd < ARRAY_SIZE(dlck_cmds));
	D_ASSERT(ctrl.common.cmd >= 0);

	ctrl.print.dp_printf = dlck_printf;

	rc = dlck_cmds[ctrl.common.cmd](&ctrl);

	dlck_args_free(&ctrl);

	return rc;
}
