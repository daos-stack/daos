/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_ARGS__
#define __DLCK_ARGS__

#include <stdbool.h>
#include <argp.h>
#include <gurt/list.h>

#include <uuid/uuid.h>

#include "dlck_cmds.h"

#define _STRINGIFY(x)                   #x
#define STRINGIFY(x)                    _STRINGIFY(x)

/** documentation groups */

#define GROUP_OPTIONS                   1
#define GROUP_AVAILABLE_CMDS            2
#define GROUP_AUTOMAGIC                 (-1) /** yes, -1 is the last group */

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

/**
 * @struct dlck_file
 *
 * Describes VOS files by its pool UUID and a set of targets involved.
 */
struct dlck_file {
	d_list_t    link;
	uuid_t      po_uuid; /** Pool UUID. */
	int         targets; /** Bitmask of targets involved. */
	const char *desc;    /** Argument provided by the user. */
};

/**
 * @struct dlck_args_engine
 *
 * Arguments necessary to start an engine.
 */
struct dlck_args_engine {
	unsigned numa_node;
	unsigned nvme_mem_size;
	unsigned nvme_hugepage_size;
	unsigned targets;
	char    *storage_path;
	char    *nvme_conf;
};

/**
 * @struct dlck_args_files
 *
 * Provides a list of files and a container.
 */
struct dlck_args_files {
	d_list_t list;
	uuid_t   co_uuid; /** Container UUID. */
};

struct dlck_args_common {
	bool          write_mode; /** false by default (dry run) */
	enum dlck_cmd cmd;
};

struct dlck_args_out {
	int (*printf)(const char *fmt, ...);
};

struct dlck_args {
	struct dlck_args_common common;
	struct dlck_args_files  files;
	struct dlck_args_engine engine;
	struct dlck_args_out    out;
};

/** helper definitions */

#define OPT_HEADER(HEADER, GROUP)                                                                  \
	{                                                                                          \
		0, 0, 0, 0, HEADER, GROUP                                                          \
	}

#define LIST_ENTRY(CMD, DESC)                                                                      \
	{                                                                                          \
		CMD, 0, 0, OPTION_DOC, DESC                                                        \
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

#define DLCK_PRINT(args, fmt)       args->out.printf(fmt)

#define DLCK_PRINTF(args, fmt, ...) args->out.printf(fmt, __VA_ARGS__)

/** dlck_args_parse.c */

/**
 * Extract from \p arg an unsigned \p value.
 *
 * \param[in]	arg	String value.
 * \param[out]	value	Unsigned value.
 * \param[out]	state	State of the parser.
 *
 * \retval 0		Success.
 * \retval EINVAL	Invalid numeric value.
 * \retval EOVERFLOW	Unsigned overflow.
 */
int
parse_unsigned(const char *arg, unsigned *value, struct argp_state *state);

/**
 * Extract from \p arg a file description.
 *
 * \param[in]	arg		String value.
 * \param[out]	state		State of the parser.
 * \param[out]	file_ptr	Extracted file.
 *
 * \retval 0		Success.
 * \retval ENOMEM	Out of memory.
 * \retval EINVAL	No pool UUID provided.
 * \retval EINVAL	Malformed UUID.
 * \retval EINVAL	Invalid numeric value.
 * \retval EOVERFLOW	Unsigned overflow.
 */
int
parse_file(const char *arg, struct argp_state *state, struct dlck_file **file_ptr);

/**
 * Extract a command from \p arg.
 *
 * \param[in]	arg	String value.
 *
 * \retval DLCK_CMD_UNKNOWN	The provided command is unknown.
 * \retval DLCK_CMD_*		DLCK command.
 */
enum dlck_cmd
parse_command(const char *arg);

/** dlck_args.c */

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

#endif /** __DLCK_ARGS__ */