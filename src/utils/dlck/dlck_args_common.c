/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <string.h>
#include <stdbool.h>
#include <argp.h>

#include "dlck_args.h"

#define DLCK_OPT_CO_UUID_STR          "co_uuid"
#define DLCK_OPT_NON_ZERO_PADDING_STR "non_zero_padding"

static struct argp_option args_common_options[] = {
    OPT_HEADER("Options:", GROUP_OPTIONS),
    /** entries below inherits the group number of the header entry */
    {"write_mode", KEY_COMMON_WRITE_MODE, 0, 0, "Make changes persistent."},
    {"options", KEY_COMMON_OPTIONS, "OPTIONS", 0,
     "Set options. Options are comma-separated and may include arguments using the equals sign "
     "('='). Please see available options below."},
    OPT_HEADER("Available options:", GROUP_AVAILABLE_CMDS),
    /** entries below inherits the group number of the header entry */
    LIST_ENTRY(DLCK_OPT_CO_UUID_STR "=UUID",
	       "UUID of a container to process. If not provided all containers are processed."),
    LIST_ENTRY(DLCK_OPT_NON_ZERO_PADDING_STR "=EVENT",
	       "Action to take when non-zero padding or reserved fields are detected. EVENT can be "
	       "either 'error' or 'warning'. It is 'error' by default."),
    {0}};

enum dlck_options_values { DLCK_OPT_CO_UUID, DLCK_OPT_NON_ZERO_PADDING };

static char *options_tokens[] = {
    [DLCK_OPT_CO_UUID]          = DLCK_OPT_CO_UUID_STR,
    [DLCK_OPT_NON_ZERO_PADDING] = DLCK_OPT_NON_ZERO_PADDING_STR,
};

static void
args_common_init(struct dlck_args_common *args)
{
	memset(args, 0, sizeof(*args));
	/** set defaults */
	args->write_mode = false; /** dry run */
	args->options.non_zero_padding = DLCK_EVENT_ERROR;
	uuid_clear(args->options.co_uuid);
}

static int
args_common_options_parse(char *options_str, struct dlck_options *opts, struct argp_state *state)
{
	char           *value;
	uuid_t          tmp_uuid;
	enum dlck_event tmp_event;
	int             rc;

	while (*options_str != '\0') {
		switch (getsubopt(&options_str, options_tokens, &value)) {
		case DLCK_OPT_CO_UUID:
			if (value == NULL) {
				RETURN_FAIL(state, EINVAL, MISSING_ARG_FMT, DLCK_OPT_CO_UUID_STR);
			}
			rc = uuid_parse(value, tmp_uuid);
			if (rc != 0) {
				RETURN_FAIL(state, EINVAL, "Malformed uuid: %s", value);
			}
			uuid_copy(opts->co_uuid, tmp_uuid);
			break;
		case DLCK_OPT_NON_ZERO_PADDING:
			tmp_event = parse_event(DLCK_OPT_NON_ZERO_PADDING_STR, value, state, &rc);
			if (tmp_event == DLCK_EVENT_INVALID) {
				return rc;
			}
			opts->non_zero_padding = tmp_event;
			break;
		default:
			RETURN_FAIL(state, EINVAL, "Unknown option: '%s'", value);
		}
	}

	return 0;
}

static error_t
args_common_parser(int key, char *arg, struct argp_state *state)
{
	struct dlck_args_common *args = state->input;
	int                      rc = 0;

	/** state changes */
	switch (key) {
	case ARGP_KEY_INIT:
		args_common_init(args);
		return 0;
	case ARGP_KEY_END:
	case ARGP_KEY_SUCCESS:
	case ARGP_KEY_FINI:
		return 0;
	}

	/** options */
	switch (key) {
	case KEY_COMMON_WRITE_MODE:
		args->write_mode = true;
		break;
	case KEY_COMMON_OPTIONS:
		rc = args_common_options_parse(arg, &args->options, state);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return rc;
}

struct argp argp_common = {args_common_options, args_common_parser, NULL, NULL, NULL};
