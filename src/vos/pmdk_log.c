/**
 * (C) Copyright 2024 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Connecting the PMDK's logging to DAOS logging.
 *
 * vos/pmdk_log.c
 */

#define D_LOGFAC DD_FAC(pmdk)

#ifdef DAOS_PMEM_BUILD
#include <daos/debug.h>
#include <daos/common.h>
#include <libpmemobj/log.h>
#include <libpmemobj.h>

#define PMDK_LOG_2_DAOS_LOG_INIT(PMDK_LEVEL, DAOS_LEVEL)                                           \
	[PMDK_LEVEL] = {.level = DAOS_LEVEL, .saved_mask = &DD_FLAG(DAOS_LEVEL, D_LOGFAC)}

static struct {
	int  level;
	int *saved_mask;
} pmemobj_log_level_2_daos_log[] = {
    PMDK_LOG_2_DAOS_LOG_INIT(PMEMOBJ_LOG_LEVEL_HARK, DLOG_INFO),
    PMDK_LOG_2_DAOS_LOG_INIT(PMEMOBJ_LOG_LEVEL_FATAL, DLOG_CRIT),
    PMDK_LOG_2_DAOS_LOG_INIT(PMEMOBJ_LOG_LEVEL_ERROR, DLOG_ERR),
    PMDK_LOG_2_DAOS_LOG_INIT(PMEMOBJ_LOG_LEVEL_WARNING, DLOG_WARN),
    PMDK_LOG_2_DAOS_LOG_INIT(PMEMOBJ_LOG_LEVEL_NOTICE, DLOG_NOTE),
    PMDK_LOG_2_DAOS_LOG_INIT(PMEMOBJ_LOG_LEVEL_INFO, DLOG_INFO),
    PMDK_LOG_2_DAOS_LOG_INIT(PMEMOBJ_LOG_LEVEL_DEBUG, DLOG_DBG),
};

#undef PMDK_LOG_2_DAOS_LOG_INIT

static void
pmdk_log_function(enum pmemobj_log_level level, const char *file_name, unsigned line_no,
		  const char *function_name, const char *message)
{
	/* normalize file path - remove leading "../" */
	while ((*file_name == '.') && (*(file_name + 1) == '.') && (*(file_name + 2) == '/')) {
		file_name += 3;
	}

	/* simplify file path by removing cyclic pattern "src/../src" */
	if (strncmp(file_name, "src/../src", sizeof("src/../src") - 1) == 0) {
		file_name += sizeof("src/../") - 1;
	}

	/** Add "pmdk/" prefix to file name
	 * Prefix is needed to filter out PMDK messages in NLT results analysis
	 * as it is implemented in https://github.com/daos-stack/pipeline-lib/pull/457
	 */
#define PMDK_LOG_FUNCTION_MAX_FILENAME 255
	char  file_name_buff[PMDK_LOG_FUNCTION_MAX_FILENAME] = "pmdk/";
	char *local_file_name                                = file_name_buff + sizeof("pmdk/") - 1;
	while ((local_file_name < file_name_buff + PMDK_LOG_FUNCTION_MAX_FILENAME - 1) &&
	       (*file_name != '\0')) {
		*(local_file_name++) = *(file_name++);
	}
	*local_file_name = '\0';

/*
 * There is a set of handy macros for each of the message priorities
 * that are used normally to report a message. They can't be used here
 * directly since the file name, line number and the function name
 * are provided via arguments to this callback function instead of
 * via macro definitions (__FILE__, __LINE__, and __func__) as
 * _D_LOG_NOCHECK() would like to consume them. So, the message here is
 * provided a few macro-calls later via _D_DEBUG_W_SAVED_MASK() macro which allows
 * to swap the _D_LOG_NOCHECK macro for a custom macro.
 *
 * D_ERROR(...) -> D_DEBUG(DLOG_ERR, ...) ->
 *      _D_DEBUG(_D_LOG_NOCHECK, DLOG_ERR, ...) ->
 *      _D_DEBUG_W_SAVED_MASK(_D_LOG_NOCHECK,  DD_FLAG(DLOG_ERR, D_LOGFAC), DLOG_ERR,  ...)
 */

/*
 * A custom variant of _D_LOG_NOCHECK() which passes the file name,
 * line number and the function name from the local variables.
 */
#define PMDK_LOG_NOCHECK(mask, fmt, ...)                                                           \
	d_log(mask, "%s:%d %s() " fmt, file_name_buff, line_no, function_name, ##__VA_ARGS__)

	int *saved_mask = pmemobj_log_level_2_daos_log[level].saved_mask;
	_D_DEBUG_W_SAVED_MASK(PMDK_LOG_NOCHECK, *saved_mask,
			      pmemobj_log_level_2_daos_log[level].level, "%s\n", message);

#undef PMDK_LOG_NOCHECK
}

int
pmdk_log_attach(void)
{
	int rc = pmemobj_log_set_function(pmdk_log_function);
	if (rc == 0) {
		return 0;
	} else {
		return daos_errno2der(errno);
	}
}

#else

int
pmdk_log_attach(void)
{
	;
}

#endif /* DAOS_PMEM_BUILD */
