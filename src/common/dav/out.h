/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2014-2021, Intel Corporation */

/*
 * out.h -- definitions for "out" module
 */

#ifndef __DAOS_COMMON_OUT_H
#define __DAOS_COMMON_OUT_H 1

#include <daos/debug.h>
#include "util.h"

#define DAV_LOG_FAC DB_TRACE

/* enable extra debug messages and extra checks */
/*#define DAV_EXTRA_DEBUG*/

#ifndef EVALUATE_DBG_EXPRESSIONS
#if defined(DAV_EXTRA_DEBUG) || defined(__clang_analyzer__) || defined(__COVERITY__) ||\
	defined(__KLOCWORK__)
#define EVALUATE_DBG_EXPRESSIONS 1
#else
#define EVALUATE_DBG_EXPRESSIONS 0
#endif
#endif

/* produce debug/trace output */
#if defined(DAV_EXTRA_DEBUG)
#define DAV_DBG(fmt, ...) D_DEBUG(DAV_LOG_FAC, fmt "\n", ##__VA_ARGS__)
#else
#define DAV_DBG(fmt, ...) SUPPRESS_UNUSED(__VA_ARGS__)
#endif

/* produce output and exit */
#define FATAL(fmt, ...)					\
	D_ASSERTF(0, fmt "\n", ## __VA_ARGS__)

/* assert a condition is true at runtime */
#define ASSERT(cnd)                                                                                \
	do {                                                                                       \
		if (!EVALUATE_DBG_EXPRESSIONS)                                                     \
			break;                                                                     \
		D_ASSERT(cnd);                                                                     \
	} while (0)

/* assert two integer values are equal at runtime */
#define ASSERTeq(lhs, rhs)                                                                         \
	do {                                                                                       \
		if (!EVALUATE_DBG_EXPRESSIONS)                                                     \
			break;                                                                     \
		D_ASSERTF(((lhs) == (rhs)), "assertion failure: %s (0x%llx) == %s (0x%llx)", #lhs, \
			  (unsigned long long)(lhs), #rhs, (unsigned long long)(rhs));             \
	} while (0)

/* assert two integer values are not equal at runtime */
#define ASSERTne(lhs, rhs)                                                                         \
	do {                                                                                       \
		if (!EVALUATE_DBG_EXPRESSIONS)                                                     \
			break;                                                                     \
		D_ASSERTF(((lhs) != (rhs)), "assertion failure: %s (0x%llx) != %s (0x%llx)", #lhs, \
			  (unsigned long long)(lhs), #rhs, (unsigned long long)(rhs));             \
	} while (0)

#define ERR(fmt, ...)\
	D_ERROR(fmt "\n", ## __VA_ARGS__)

#endif /* __DAOS_COMMON_OUT_H */
