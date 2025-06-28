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

static void
args_init(struct dlck_args *args)
{
	memset(args, 0, sizeof(struct dlck_args));
	/** set defaults */
	D_INIT_LIST_HEAD(&args->common.files);
	uuid_clear(args->common.co_uuid);
	args->common.write_mode         = false; /** dry run */
	args->common.cmd                = DLCK_CMD_NOT_SET;
	args->common.nvme_mem_size      = DLCK_DEFAULT_NVME_MEM_SIZE;
	args->common.nvme_hugepage_size = DLCK_DEFAULT_NVME_HUGEPAGE_SIZE;
	args->common.targets            = DLCK_DEFAULT_TARGETS;
}

#define FAIL(STATE, RC, ERRNUM, ...)                                                               \
	do {                                                                                       \
		argp_failure(STATE, ERRNUM, ERRNUM, __VA_ARGS__);                                  \
		RC = ERRNUM;                                                                       \
	} while (0)

#define RETURN_FAIL(STATE, ERRNUM, ...)                                                            \
	do {                                                                                       \
		argp_failure(STATE, ERRNUM, ERRNUM, __VA_ARGS__);                                  \
		return ERRNUM;                                                                     \
	} while (0)

static int
args_check(struct argp_state *state, struct dlck_args *args)
{
	if (args->common.cmd == DLCK_CMD_NOT_SET) {
		RETURN_FAIL(state, EINVAL, "Command not set");
	}
	if (d_list_empty(&args->common.files)) {
		RETURN_FAIL(state, EINVAL, "No file chosen");
	}
	return 0;
}

static enum dlck_cmd
parse_command(const char *arg)
{
	if (strcmp(arg, DLCK_CMD_DTX_ACT_RECOVER_STR) == 0) {
		return DLCK_CMD_DTX_ACT_RECOVER;
	}

	return DLCK_CMD_UNKNOWN;
}

static int
parse_unsigned(const char *arg, unsigned *value, struct argp_state *state)
{
	char         *endptr = NULL;
	unsigned long ret;

	ret = strtoul(arg, &endptr, 0);

	if (endptr && *endptr != '\0') {
		RETURN_FAIL(state, EINVAL, "Invalid numeric value: %s", arg);
	}

	if (ret == ULONG_MAX || ret > UINT_MAX) {
		RETURN_FAIL(state, EINVAL, "Unsigned overflow: %s", arg);
	}

	*value = ret;

	return 0;
}

#define FILE_SEPARATOR ","

static int
parse_file(const char *arg, struct dlck_args *args, struct argp_state *state)
{
	char             *arg_copy;
	char             *token;
	char             *saveptr;
	struct dlck_file *file;
	unsigned          target;
	int               rc;

	D_ALLOC_PTR(file);
	if (file == NULL) {
		RETURN_FAIL(state, ENOMEM, "Cannot append more files");
	}

	file->desc = arg;

	D_STRNDUP(arg_copy, arg, 1024);
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
	}

	while ((token = strtok_r(NULL, FILE_SEPARATOR, &saveptr))) {
		rc = parse_unsigned(token, &target, state);
		if (rc != 0) {
			goto fail;
		}
		file->targets |= (1 << target);
	}

	d_list_add_tail(&file->link, &args->common.files);

	D_FREE(arg_copy);

	return 0;

fail:
	D_FREE(arg_copy);
free_file:
	D_FREE(file);

	return rc;
}

error_t
parser_common(int key, char *arg, struct argp_state *state)
{
	struct dlck_args *args = state->input;
	uuid_t            tmp_uuid;
	int               rc = 0;

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
	case KEY_COMMON_WRITE_MODE:
		args->common.write_mode = true;
		break;
	case KEY_COMMON_FILE:
		rc = parse_file(arg, args, state);
		break;
	case KEY_COMMON_CO_UUID:
		rc = uuid_parse(arg, tmp_uuid);
		if (rc != 0) {
			RETURN_FAIL(state, EINVAL, "Malformed uuid: %s", arg);
		}
		uuid_copy(args->common.co_uuid, tmp_uuid);
		break;
	case KEY_COMMON_CMD:
		args->common.cmd = parse_command(arg);
		if (args->common.cmd == DLCK_CMD_UNKNOWN) {
			RETURN_FAIL(state, EINVAL, "Unknown command: %s", arg);
		}
		break;
	case KEY_COMMON_NUMA_NODE:
		rc = parse_unsigned(arg, &args->common.numa_node, state);
		break;
	case KEY_COMMON_MEM_SIZE:
		rc = parse_unsigned(arg, &args->common.nvme_mem_size, state);
		break;
	case KEY_COMMON_HUGEPAGE_SIZE:
		rc = parse_unsigned(arg, &args->common.nvme_hugepage_size, state);
		break;
	case KEY_COMMON_TARGETS:
		rc = parse_unsigned(arg, &args->common.targets, state);
		break;
	case KEY_COMMON_STORAGE:
		args->common.storage_path = arg;
		break;
	case KEY_COMMON_NVME:
		args->common.nvme_conf = arg;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return rc;
}
