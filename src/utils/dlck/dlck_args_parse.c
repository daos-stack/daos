/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <string.h>
#include <argp.h>
#include <daos/common.h>

#include "dlck_args.h"

int
parse_unsigned(const char *arg, unsigned *value, struct argp_state *state)
{
	char         *endptr = NULL;
	unsigned long ret;

	ret = strtoul(arg, &endptr, 0);

	if (endptr && *endptr != '\0') {
		RETURN_FAIL(state, EINVAL, "Invalid numeric value: %s", arg);
	}

	if (ret == ULONG_MAX || ret > UINT_MAX) {
		RETURN_FAIL(state, EOVERFLOW, "Unsigned overflow: %s", arg);
	}

	*value = ret;

	return 0;
}

#define FILE_SEPARATOR ","
#define FILE_STR_MAX   (UUID_STR_LEN + 10) /** uuid + separator + generous/reasonable number */

int
parse_file(const char *arg, struct argp_state *state, struct dlck_file **file_ptr)
{
	char             *arg_copy;
	char             *token;
	char             *saveptr;
	struct dlck_file *file;
	unsigned          target;
	int               rc;

	D_ALLOC_PTR(file);
	if (file == NULL) {
		RETURN_FAIL(state, ENOMEM, "Out of memory");
	}

	file->desc = arg;

	D_STRNDUP(arg_copy, arg, FILE_STR_MAX);
	if (arg_copy == NULL) {
		FAIL(state, rc, ENOMEM, "Out of memory");
		goto free_file;
	}

	token = strtok_r(arg_copy, FILE_SEPARATOR, &saveptr);
	if (token == NULL) {
		FAIL(state, rc, EINVAL, "No pool UUID provided");
		goto fail;
	}
	rc = uuid_parse(token, file->po_uuid);
	if (rc != 0) {
		FAIL(state, rc, EINVAL, "Malformed uuid: %s", arg);
		goto fail;
	}

	while ((token = strtok_r(NULL, FILE_SEPARATOR, &saveptr))) {
		rc = parse_unsigned(token, &target, state);
		if (rc != 0) {
			goto fail;
		}
		file->targets |= (1 << target);
	}

	D_FREE(arg_copy);

	*file_ptr = file;

	return 0;

fail:
	D_FREE(arg_copy);
free_file:
	D_FREE(file);

	return rc;
}

enum dlck_cmd
parse_command(const char *arg)
{
	if (strcmp(arg, DLCK_CMD_DTX_ACT_RECOVER_STR) == 0) {
		return DLCK_CMD_DTX_ACT_RECOVER;
	}

	return DLCK_CMD_UNKNOWN;
}
