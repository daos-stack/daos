/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(dlck)

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>
#include <abt.h>

#include <daos_errno.h>
#include <daos/debug.h>
#include <daos/rpc.h>
#include <daos_srv/daos_engine.h>
#include <gurt/common.h>

#include "dlck_args.h"
#include "dlck_checker.h"
#include "dlck_cmds.h"

#define EFFECTIVE_USER_STR "Effective user: "
#define UNEXPECTED_USER_WARNING_MSG                                                                \
	"WARNING: It is recommended to run this program as root or user '" DAOS_DEFAULT_SYS_NAME   \
	"'.\n"                                                                                     \
	"These accounts are expected to have the necessary privileges.\n"                          \
	"Running under other users may cause the program to stop due to insufficient "             \
	"privileges.\n\n"

static void
check_user(struct checker *ck)
{
	uid_t          euid = geteuid();
	struct passwd *pw;
	int            ret;

	/** The root user is not always named "root" but its uid is always 0. */
	if (euid == 0) {
		/** the root user have all the privileges */
		CK_PRINT(ck, EFFECTIVE_USER_STR "root\n");
		return;
	}

	pw = getpwuid(euid);
	if (pw == NULL || pw->pw_name == NULL) {
		ret = d_errno2der(errno);
		CK_PRINTFL_RC(ck, ret, "Cannot get the name of a user for uid=%" PRIuMAX,
			      (uintmax_t)euid);
	}

	if (strncmp(pw->pw_name, DAOS_DEFAULT_SYS_NAME, DAOS_SYS_NAME_MAX) == 0) {
		/** the daos_server user ought to have all the necessary privileges */
		CK_PRINT(ck, EFFECTIVE_USER_STR DAOS_DEFAULT_SYS_NAME "\n");
		return;
	}

	CK_PRINTF(ck, EFFECTIVE_USER_STR "%s (uid=%" PRIuMAX ")\n", pw->pw_name, (uintmax_t)euid);
	CK_PRINT(ck, UNEXPECTED_USER_WARNING_MSG);
}

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

	rc = dlck_checker_main_init(&ctrl.checker);
	if (rc != DER_SUCCESS) {
		goto err_abt_fini;
	}

	check_user(&ctrl.checker);

	rc = dlck_cmd_check(&ctrl);
	if (rc != DER_SUCCESS) {
		goto err_print_main_fini;
	}

	rc = dlck_checker_main_fini(&ctrl.checker);
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
	(void)dlck_checker_main_fini(&ctrl.checker);
err_abt_fini:
	(void)ABT_finalize();
err_args_free:
	dlck_args_free(&ctrl);
	(void)d_fault_inject_fini();

	return rc;
}
