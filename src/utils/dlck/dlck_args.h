/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_ARGS__
#define __DLCK_ARGS__

#include <argp.h>
#include <gurt/list.h>

#include <uuid/uuid.h>

#include "dlck_cmds.h"

/** all short options */

#define KEY_COMMON_WRITE_MODE           'w'
#define KEY_COMMON_FILE                 'f'
#define KEY_COMMON_CO_UUID              'q'
#define KEY_COMMON_CMD                  'c'
/** the options below follow the daos_engine options */
#define KEY_COMMON_NUMA_NODE            'p'
#define KEY_COMMON_MEM_SIZE             'r'
#define KEY_COMMON_HUGEPAGE_SIZE        'H'
#define KEY_COMMON_TARGETS              't'
#define KEY_COMMON_STORAGE              's'
#define KEY_COMMON_NVME                 'n'

/** defaults */

#define DLCK_DEFAULT_NVME_MEM_SIZE      5120
#define DLCK_DEFAULT_NVME_HUGEPAGE_SIZE 2
#define DLCK_DEFAULT_TARGETS            4

struct dlck_file {
	d_list_t    link;
	const char *desc;
	uuid_t      po_uuid;
	int         targets;
};

struct dlck_args_common {
	bool          write_mode; /** false by default (dry run) */
	d_list_t      files;
	uuid_t        co_uuid;
	enum dlck_cmd cmd;
	unsigned      numa_node;
	unsigned      nvme_mem_size;
	unsigned      nvme_hugepage_size;
	unsigned      targets;
	char         *storage_path;
	char         *nvme_conf;
};

struct dlck_args {
	struct dlck_args_common common;
};

/**
 * \brief Parse provided argc/argv, validate and write down into \p args state.
 *
 * It may close the calling process if requested for version or help.
 *
 * \param[in]   argc  Length of the \p argv array.
 * \param[in]   argv  Standard list of arguments.
 * \param[out]	args	Parsed arguments.
 */
void
dlck_args_parse(int argc, char *argv[], struct dlck_args *args);

/** dlck_args_parser.c */

/**
 * XXX
 */
error_t
parser_common(int key, char *arg, struct argp_state *state);

#endif /** __DLCK_ARGS__ */