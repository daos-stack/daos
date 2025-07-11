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

static struct argp_option file_options[] = {
    {"file", KEY_COMMON_FILE, "UUID,TARGET", 0,
     "Pool UUID and set of targets. Can be used more than once.", GROUP_OPTIONS},
    {"co_uuid", KEY_COMMON_CO_UUID, "UUID", 0,
     "UUID of a container to process. If not provided all containers are processed."},
    {0}};

static void
args_init(struct dlck_args_files *args)
{
	memset(args, 0, sizeof(*args));
	/** set defaults */
	D_INIT_LIST_HEAD(&args->list);
	uuid_clear(args->co_uuid);
}

static int
args_check(struct argp_state *state, struct dlck_args_files *args)
{
	if (d_list_empty(&args->list)) {
		RETURN_FAIL(state, EINVAL, "No file chosen");
	}
	return 0;
}

static error_t
parser_file(int key, char *arg, struct argp_state *state)
{
	struct dlck_args_files *args = state->input;
	struct dlck_file       *file;
	uuid_t                  tmp_uuid;
	int                     rc = 0;

	/** state changes */
	switch (key) {
	case ARGP_KEY_INIT:
		args_init(args);
		return 0;
	case ARGP_KEY_END:
		return args_check(state, args);
	case ARGP_KEY_SUCCESS:
	case ARGP_KEY_FINI:
		return 0;
	}

	/** options */
	switch (key) {
	case KEY_COMMON_FILE:
		rc = parse_file(arg, state, &file);
		if (rc == 0) {
			d_list_add_tail(&file->link, &args->list);
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

struct argp argp_file = {file_options, parser_file, NULL, NULL, NULL};
