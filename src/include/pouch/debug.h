/* Copyright (C) 2017 Intel Corporation
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

#ifndef __CRT_DEBUG_H__
#define __CRT_DEBUG_H__

#include <pouch/clog.h>

extern int crt_misc_logfac;
extern int crt_mem_logfac;
extern int crt_rpc_logfac;
extern int crt_bulk_logfac;
extern int crt_corpc_logfac;
extern int crt_grp_logfac;
extern int crt_lm_logfac;
extern int crt_hg_logfac;
extern int crt_pmix_logfac;
extern int crt_self_test_logfac;

#define CD_FAC(name)	(crt_##name##_logfac)

#ifndef C_LOGFAC
#define C_LOGFAC	CD_FAC(misc)
#endif

#define CRT_CRIT	(C_LOGFAC | CLOG_CRIT)
#define CRT_ERR		(C_LOGFAC | CLOG_ERR)
#define CRT_WARN	(C_LOGFAC | CLOG_WARN)
#define CRT_INFO	(C_LOGFAC | CLOG_INFO)
#define CRT_DBG		(C_LOGFAC | CLOG_DBG)

/*
 * Add a new log facility.
 *
 * \param aname [IN]	abbr. name for the facility, for example DSR.
 * \param lname [IN]	long name for the facility, for example CRT_SR.
 *
 * \return		new positive facility number on success, -1 on error.
 */
static inline int
crt_add_log_facility(char *aname, char *lname)
{
	return crt_log_allocfacility(aname, lname);
}

/*
 * C_PRINT_ERR must be used for any error logging before clog is enabled or
 * after it is disabled
 */
#define C_PRINT_ERR(fmt, ...)                                          \
	do {                                                           \
		fprintf(stderr, "%s:%d:%d:%s() " fmt, __FILE__,        \
			getpid(), __LINE__, __func__, ## __VA_ARGS__); \
		fflush(stderr);                                        \
	} while (0)


/*
 * C_DEBUG/C_ERROR etc can-only be used when clog enabled. User can define other
 * similar macros using different subsystem and log-level, for example:
 * #define DSR_DEBUG(...) crt_log(DSR_DEBUG, ...)
 */
#define C_DEBUG(fmt, ...)						\
	crt_log(CRT_DBG, "%s:%d %s() " fmt, __FILE__, __LINE__, __func__, \
		##__VA_ARGS__)

#define C_WARN(fmt, ...)						\
	crt_log(CRT_WARN, "%s:%d %s() " fmt, __FILE__, __LINE__, __func__,\
		##__VA_ARGS__)

#define C_ERROR(fmt, ...)						\
	crt_log(CRT_ERR, "%s:%d %s() " fmt, __FILE__, __LINE__, __func__, \
		##__VA_ARGS__)

#define C_FATAL(fmt, ...)						\
	crt_log(CRT_CRIT, "%s:%d %s() " fmt, __FILE__, __LINE__, __func__,\
		##__VA_ARGS__)

#define C_ASSERT(e)	assert(e)

#define C_ASSERTF(cond, fmt, ...)					\
do {									\
	if (!(cond))							\
		C_FATAL(fmt, ## __VA_ARGS__);				\
	assert(cond);							\
} while (0)

#define C_CASSERT(cond)							\
	do {switch (1) {case (cond): case 0: break; } } while (0)

#define CF_U64		"%" PRIu64
#define CF_X64		"%" PRIx64

#endif /* __CRT_DEBUG_H__ */
