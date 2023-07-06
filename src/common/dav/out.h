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

#define TEST_ALWAYS_TRUE_EXPR(cnd) do {	\
	if (__builtin_constant_p(cnd))	\
		COMPILE_ERROR_ON(cnd);	\
} while (0)
#define TEST_ALWAYS_EQ_EXPR(lhs, rhs) do {				\
	if (__builtin_constant_p(lhs) && __builtin_constant_p(rhs))	\
		COMPILE_ERROR_ON((lhs) == (rhs));			\
} while (0)
#define TEST_ALWAYS_NE_EXPR(lhs, rhs) do {				\
	if (__builtin_constant_p(lhs) && __builtin_constant_p(rhs))	\
		COMPILE_ERROR_ON((lhs) != (rhs));			\
} while (0)

/* produce debug/trace output */
#if defined(DAV_EXTRA_DEBUG)
#define DAV_DBG(fmt, ...)
	D_DEBUG(DAV_LOG_FAC, fmt "\n", ## __VA_ARGS__)
#else
#define DAV_DBG(fmt, ...) SUPPRESS_UNUSED(__VA_ARGS__)
#endif

/* produce output and exit */
#define FATAL(fmt, ...)					\
	D_ASSERTF(0, fmt "\n", ## __VA_ARGS__)

/* assert a condition is true at runtime */
#define ASSERT_rt(cnd) do {				\
	if (!EVALUATE_DBG_EXPRESSIONS || (cnd))		\
		break;					\
	D_ASSERT(cnd);					\
} while (0)

/* assert two integer values are equal at runtime */
#define ASSERTeq_rt(lhs, rhs) do {			\
	if (!EVALUATE_DBG_EXPRESSIONS || ((lhs) == (rhs)))\
		break; \
	D_ASSERTF(((lhs) == (rhs)),			\
	"assertion failure: %s (0x%llx) == %s (0x%llx)", #lhs,\
	(unsigned long long)(lhs), #rhs, (unsigned long long)(rhs)); \
} while (0)

/* assert two integer values are not equal at runtime */
#define ASSERTne_rt(lhs, rhs) do {			\
	if (!EVALUATE_DBG_EXPRESSIONS || ((lhs) != (rhs)))\
		break;					\
	D_ASSERTF(((lhs) != (rhs)),			\
	"assertion failure: %s (0x%llx) != %s (0x%llx)", #lhs,\
	(unsigned long long)(lhs), #rhs, (unsigned long long)(rhs)); \
} while (0)

/*
 * Detect useless asserts on always true expression. Please use
 * COMPILE_ERROR_ON(!cnd) or ASSERT_rt(cnd) in such cases.
 */
/* assert a condition is true */
#define ASSERT(cnd) do {\
		TEST_ALWAYS_TRUE_EXPR(cnd);\
		ASSERT_rt(cnd);\
	} while (0)

/* assert two integer values are equal */
#define ASSERTeq(lhs, rhs) do {\
		/* See comment in ASSERT. */\
		TEST_ALWAYS_EQ_EXPR(lhs, rhs);\
		ASSERTeq_rt(lhs, rhs);\
	} while (0)

/* assert two integer values are not equal */
#define ASSERTne(lhs, rhs) do {\
		/* See comment in ASSERT. */\
		TEST_ALWAYS_NE_EXPR(lhs, rhs);\
		ASSERTne_rt(lhs, rhs);\
	} while (0)

#define ERR(fmt, ...)\
	D_ERROR(fmt "\n", ## __VA_ARGS__)

#endif /* __DAOS_COMMON_OUT_H */
