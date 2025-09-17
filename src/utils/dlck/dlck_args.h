/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_ARGS__
#define __DLCK_ARGS__

#include <stdbool.h>
#include <stdint.h>
#include <argp.h>
#include <uuid/uuid.h>
#include <gurt/list.h>

#include "dlck_cmds.h"

#define _STRINGIFY(x)                   #x
#define STRINGIFY(x)                    _STRINGIFY(x)

/** documentation groups */

#define GROUP_OPTIONS                   1
#define GROUP_AVAILABLE_CMDS            2
#define GROUP_AUTOMAGIC                 (-1) /** yes, -1 is the last group */

/** all short options */
#define KEY_COMMON_CMD                  'c'
#define KEY_COMMON_CO_UUID              'q'
#define KEY_COMMON_WRITE_MODE           'w'
#define KEY_FILES                       'f'
/** the options below follow the daos_engine options */
#define KEY_ENGINE_NUMA_NODE            'p'
#define KEY_ENGINE_MAX_DMA_BUF_SIZE     'r'
#define KEY_ENGINE_HUGEPAGE_SIZE        'H'
#define KEY_ENGINE_TARGETS              't'
#define KEY_ENGINE_STORAGE              's'
#define KEY_ENGINE_NVME                 'n'

/** defaults */
#define DLCK_DEFAULT_MAX_DMA_BUF_SIZE   5120
#define DLCK_DEFAULT_NVME_HUGEPAGE_SIZE 2
#define DLCK_DEFAULT_TARGETS            4

#define DLCK_TARGET_MAX                 31

struct dlck_args_common {
	enum dlck_cmd cmd;
	uuid_t        co_uuid;    /** Container UUID. */
	bool          write_mode; /** false by default (dry run) */
};

/**
 * @struct dlck_file
 *
 * Describes VOS files by its pool UUID and a set of targets involved.
 */
struct dlck_file {
	d_list_t    link;
	uuid_t      po_uuid;        /** Pool UUID. */
	uint32_t    targets_bitmap; /** Bitmap of targets involved. */
	const char *desc;           /** Argument provided by the user. */
};

/**
 * @struct dlck_args_engine
 *
 * Arguments necessary to start an engine.
 */
struct dlck_args_engine {
	unsigned numa_node;
	unsigned max_dma_buf_size;
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
};

struct dlck_print {
	int (*dp_printf)(const char *fmt, ...);
};

struct dlck_control {
	/** in */
	struct dlck_args_common common;
	struct dlck_args_files  files;
	struct dlck_args_engine engine;
	/** print */
	struct dlck_print       print;
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

#define DLCK_PRINT(ctrl, fmt)       (void)ctrl->print.dp_printf(fmt)

#define DLCK_PRINTF(ctrl, fmt, ...) (void)ctrl->print.dp_printf(fmt, __VA_ARGS__)

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

/** dlck_args_files.c */

/**
 * Free arguments.
 *
 * \param[in]	ctrl	Control state with arguments to free.
 */
void
dlck_args_free(struct dlck_control *ctrl);

/**
 * Free file arguments.
 *
 * \param[in]	args	Arguments to free.
 */
void
dlck_args_files_free(struct dlck_args_files *args);

/** dlck_args.c */

/**
 * \brief Parse provided argc/argv, validate and write down into \p args state.
 *
 * It may close the calling process if requested for version or help.
 *
 * \param[in]   argc	Length of the \p argv array.
 * \param[in]   argv  	Standard list of arguments.
 * \param[out]	control	Control state to store the arguments in.
 */
void
dlck_args_parse(int argc, char *argv[], struct dlck_control *control);

/**
 * Free arguments.
 *
 * \param[in]	ctrl	Control state with arguments to free.
 */
void
dlck_args_free(struct dlck_control *ctrl);

#endif /** __DLCK_ARGS__ */
