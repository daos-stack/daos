/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(dlck)

#include <daos_errno.h>
#include <daos/debug.h>
#include <daos_version.h>
#include <argp.h>

#include "dlck_args.h"

/** set --version */

#define DAOS_VERSION_STR                                                                           \
	STRINGIFY(DAOS_VERSION_MAJOR)                                                              \
	"." STRINGIFY(DAOS_VERSION_MINOR) "." STRINGIFY(DAOS_VERSION_FIX)

const char *argp_program_version = "dlck " DAOS_VERSION_STR;

/** documentation groups */

#define GROUP_COMMON         1
#define GROUP_AVAILABLE_CMDS 2
#define GROUP_AUTOMAGIC      (-1) /** yes, -1 is the last group */

/** complete help list (in order) */

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
	struct dlck_args *args = state->input;

	/** state changes */
	switch (key) {
	case ARGP_KEY_INIT:
		state->child_inputs[0] = &args->common;
		state->child_inputs[1] = &args->files;
		state->child_inputs[2] = &args->engine;
		break;
	}

	return ARGP_ERR_UNKNOWN;
}

static struct argp argp = {empty_options, parser, NULL /** usage */, doc, children};

/** entry point */

void
dlck_args_parse(int argc, char *argv[], struct dlck_args *args)
{
	error_t ret = argp_parse(&argp, argc, argv, ARGP_IN_ORDER, 0, args);

	if (ret != 0) {
		D_ERROR("Parsing arguments failed: %d", ret);
		exit(ret);
	}
}
