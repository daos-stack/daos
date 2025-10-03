/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(dlck)

#include <stdlib.h>
#include <stdio.h>
#include <abt.h>

#include <daos_errno.h>
#include <daos/debug.h>
#include <daos_srv/daos_engine.h>
#include <gurt/common.h>

#include "dlck_args.h"
#include "dlck_cmds.h"
#include "dlck_print.h"

static const dlck_cmd_func dlck_cmds[] = DLCK_CMDS_FUNCS;

int
main(int argc, char *argv[])
{
	struct dlck_control ctrl = {0};
	int                 rc_abt;
	int                 rc;

	dlck_args_parse(argc, argv, &ctrl);

	D_ASSERT(ctrl.common.cmd < ARRAY_SIZE(dlck_cmds));
	D_ASSERT(ctrl.common.cmd >= 0);

	rc_abt = ABT_init(0, NULL);
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		goto fail_abt;
	}

	rc = dlck_print_main_init(&ctrl.print);
	if (rc != DER_SUCCESS) {
		goto fail_print_main;
	}

	rc = dlck_cmds[ctrl.common.cmd](&ctrl);
	if (rc != DER_SUCCESS) {
		goto fail_cmd;
	}

	rc = dlck_print_main_fini(&ctrl.print);
	if (rc != DER_SUCCESS) {
		goto fail_print_main;
	}

	rc_abt = ABT_finalize();
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		goto fail_abt;
	}

	dlck_args_free(&ctrl);

	return rc;

fail_cmd:
	(void)dlck_print_main_fini(&ctrl.print);
fail_print_main:
	(void)ABT_finalize();
fail_abt:
	dlck_args_free(&ctrl);

	return rc;
}
