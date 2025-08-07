/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <string.h>
#include <stdbool.h>
#include <argp.h>

#include "dlck_args.h"

static struct argp_option args_common_options[] = {
    OPT_HEADER("Options:", GROUP_OPTIONS),
    /** entries below inherits the group number of the header entry */
    {"write_mode", KEY_COMMON_WRITE_MODE, 0, 0, "Make changes persistent."},
    {"cmd", KEY_COMMON_CMD, "CMD", 0, "Command (Required). Please see available commands below."},
    {"co_uuid", KEY_COMMON_CO_UUID, "UUID", 0,
     "UUID of a container to process. If not provided all containers are processed."},
    OPT_HEADER("Available commands:", GROUP_AVAILABLE_CMDS),
    /** entries below inherits the group number of the header entry */
    LIST_ENTRY(DLCK_CMD_DTX_ACT_RECOVER_STR, "Active DTX entries' records recovery."),
    {0}};

static void
args_common_init(struct dlck_args_common *args)
{
	memset(args, 0, sizeof(*args));
	/** set defaults */
	args->write_mode = false; /** dry run */
	args->cmd        = DLCK_CMD_NOT_SET;
	uuid_clear(args->co_uuid);
}

static int
args_common_check(struct argp_state *state, struct dlck_args_common *args)
{
	if (args->cmd == DLCK_CMD_NOT_SET) {
		RETURN_FAIL(state, EINVAL, "Command not set");
	}
	return 0;
}

static error_t
args_common_parser(int key, char *arg, struct argp_state *state)
{
	struct dlck_args_common *args = state->input;
	uuid_t                   tmp_uuid;
	int                      rc = 0;

	/** state changes */
	switch (key) {
	case ARGP_KEY_INIT:
		args_common_init(args);
		return 0;
	case ARGP_KEY_END:
		return args_common_check(state, args);
	case ARGP_KEY_SUCCESS:
	case ARGP_KEY_FINI:
		return 0;
	}

	/** options */
	switch (key) {
	case KEY_COMMON_WRITE_MODE:
		args->write_mode = true;
		break;
	case KEY_COMMON_CMD:
		args->cmd = parse_command(arg);
		if (args->cmd == DLCK_CMD_UNKNOWN) {
			RETURN_FAIL(state, EINVAL, "Unknown command: %s", arg);
		}
		break;
	case KEY_COMMON_CO_UUID:
		rc = uuid_parse(arg, tmp_uuid);
		if (rc != 0) {
			RETURN_FAIL(state, EINVAL, "Malformed uuid: %s", arg);
		}
		uuid_copy(args->co_uuid, tmp_uuid);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return rc;
}

struct argp argp_common = {args_common_options, args_common_parser, NULL, NULL, NULL};
