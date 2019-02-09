/* Copyright (C) 2017-2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __GURT_DEBUG_H__
#define __GURT_DEBUG_H__

#include <stdio.h>
#include <stddef.h>
#include <assert.h>

#include <gurt/debug_setup.h>
#include <gurt/errno.h>

/**
 * \file
 * Debug macros and functions
 */

/** @addtogroup GURT_DEBUG
 * @{
 */

#define D_FOREACH_GURT_FAC(ACTION, arg)                                 \
	ACTION(misc, misc, arg)  /* misc debug messages */              \
	ACTION(mem,  mem,  arg)  /* memory debug messages */            \
	ACTION(swim, swim, arg)  /* swim debug messages (move ?) */     \
	ACTION(fi,   fi,   arg)  /* fault injection debug messages */

/**
 * d_alt_assert is a pointer to an alternative assert function, meaning an
 * alternative to the C library assert(). It is declared in gurt/debug.c. See
 * the example in D_ASSERT for how this is called from a macro.
 *
 * \param[in] result		The expression to assert
 * \param[in] expression	The expression as string
 * \param[in] file		The file which calls the alternative assert
 * \param[in] line		The line which calls the alternative assert
 */
extern void (*d_alt_assert)(const int, const char*, const char*, const int);

#define DB_ALL_BITS	"all"

#define D_LOG_FILE_ENV	"D_LOG_FILE"	/**< Env to specify log file */
#define D_LOG_MASK_ENV	"D_LOG_MASK"	/**< Env to specify log mask */

/* Enable shadow warning where users use same variable name in nested
 * scope.   This enables use of a variable in the macro below and is
 * just good coding practice.
 */
#pragma GCC diagnostic warning "-Wshadow"

/** Internal macro for printing the message using resolved mask */
#define _D_LOG_NOCHECK(mask, fmt, ...)				\
	d_log(mask, "%s:%d %s() " fmt, __FILE__, __LINE__,	\
	      __func__, ##__VA_ARGS__)

/** Internal macro for printing trace message with resolved mask */
#define _D_TRACE_NOCHECK(mask, ptr, fmt, ...)			\
	d_log(mask, "%s:%d %s(%p) " fmt, __FILE__, __LINE__,	\
	      __func__, ptr, ##__VA_ARGS__)

/** Internal macro for saving the log, checking it, and printing, if enabled */
#define _D_LOG_CHECK(func, saved_mask, mask, ...)			\
	do {								\
		(saved_mask) = d_log_check(mask);			\
									\
		if (saved_mask)						\
			func(saved_mask, ##__VA_ARGS__);		\
	} while (0)

/**
 * The _D_LOG internal macro checks the specified mask and, if enabled, it
 * logs the message, prependng the file, line, and function name.  This
 * function can be used directly by users or by user defined macros if the
 * provided log level macros are not flexible enough.
 */
#define _D_LOG(func, mask, ...)						\
	do {								\
		int __tmp_mask;						\
									\
		_D_LOG_CHECK(func, __tmp_mask, mask, ##__VA_ARGS__);	\
	} while (0)

/* Log a message conditionally upon resolving the mask
 *
 * The mask is combined with D_LOGFAC which the user should define before
 * including debug headers
 *
 * \param mask	The debug bits or priority mask
 * \param fmt	The format string to print
 *
 *  User should define D_LOGFAC for the file
 */
#define D_DEBUG_V1(mask, fmt, ...)	\
	_D_LOG(_D_LOG_NOCHECK, (mask) | D_LOGFAC, fmt, ## __VA_ARGS__)

/* Log a pointer value and message conditionally upon resolving the mask
 *
 * The mask is combined with D_LOGFAC which the user should define before
 * including debug headers
 *
 * \param mask	The debug bits or priority mask
 * \param ptr	A pointer value that is put into the message
 * \param fmt	The format string to print
 *
 *  User should define D_LOGFAC for the file
 */
#define D_TRACE_DEBUG_V1(mask, ptr, fmt, ...) \
	_D_LOG(_D_TRACE_NOCHECK, (mask) | D_LOGFAC, ptr, fmt, ## __VA_ARGS__)

#ifdef D_LOG_USE_V2
#define D_DEBUG D_DEBUG_V2
#define D_TRACE_DEBUG D_TRACE_DEBUG_V2

#define _D_DEBUG_V2(func, flag, ...)					   \
	do {								   \
		if (__builtin_expect(DD_FLAG(flag, D_LOGFAC), 0)) {	   \
			if (DD_FLAG(flag, D_LOGFAC) == (int)DLOG_UNINIT) { \
				_D_LOG_CHECK(func,			   \
					     DD_FLAG(flag, D_LOGFAC),	   \
					     (flag) | D_LOGFAC,		   \
					     ##__VA_ARGS__);		   \
				break;					   \
			}						   \
			func(DD_FLAG(flag, D_LOGFAC), ##__VA_ARGS__);	   \
		}							   \
	} while (0)
/**
 * New version of D_DEBUG which utilizes the facility cache to optimize
 * both negative and postive debug lookups
 */
#define D_DEBUG_V2(flag, fmt, ...)				\
	_D_DEBUG_V2(_D_LOG_NOCHECK, flag, fmt, ##__VA_ARGS__)

/**
 * New version of D_DEBUG which utilizes the facility cache to optimize
 * both negative and postive debug lookups
 */
#define D_TRACE_DEBUG_V2(flag, ptr, fmt, ...)				\
	_D_DEBUG_V2(_D_TRACE_NOCHECK, flag, ptr, fmt, ##__VA_ARGS__)

#else /* !D_LOG_USE_V2 */
#define D_DEBUG D_DEBUG_V1
#define D_TRACE_DEBUG D_TRACE_DEBUG_V1
#ifdef D_LOG_V1_TEST
/** Define the new macro to a sane value so tests still work.  Otherwise, leave
 * it undefined as users shouldn't be using it without defining D_LOG_V2
 */
#define D_DEBUG_V2 D_DEBUG_V1
#define D_TRACE_DEBUG_V2 D_TRACE_DEBUG_V1
#endif /* D_LOG_TEST_V1 */
#endif /* D_LOG_USE_V2 */

/** Special conditional debug so we can pass different flags to base routine
 *  based on a condition.   With V2, things like cond ? flag1 : flag2 don't
 *  work natively.
 */
#define D_CDEBUG(cond, flag_true, flag_false, ...)		\
	do {							\
		if (cond)					\
			D_DEBUG(flag_true, __VA_ARGS__);	\
		else						\
			D_DEBUG(flag_false, __VA_ARGS__);	\
	} while (0)

/** Helper macros to conditionally output logs conditionally based on
 *  the message priority and the current log level.  See D_DEBUG and
 *  D_TRACE_DEBUG
 */
#define D_INFO(fmt, ...)	D_DEBUG(DLOG_INFO, fmt, ## __VA_ARGS__)
#define D_NOTE(fmt, ...)	D_DEBUG(DLOG_NOTE, fmt, ## __VA_ARGS__)
#define D_WARN(fmt, ...)	D_DEBUG(DLOG_WARN, fmt, ## __VA_ARGS__)
#define D_ERROR(fmt, ...)	D_DEBUG(DLOG_ERR, fmt, ## __VA_ARGS__)
#define D_CRIT(fmt, ...)	D_DEBUG(DLOG_CRIT, fmt, ## __VA_ARGS__)
#define D_FATAL(fmt, ...)	D_DEBUG(DLOG_EMERG, fmt, ## __VA_ARGS__)

#define D_TRACE_INFO(ptr, fmt, ...)	\
	D_TRACE_DEBUG(DLOG_INFO, ptr, fmt, ## __VA_ARGS__)
#define D_TRACE_NOTE(ptr, fmt, ...)	\
	D_TRACE_DEBUG(DLOG_NOTE, ptr, fmt, ## __VA_ARGS__)
#define D_TRACE_WARN(ptr, fmt, ...)	\
	D_TRACE_DEBUG(DLOG_WARN, ptr, fmt, ## __VA_ARGS__)
#define D_TRACE_ERROR(ptr, fmt, ...)	\
	D_TRACE_DEBUG(DLOG_ERR, ptr, fmt, ## __VA_ARGS__)
#define D_TRACE_CRIT(ptr, fmt, ...)	\
	D_TRACE_DEBUG(DLOG_CRIT, ptr, fmt, ## __VA_ARGS__)
#define D_TRACE_FATAL(ptr, fmt, ...)	\
	D_TRACE_DEBUG(DLOG_EMERG, ptr, fmt, ## __VA_ARGS__)

D_FOREACH_GURT_FAC(D_LOG_DECLARE_FAC, D_NOOP)

D_FOREACH_GURT_DB(D_LOG_DECLARE_DB, D_NOOP)

/**
 * D_PRINT_ERR must be used for any error logging before clog is enabled or
 * after it is disabled
 */
#define D_PRINT_ERR(fmt, ...)					\
	do {							\
		fprintf(stderr, "%s:%d:%s() " fmt, __FILE__,	\
			__LINE__, __func__, ## __VA_ARGS__);	\
		fflush(stderr);					\
	} while (0)

/**
 * Add a new log facility.
 *
 * \param[in] aname	abbr. name for the facility, for example DSR.
 * \param[in] lname	long name for the facility, for example DSR.
 *
 * \return		new positive facility number on success, -1 on error.
 */
static inline int
d_add_log_facility(const char *aname, const char *lname)
{
	return d_log_allocfacility(aname, lname);
}

/**
 * Add a new log facility.
 *
 * \param[out fac	facility number to be returned
 * \param[in] aname	abbr. name for the facility, for example DSR.
 * \param[in] lname	long name for the facility, for example DSR.
 *
 * \return		0 on success, -1 on error.
 */
static inline int
d_init_log_facility(int *fac, const char *aname, const char *lname)
{
	assert(fac != NULL);

	*fac = d_add_log_facility(aname, lname);
	if (*fac < 0) {
		D_PRINT_ERR("d_add_log_facility failed, *fac: %d\n", *fac);
		return -DER_UNINIT;
	}

	return DER_SUCCESS;
}

/**
 * Get allocated debug bit for the given debug bit name.
 *
 * \param[in] bitname	short name for given bit
 * \param[out] dbgbit	allocated bit mask
 *
 * \return		0 on success, -1 on error
 */
int d_log_getdbgbit(d_dbug_t *dbgbit, char *bitname);

/**
 * Set an alternative assert function. This is useful in unit testing when you
 * may want to replace assert() with cmocka's mock_assert() so that you can
 * test if a function throws an assertion with cmocka's expect_assert_failure().
 *
 * \param[in] *alt_assert	Function pointer to the alternative assert
 *
 * \return			0 on success, -DER_INVAL on error
 */
int d_register_alt_assert(void (*alt_assert)(const int, const char*,
			  const char*, const int));

/**
 * D_PRINT can be used for output to stdout with or without clog being enabled
 */
#define D_PRINT(fmt, ...)						\
	do {								\
		fprintf(stdout, fmt, ## __VA_ARGS__);			\
		fflush(stdout);						\
	} while (0)

#define D_ASSERT(e)							\
	do {								\
		if (d_alt_assert != NULL)				\
			d_alt_assert((int64_t)(e), #e, __FILE__, __LINE__);\
		assert(e);						\
	} while (0)

#define D_ASSERTF(cond, fmt, ...)					\
do {									\
	if (!(cond))							\
		D_FATAL(fmt, ## __VA_ARGS__);				\
	assert(cond);							\
} while (0)


#define D_CASSERT(cond, ...)						\
	_Static_assert(cond, #cond ": " __VA_ARGS__)

#define DF_U64		"%" PRIu64
#define DF_X64		"%" PRIx64

/** @}
 */
#endif /* __GURT_DEBUG_H__ */
