/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(server)

#include <abt.h>

const char *
dss_abterr2str(int rc)
{
	static const char *err_str[] = {"ABT_SUCCESS",
					"ABT_ERR_UNINITIALIZED",
					"ABT_ERR_MEM",
					"ABT_ERR_OTHER",
					"ABT_ERR_INV_XSTREAM",
					"ABT_ERR_INV_XSTREAM_RANK",
					"ABT_ERR_INV_XSTREAM_BARRIER",
					"ABT_ERR_INV_SCHED",
					"ABT_ERR_INV_SCHED_KIND",
					"ABT_ERR_INV_SCHED_PREDEF",
					"ABT_ERR_INV_SCHED_TYPE",
					"ABT_ERR_INV_SCHED_CONFIG",
					"ABT_ERR_INV_POOL",
					"ABT_ERR_INV_POOL_KIND",
					"ABT_ERR_INV_POOL_ACCESS",
					"ABT_ERR_INV_UNIT",
					"ABT_ERR_INV_THREAD",
					"ABT_ERR_INV_THREAD_ATTR",
					"ABT_ERR_INV_TASK",
					"ABT_ERR_INV_KEY",
					"ABT_ERR_INV_MUTEX",
					"ABT_ERR_INV_MUTEX_ATTR",
					"ABT_ERR_INV_COND",
					"ABT_ERR_INV_RWLOCK",
					"ABT_ERR_INV_EVENTUAL",
					"ABT_ERR_INV_FUTURE",
					"ABT_ERR_INV_BARRIER",
					"ABT_ERR_INV_TIMER",
					"ABT_ERR_INV_QUERY_KIND",
					"ABT_ERR_XSTREAM",
					"ABT_ERR_XSTREAM_STATE",
					"ABT_ERR_XSTREAM_BARRIER",
					"ABT_ERR_SCHED",
					"ABT_ERR_SCHED_CONFIG",
					"ABT_ERR_POOL",
					"ABT_ERR_UNIT",
					"ABT_ERR_THREAD",
					"ABT_ERR_TASK",
					"ABT_ERR_KEY",
					"ABT_ERR_MUTEX",
					"ABT_ERR_MUTEX_LOCKED",
					"ABT_ERR_COND",
					"ABT_ERR_COND_TIMEDOUT",
					"ABT_ERR_RWLOCK",
					"ABT_ERR_EVENTUAL",
					"ABT_ERR_FUTURE",
					"ABT_ERR_BARRIER",
					"ABT_ERR_TIMER",
					"ABT_ERR_MIGRATION_TARGET",
					"ABT_ERR_MIGRATION_NA",
					"ABT_ERR_MISSING_JOIN",
					"ABT_ERR_FEATURE_NA",
					"ABT_ERR_INV_TOOL_CONTEXT",
					"ABT_ERR_INV_ARG",
					"ABT_ERR_SYS",
					"ABT_ERR_CPUID",
					"ABT_ERR_INV_POOL_CONFIG",
					"ABT_ERR_INV_POOL_USER_DEF"};

	if (rc < 0 || rc >= sizeof(err_str) / sizeof(err_str[0]))
		rc = ABT_ERR_OTHER;

	return err_str[rc];
}

const char *
dss_abterr2desc(int rc)
{
	static const char *err_desc[] = {"The routine returns successfully",
					 "Argobots it not initialized",
					 "Memory allocation failure",
					 "Other error",
					 "Invalid execution stream",
					 "Invalid execution stream rank",
					 "Invalid execution stream barrier",
					 "Invalid scheduler",
					 "Invalid scheduler kind",
					 "Invalid predefined scheduler type",
					 "Deprecated error code",
					 "Invalid scheduler configuration",
					 "Invalid pool",
					 "Invalid pool kind",
					 "Invalid pool access type",
					 "Invalid work unit for scheduling",
					 "Invalid work unit",
					 "Invalid ULT attribute",
					 "Invalid work unit",
					 "Invalid work-unit-specific data key",
					 "Invalid mutex",
					 "Invalid mutex attribute",
					 "Invalid condition variable",
					 "Invalid readers-writer lock",
					 "Invalid eventual",
					 "Invalid future",
					 "Invalid barrier",
					 "Invalid timer",
					 "Invalid query kind",
					 "Error related to an execution stream",
					 "Error related to an execution stream state",
					 "Error related to an execution stream",
					 "Error related to a scheduler",
					 "Error related to a scheduler configuration",
					 "Error related to a pool",
					 "Error related to a work unit for scheduling",
					 "Error related to a work unit",
					 "Error related to a work unit",
					 "Error related to a work-unit-specific data key",
					 "Error related to a mutex",
					 "A return value when a mutex is locked",
					 "Error related to a condition variable",
					 "A return value when a condition variable is timed out",
					 "Error related to a readers-writer lock",
					 "Error related to an eventual",
					 "Error related to a future",
					 "Error related to a barrier",
					 "Error related to a timer",
					 "Error related to a migration target",
					 "Migration is not supported",
					 "Deprecated error code",
					 "Unsupported feature",
					 "Invalid tool context",
					 "Invalid user argument",
					 "Error related to system calls and standard libraries",
					 "Error related to CPU ID",
					 "Invalid pool configuration",
					 "Invalid pool definition"};

	if (rc < 0 || rc >= sizeof(err_desc) / sizeof(err_desc[0]))
		rc = ABT_ERR_OTHER;

	return err_desc[rc];
}
