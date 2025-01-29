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

static uint64_t pmemobj_log_level_2_dlog_prio[] = {
    [PMEMOBJ_LOG_LEVEL_HARK] = DLOG_INFO,   [PMEMOBJ_LOG_LEVEL_FATAL] = DLOG_CRIT,
    [PMEMOBJ_LOG_LEVEL_ERROR] = DLOG_ERR,   [PMEMOBJ_LOG_LEVEL_WARNING] = DLOG_WARN,
    [PMEMOBJ_LOG_LEVEL_NOTICE] = DLOG_NOTE, [PMEMOBJ_LOG_LEVEL_INFO] = DLOG_INFO,
    [PMEMOBJ_LOG_LEVEL_DEBUG] = DLOG_DBG,
};

static int * pmemobj_log_level_2_flag_var[] = {
    [PMEMOBJ_LOG_LEVEL_HARK] = &DD_FLAG(DLOG_INFO, D_LOGFAC),
    [PMEMOBJ_LOG_LEVEL_FATAL] = &DD_FLAG(DLOG_CRIT, D_LOGFAC),
    [PMEMOBJ_LOG_LEVEL_ERROR] = &DD_FLAG(DLOG_ERR, D_LOGFAC),
    [PMEMOBJ_LOG_LEVEL_WARNING] = &DD_FLAG(DLOG_WARN, D_LOGFAC),
    [PMEMOBJ_LOG_LEVEL_NOTICE] = &DD_FLAG(DLOG_NOTE, D_LOGFAC),
    [PMEMOBJ_LOG_LEVEL_INFO] = &DD_FLAG(DLOG_INFO, D_LOGFAC),
    [PMEMOBJ_LOG_LEVEL_DEBUG] = &DD_FLAG(DLOG_DBG, D_LOGFAC),
};

static void
pmdk_log_function(enum pmemobj_log_level level, const char *file_name, unsigned line_no,
		  const char *function_name, const char *message)
{
	uint64_t dlog_prio = pmemobj_log_level_2_dlog_prio[level];
	int *flag_var = pmemobj_log_level_2_flag_var[level];

/*
 * There is a set of handy macros for each of the message priorities
 * that are used normally to report a message. They can't be used here
 * directly since the file name, line number and the function name
 * are provided via arguments to this callback function instead of
 * via macro definitions (__FILE__, __LINE__, and __func__) as
 * _D_LOG_NOCHECK() would like to consume them. So, the message here is
 * provided a few macro-calls later via _D_DEBUG() macro which allows
 * to swap the _D_LOG_NOCHECK macro for a custom macro.
 *
 * D_ERROR(...) -> D_DEBUG(DLOG_ERR, ...) ->
 *      _D_DEBUG(_D_LOG_NOCHECK, DLOG_ERR, ...)
 */

/*
 * A custom variant of _D_LOG_NOCHECK() which passes the file name,
 * line number and the function name from the local variables.
 */
#define PMDK_LOG_NOCHECK(mask, fmt, ...)                                                           \
	d_log(mask, "%s:%d %s() " fmt, file_name, line_no, function_name, ##__VA_ARGS__)

	_D_DEBUGX(PMDK_LOG_NOCHECK, dlog_prio, *flag_var, "%s\n", message);

/*
 * The calculated message priority can't be passed as an argument to
 * the _D_DEBUG() since the argument's name is used to construct
 * a variable name.
 */
#define PMDK_DLOG_CASE(DLOG_PRIO)                                                                  \
	case DLOG_PRIO:                                                                            \
		_D_DEBUG(PMDK_LOG_NOCHECK, DLOG_PRIO, "%s\n", message);                            \
		break

	switch (dlog_prio) {
		PMDK_DLOG_CASE(DLOG_EMIT);
		PMDK_DLOG_CASE(DLOG_CRIT);
		PMDK_DLOG_CASE(DLOG_ERR);
		PMDK_DLOG_CASE(DLOG_WARN);
		PMDK_DLOG_CASE(DLOG_NOTE);
		PMDK_DLOG_CASE(DLOG_INFO);
		PMDK_DLOG_CASE(DLOG_DBG);
	default:
		D_ERROR("Not implemented dlog priority: %#x\n", (unsigned)dlog_prio);
		_D_DEBUG(PMDK_LOG_NOCHECK, DLOG_EMIT, "%s\n", message);
	}

#undef PMDK_DLOG_CASE
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
