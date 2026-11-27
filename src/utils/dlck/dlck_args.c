/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(dlck)

#include <stdlib.h>
#include <argp.h>
#include <daos_errno.h>
#include <daos/debug.h>
#include <daos_version.h>

#include "dlck_args.h"

/** set --version */

#define DAOS_VERSION_STR                                                                           \
	STRINGIFY(DAOS_VERSION_MAJOR)                                                              \
	"." STRINGIFY(DAOS_VERSION_MINOR) "." STRINGIFY(DAOS_VERSION_FIX)

const char               *argp_program_version = "dlck " DAOS_VERSION_STR;

/** XXX provide more details here. */
static char               doc[] = "\nDAOS Local Consistency Checker (dlck)";

static struct argp_option empty_options[] = {{0}};

static struct argp_option _automagic[] = {OPT_HEADER("Other options:", GROUP_AUTOMAGIC), {0}};

/** glue everything together */

extern struct argp        argp_common;
extern struct argp        argp_file;
extern struct argp        argp_engine;

static struct argp        automagic = {_automagic, NULL};

static struct argp_child  children[] = {
    {&argp_common}, {&argp_file}, {&argp_engine}, {&automagic}, {0}};

error_t
parser(int key, char *arg, struct argp_state *state)
{
	struct dlck_control *ctrl = state->input;

	/** state changes */
	switch (key) {
	case ARGP_KEY_INIT:
		/** the following has to match the order of the children parsers */
		state->child_inputs[0] = &ctrl->common;
		state->child_inputs[1] = &ctrl->files;
		state->child_inputs[2] = &ctrl->engine;
		break;
	}

	return ARGP_ERR_UNKNOWN;
}

static struct argp argp = {empty_options, parser, NULL /** usage */, doc, children};

void
dlck_env(struct dlck_control *ctrl)
{
	char *env;

	/**
	 * Provide an environment variable to disable checking of specific scopes.
	 * When no environment variable is set, the corresponding scope will be checked.
	 */
	ctrl->scope.pools      = d_agetenv_str(&env, "DLCK_CHECK_NO_POOLS") != DER_SUCCESS;
	ctrl->scope.containers = d_agetenv_str(&env, "DLCK_CHECK_NO_CONTAINERS") != DER_SUCCESS;
	ctrl->scope.objects    = d_agetenv_str(&env, "DLCK_CHECK_NO_OBJECTS") != DER_SUCCESS;
}

void
dlck_args_parse(int argc, char *argv[], struct dlck_control *ctrl)
{
	error_t ret;

	dlck_env(ctrl);

	ret = argp_parse(&argp, argc, argv, ARGP_IN_ORDER, 0, ctrl);
	if (ret != 0) {
		D_ERROR("Parsing arguments failed: %d", ret);
		exit(ret);
	}
}

void
dlck_args_free(struct dlck_control *ctrl)
{
	dlck_args_files_free(&ctrl->files);
}
