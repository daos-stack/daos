/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2014-2021, Intel Corporation */

/*
 * out.h -- definitions for "out" module
 */

#ifndef PMDK_OUT_H
#define PMDK_OUT_H 1

#include <daos/debug.h>

#define DAV_LOG_FAC DB_TRACE

#ifndef EVALUATE_DBG_EXPRESSIONS
#if defined(DEBUG) || defined(__clang_analyzer__) || defined(__COVERITY__) ||\
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
#define DAV_DEBUG(fmt, ...) do {			\
	if (!EVALUATE_DBG_EXPRESSIONS)			\
		break;					\
	D_DEBUG(DAV_LOG_FAC, fmt "\n", ## __VA_ARGS__);	\
} while (0)

/* produce output and exit */
#define FATAL(fmt, ...)					\
	D_FATAL(fmt "\n", ## __VA_ARGS__)

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
	D_ERROR(fmt, ## __VA_ARGS__)

#endif
