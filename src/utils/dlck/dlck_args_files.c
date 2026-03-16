/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <string.h>
#include <uuid/uuid.h>
#include <argp.h>

#include <gurt/common.h>
#include <gurt/list.h>

#include "dlck_args.h"

static struct argp_option args_files_options[] = {
    {"file", KEY_FILES, "UUID[,TARGET]", 0,
     "Pool UUID and a set of targets. If no TARGET is provided, all targets are used. This option "
     "can be specified multiple times. By default all pools are used.",
     GROUP_OPTIONS},
    {0}};

static inline void
args_files_init(struct dlck_args_files *args)
{
	memset(args, 0, sizeof(*args));
	/** set defaults */
	D_INIT_LIST_HEAD(&args->list);
}

static error_t
args_files_parser(int key, char *arg, struct argp_state *state)
{
	struct dlck_args_files *args = state->input;
	struct dlck_file       *file;
	int                     rc = 0;

	/** state changes */
	switch (key) {
	case ARGP_KEY_INIT:
		args_files_init(args);
		return 0;
	case ARGP_KEY_END:
	case ARGP_KEY_SUCCESS:
	case ARGP_KEY_FINI:
		return 0;
	}

	/** options */
	switch (key) {
	case KEY_FILES:
		rc = parse_file(arg, state, &file);
		if (rc == 0) {
			d_list_add(&file->link, &args->list);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return rc;
}

struct argp argp_file = {args_files_options, args_files_parser, NULL, NULL, NULL};

void
dlck_args_files_free(struct dlck_args_files *args)
{
	struct dlck_file *file;
	struct dlck_file *next;

	d_list_for_each_entry_safe(file, next, &args->list, link) {
		d_list_del(&file->link);
		D_FREE(file);
	}
}
