/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DFUSE_LOG_H__
#define __DFUSE_LOG_H__

#include <sys/types.h>
#include <stdio.h>

#include <inttypes.h>

#include <daos/common.h>
#include <gurt/debug.h>

/* Allow changing the default so these macros can be
 * used by files that don't log to the default facility
 */

#define DFUSE_LOG_WARNING(fmt, ...)	\
	D_WARN(fmt "\n", ## __VA_ARGS__)

#define DFUSE_LOG_ERROR(fmt, ...)		\
	D_ERROR(fmt "\n", ## __VA_ARGS__)

#define DFUSE_LOG_DEBUG(fmt, ...)	\
	D_DEBUG(DB_ANY, fmt "\n", ## __VA_ARGS__)

#define DFUSE_LOG_INFO(fmt, ...)		\
	D_INFO(fmt "\n", ## __VA_ARGS__)

/* A couple of helper functions so we can append '\n'
 * without changing all of instances of the macros
 */
#define DFUSE_TRA_HELPER(func, ptr, fmt, ...) \
	func(ptr, fmt "\n", ## __VA_ARGS__)

#define DFUSE_TRA_DEBUG_HELPER(func, ptr, fmt, ...) \
	D_TRACE_DEBUG(DB_ANY, ptr, fmt "\n", ## __VA_ARGS__)

/* DFUSE_TRACE marcos defined for tracing descriptors and RPCs
 * in the logs. UP() is used to register a new descriptor -
 * this includes giving it a "type" and also a parent to build
 * a descriptor hierarchy. Then DOWN() will de-register
 * the descriptor.
 *
 * For RPCs only, LINK() is used to link an RPC to a
 * descriptor in the hierarchy. RPCs are not registered
 * (warning if UP and LINK are both called for the same pointer).
 *
 * All other logging remains the same for WARNING/ERROR/
 * DEBUG/INFO, however just takes an extra argument for the
 * lowest-level descriptor to tie the logging message to.
 */
#define DFUSE_TRA_WARNING(ptr, ...)			\
	DFUSE_TRA_HELPER(D_TRACE_WARN, ptr, "" __VA_ARGS__)

#define DFUSE_TRA_ERROR(ptr, ...)			\
	DFUSE_TRA_HELPER(D_TRACE_ERROR, ptr, "" __VA_ARGS__)

#define DFUSE_TRA_DEBUG(ptr, ...)			\
	DFUSE_TRA_DEBUG_HELPER(DB_ANY, ptr, "" __VA_ARGS__)

#define DFUSE_TRA_INFO(ptr, ...)			\
	DFUSE_TRA_HELPER(D_TRACE_INFO, ptr, "" __VA_ARGS__)

/* Register a descriptor with a parent and a type */
#define DFUSE_TRA_UP(ptr, parent, type)				\
	D_TRACE_UP(DB_ANY, ptr, parent, type)

/* De-register a descriptor, including all aliases */
#define DFUSE_TRA_DOWN(ptr)					\
	D_TRACE_DOWN(DB_ANY, ptr)

/* Register as root of hierarchy, used in place of DFUSE_TRA_UP */
#define DFUSE_TRA_ROOT(ptr, type)				\
	D_TRACE_ROOT(DB_ANY, ptr, type)

#endif /* __DFUSE_LOG_H__ */
