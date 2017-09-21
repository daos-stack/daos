/* Copyright (C) 2016-2017 Intel Corporation
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
/**
 * This file is part of GURT.
 */
#include <gurt/errno.h>

#define D_DEFINE_GURT_ERRSTR(name, value) #name,

/* Compile time assertion.  Used to ensure ranges are contiguous */
#define D_STATIC_ASSERT(cond, msg)					\
	typedef char static_assertion_##msg[(cond) ? 1 : -1]

#define D_DEFINE_COMP_ERRSTR(name, base)				\
	static const char * const g_##name##_errstr[] = {		\
		D_FOREACH_##name##_ERR(D_DEFINE_GURT_ERRSTR)		\
	};								\
	D_STATIC_ASSERT((sizeof(g_##name##_errstr) /			\
			 sizeof(g_##name##_errstr[0])) ==		\
	      ((DER_ERR_##name##_LIMIT - DER_ERR_##name##_BASE - 1)),	\
		      name##_err_non_contiguous);

D_FOREACH_ERR_RANGE(D_DEFINE_COMP_ERRSTR)

#define D_CHECK_RANGE(name, base)				\
	do {							\
		int first = DER_ERR_##name##_BASE + 1;		\
		if (rc <= DER_ERR_##name##_BASE ||		\
		    rc >= DER_ERR_##name##_LIMIT)		\
			break;					\
		return g_##name##_errstr[rc - first];		\
	} while (0);

#define D_ABS(value) ((value) > 0 ? (value) : (-value))

const char *d_errstr(int rc)
{
	if (rc == 0)
		return "DER_SUCCESS";

	rc = D_ABS(rc);

	D_FOREACH_ERR_RANGE(D_CHECK_RANGE)

	return "DER_UNKNOWN";
}
