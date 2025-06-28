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

#define _STRINGIFY(x) #x
#define STRINGIFY(x)  _STRINGIFY(x)

#define DAOS_VERSION_STR                                                                           \
	STRINGIFY(DAOS_VERSION_MAJOR)                                                              \
	"." STRINGIFY(DAOS_VERSION_MINOR) "." STRINGIFY(DAOS_VERSION_FIX)

const char *argp_program_version = "dlck " DAOS_VERSION_STR;

/** documentation groups */

#define GROUP_COMMON                 1
#define GROUP_AVAILABLE_CMDS         2
#define GROUP_AUTOMAGIC              (-1) /** yes, -1 is the last group */

/** helper definitions */

#define OPT_HEADER(HEADER, GROUP)    {0, 0, 0, 0, HEADER, GROUP}

#define POSITIONAL(ARG, DESC, GROUP) {ARG, 0, 0, OPTION_DOC, DESC, GROUP}

#define LIST_ENTRY(CMD, DESC)        {CMD, 0, 0, OPTION_DOC, DESC}

/** complete help list (in order) */

/** XXX provide more details here. */
static char               doc[] = "\nDAOS Local Consistency Checker (dlck)";

static struct argp_option common_options[] = {
    OPT_HEADER("Common options:", GROUP_COMMON),
    /** entries below inherits the group number of the header entry */
    {"write_mode", KEY_COMMON_WRITE_MODE, 0, 0, "Make changes persistent."},
    {"file", KEY_COMMON_FILE, "UUID,TARGET", 0,
     "Pool UUID and set of targets. Can be used more than once."},
    {"co_uuid", KEY_COMMON_CO_UUID, "UUID", 0,
     "UUID of a container to process. If not provided all containers are processed."},
    {"cmd", KEY_COMMON_CMD, "CMD", 0, "Command (Required). Please see available commands below."},
    {"pinned_numa_node", KEY_COMMON_NUMA_NODE, 0, 0,
     "Bind to cores within the specified NUMA node."},
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

static struct argp_option _cmds_list[] = {
    OPT_HEADER("Available commands:", GROUP_AVAILABLE_CMDS),
    /** entries below inherits the group number of the header entry */
    LIST_ENTRY(DLCK_CMD_DTX_ACT_RECOVER_STR, "Active DTX entries' records recovery."),
    {0}};

static struct argp_option _automagic[] = {OPT_HEADER("Other options:", GROUP_AUTOMAGIC), {0}};

/** glue everything together */

static struct argp        cmds_list = {_cmds_list, NULL};

static struct argp        automagic = {_automagic, NULL};

static struct argp_child  children[] = {{&cmds_list}, {&automagic}, {0}};

static struct argp        argp = {common_options, parser_common, NULL /** usage */, doc, children};

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
