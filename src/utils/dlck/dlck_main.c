/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(dlck)

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <grp.h>
#include <abt.h>

#include <daos_errno.h>
#include <daos/debug.h>
#include <daos/mgmt.h>
#include <daos_srv/daos_engine.h>
#include <gurt/common.h>

#include "dlck_args.h"
#include "dlck_checker.h"
#include "dlck_cmds.h"

#define EFFECTIVE_USER_STR "Effective user: "
#define USER_BELONGS_TO_GRP_FMT "User %sbelong%s to group: %s (gid=%" PRIuMAX ")\n"
#define UNEXPECTED_USER_WARNING_MSG                                                                \
	"\nWARNING: It is recommended to run this program as root or as a user who belongs to "    \
	"the '" DAOS_DEFAULT_SYS_NAME "' group.\n"                                                 \
	"Running it under any other account may cause the program to stop due to insufficient "    \
	"privileges.\n\n"

static bool
user_is_root(struct checker *ck)
{
	uid_t euid = geteuid();

	if (DAOS_FAIL_CHECK(DLCK_MOCK_ROOT)) { /** fault injection */
		/** it does not have ANY effect on the actual privileges of the user */
		euid = 0;
	}

	if (euid == 0) {
		/** The root user is not always named "root" but its uid is always 0. */
		CK_PRINT(ck, EFFECTIVE_USER_STR "root\n");
		return true;
	}

	CK_PRINTF(ck, EFFECTIVE_USER_STR "uid=%" PRIuMAX "\n", (uintmax_t)euid);
	return false;
}

#define MAX_GROUPS 128

static bool
user_belongs_to_group(const char *group_name, struct checker *ck)
{
	struct group *group = NULL;
	gid_t         group_id;
	gid_t         groups[MAX_GROUPS];
	int           rc;

	/** get GID of the requested group */
	if (DAOS_FAIL_CHECK(DLCK_FAULT_GETGRNAM)) { /** fault injection */
		errno = daos_fail_value_get();
	} else if (DAOS_FAIL_CHECK(DLCK_MOCK_NO_DAOS_SERVER_GROUP)) { /** fault injection */
		errno = 0;
	} else {
		errno = 0;
		group = getgrnam(group_name);
	}
	if (group == NULL) {
		if (errno != 0) {
			rc = daos_errno2der(errno);
			CK_PRINTFL_RC(ck, rc, "getgrnam(%s) failed", group_name);
		} else {
			CK_PRINTF(ck, "The %s group does not exist.\n", group_name);
		}
		return false;
	}
	group_id = group->gr_gid;

	/** check primary group */
	if (getgid() == group_id) {
		CK_PRINTF(ck, USER_BELONGS_TO_GRP_FMT, "", "s", group_name, (uintmax_t)group_id);
		return true;
	}

	/** get supplementary groups */
	if (DAOS_FAIL_CHECK(DLCK_FAULT_GETGROUPS)) { /** fault injection */
		rc    = -1;
		errno = daos_fail_value_get();
	} else {
		rc = getgroups(MAX_GROUPS, groups);
	}
	if (rc < 0) {
		rc = daos_errno2der(errno);
		CK_PRINTFL_RC(ck, rc, "getgroups() failed", group_name);
		return false;
	}

	/** check supplementary groups */
	if (!DAOS_FAIL_CHECK(DLCK_MOCK_NOT_IN_DAOS_SERVER_GROUP)) { /** fault injection */
		for (int i = 0; i < rc; i++) {
			if (groups[i] == group_id) {
				CK_PRINTF(ck, USER_BELONGS_TO_GRP_FMT, "", "s", group_name,
					  (uintmax_t)group_id);
				return true;
			}
		}
	}

	CK_PRINTF(ck, USER_BELONGS_TO_GRP_FMT, "DOES NOT ", "", group_name, (uintmax_t)group_id);

	return false;
}

static void
check_user_privileges(struct checker *ck)
{
	if (user_is_root(ck)) {
		/** the root user is assumed to have all required privileges */
		return;
	}

	if (user_belongs_to_group(DAOS_DEFAULT_SYS_NAME, ck)) {
		return;
	}

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

	if (ctrl.common.verbose) {
		rc = daos_debug_init_ex(DAOS_LOG_DEFAULT, DLOG_ERR);
		if (rc != 0) {
			goto err_args_free;
		}
	}

	rc_abt = ABT_init(0, NULL);
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		goto err_args_free;
	}

	rc = dlck_checker_main_init(&ctrl.checker);
	if (rc != DER_SUCCESS) {
		goto err_abt_fini;
	}

	check_user_privileges(&ctrl.checker);

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
	if (ctrl.common.verbose) {
		daos_debug_fini();
	}

	dlck_args_free(&ctrl);
	(void)d_fault_inject_fini();

	return rc;
}
