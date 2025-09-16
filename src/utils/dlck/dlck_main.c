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

int
main(int argc, char *argv[])
{
	struct dlck_control ctrl = {0};
	int                 rc_abt;
	int                 rc;

	rc = d_fault_inject_init();
	if (rc != DER_SUCCESS && rc != -DER_NOSYS) {
		return rc;
	}

	if (d_fault_inject_is_enabled()) {
		/** an errno value the fault injection will trigger */
		daos_fail_value_set(EINVAL);
	}

	dlck_args_parse(argc, argv, &ctrl);

	rc_abt = ABT_init(0, NULL);
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		goto err_args_free;
	}

	rc = dlck_print_main_init(&ctrl.print);
	if (rc != DER_SUCCESS) {
		goto err_abt_fini;
	}

	rc = dlck_cmd_check(&ctrl);
	if (rc != DER_SUCCESS) {
		goto err_print_main_fini;
	}

	rc = dlck_print_main_fini(&ctrl.print);
	if (rc != DER_SUCCESS) {
		goto err_abt_fini;
	}

	rc_abt = ABT_finalize();
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		goto err_args_free;
	}

	dlck_args_free(&ctrl);

	rc = d_fault_inject_fini();
	if (rc == -DER_NOSYS) {
		rc = DER_SUCCESS;
	}

	return rc;

err_print_main_fini:
	(void)dlck_print_main_fini(&ctrl.print);
err_abt_fini:
	(void)ABT_finalize();
err_args_free:
	dlck_args_free(&ctrl);
	(void)d_fault_inject_fini();

	return rc;
}
