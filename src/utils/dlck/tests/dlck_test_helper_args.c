/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <argp.h>

#include "dlck_test_helper_args.h"

/** these options cannot collide with keys already taken in dlck_args.h */
#define KEY_WORKLOAD_OIDS_NUM           'b'
#define KEY_WORKLOAD_KEYS_NUM           'k'
#define KEY_WORKLOAD_UPDATES_NUM        'u'

#define GROUP_WORKLOAD                  2

#define DLCK_HELPER_DEFAULT_OIDS_NUM    2
#define DLCK_HELPER_DEFAULT_KEYS_NUM    4
#define DLCK_HELPER_DEFAULT_UPDATES_NUM 8

static struct argp_option args_workload_options[] = {
    OPT_HEADER("Options:", GROUP_OPTIONS),
    OPT_HEADER("Workload options:", GROUP_WORKLOAD),
    /** entries below inherits the group number of the header entry */
    {"oids_num", KEY_WORKLOAD_OIDS_NUM, "N", 0,
     "Number of OIDs to use. Default: " STRINGIFY(DLCK_HELPER_DEFAULT_OIDS_NUM) "."},
    {"keys_num", KEY_WORKLOAD_KEYS_NUM, "N", 0,
     "Number of keys to use. Default: " STRINGIFY(DLCK_HELPER_DEFAULT_KEYS_NUM) "."},
    {"updates_num", KEY_WORKLOAD_UPDATES_NUM, "N", 0,
     "Number of updates to make. Default: " STRINGIFY(DLCK_HELPER_DEFAULT_UPDATES_NUM) "."},
    {0}};

static void
args_workload_init(struct dlck_helper_workload_args *args)
{
	memset(args, 0, sizeof(*args));
	/** set defaults */
	args->oids_num    = DLCK_HELPER_DEFAULT_OIDS_NUM;
	args->keys_num    = DLCK_HELPER_DEFAULT_KEYS_NUM;
	args->updates_num = DLCK_HELPER_DEFAULT_UPDATES_NUM;
}

static int
args_workload_check(struct argp_state *state, struct dlck_helper_workload_args *args)
{
	if (args->oids_num == 0) {
		RETURN_FAIL(state, EINVAL, "oids_num > 0 is required.");
	}
	if (args->keys_num == 0) {
		RETURN_FAIL(state, EINVAL, "keys_num > 0 is required.");
	}
	if (args->updates_num == 0) {
		RETURN_FAIL(state, EINVAL, "updates_num > 0 is required.");
	}

	return 0;
}

error_t
args_workload_parser(int key, char *arg, struct argp_state *state)
{
	struct dlck_helper_workload_args *args = state->input;
	int                               rc   = 0;

	/** state changes */
	switch (key) {
	case ARGP_KEY_INIT:
		args_workload_init(args);
		return 0;
	case ARGP_KEY_END:
		return args_workload_check(state, args);
	case ARGP_KEY_SUCCESS:
	case ARGP_KEY_FINI:
		return 0;
	}

	/** options */
	switch (key) {
	case KEY_WORKLOAD_OIDS_NUM:
		rc = parse_unsigned(arg, &args->oids_num, state);
		break;
	case KEY_WORKLOAD_KEYS_NUM:
		rc = parse_unsigned(arg, &args->keys_num, state);
		break;
	case KEY_WORKLOAD_UPDATES_NUM:
		rc = parse_unsigned(arg, &args->updates_num, state);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return rc;
}

struct argp        argp_workload = {args_workload_options, args_workload_parser, NULL, NULL, NULL};

/** command-line argument parsing */

extern struct argp argp_file;
extern struct argp argp_engine;

static struct argp_option _automagic[] = {OPT_HEADER("Other options:", GROUP_AUTOMAGIC), {0}};

static struct argp        automagic = {_automagic, NULL};

static struct argp_child  children[] = {
    {&argp_file}, {&argp_engine}, {&argp_workload}, {&automagic}, {0}};

/**
 * \brief Main parser.
 *
 * Just provides inputs for the child parsers.
 */
static error_t
parser(int key, char *arg, struct argp_state *state)
{
	struct dlck_helper_args *args = state->input;

	/** state changes */
	switch (key) {
	case ARGP_KEY_INIT:
		state->child_inputs[0] = &args->files;
		state->child_inputs[1] = &args->engine;
		state->child_inputs[2] = &args->workload;
		return 0;
	}

	return ARGP_ERR_UNKNOWN;
}

static struct argp argp_helper = {NULL, parser, NULL /** usage */, NULL, children};

void
dlck_helper_args_parse(int argc, char **argv, struct dlck_helper_args *args)
{
	error_t ret = argp_parse(&argp_helper, argc, argv, ARGP_NO_EXIT, 0, args);
	if (ret != 0) {
		exit(ret);
	}
}
