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

static struct argp_option engine_options[] = {
    {"pinned_numa_node", KEY_COMMON_NUMA_NODE, 0, 0,
     "Bind to cores within the specified NUMA node.", GROUP_OPTIONS},
    {"mem_size", KEY_COMMON_MEM_SIZE, "N", 0,
     "Allocates mem_size MB for SPDK. Default: " STRINGIFY(DLCK_DEFAULT_NVME_MEM_SIZE) "."},
    {"hugepage_size", KEY_COMMON_HUGEPAGE_SIZE, "N", 0,
     "Passes the configured hugepage size(2MB or 1GB). Default: " STRINGIFY(
	 DLCK_DEFAULT_NVME_HUGEPAGE_SIZE) "."},
    {"targets", KEY_COMMON_TARGETS, "N", 0,
     "Number of targets to use. Default: " STRINGIFY(DLCK_DEFAULT_TARGETS) "."},
    {"storage", KEY_COMMON_STORAGE, "PATH", 0, "Storage path."},
    {"nvme", KEY_COMMON_NVME, "PATH", 0, "NVMe config file."},
    {0}};

static void
args_init(struct dlck_args_engine *args)
{
	memset(args, 0, sizeof(*args));
	/** set defaults */
	args->nvme_mem_size      = DLCK_DEFAULT_NVME_MEM_SIZE;
	args->nvme_hugepage_size = DLCK_DEFAULT_NVME_HUGEPAGE_SIZE;
	args->targets            = DLCK_DEFAULT_TARGETS;
}

static int
args_check(struct argp_state *state, struct dlck_args_engine *args)
{
	/* XXX */
	return 0;
}

error_t
parser_engine(int key, char *arg, struct argp_state *state)
{
	struct dlck_args_engine *args = state->input;
	int                      rc   = 0;

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
	case KEY_COMMON_NUMA_NODE:
		rc = parse_unsigned(arg, &args->numa_node, state);
		break;
	case KEY_COMMON_MEM_SIZE:
		rc = parse_unsigned(arg, &args->nvme_mem_size, state);
		break;
	case KEY_COMMON_HUGEPAGE_SIZE:
		rc = parse_unsigned(arg, &args->nvme_hugepage_size, state);
		break;
	case KEY_COMMON_TARGETS:
		rc = parse_unsigned(arg, &args->targets, state);
		break;
	case KEY_COMMON_STORAGE:
		args->storage_path = arg;
		break;
	case KEY_COMMON_NVME:
		args->nvme_conf = arg;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return rc;
}

struct argp argp_engine = {engine_options, parser_engine, NULL, NULL, NULL};
