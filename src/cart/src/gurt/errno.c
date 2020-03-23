/* Copyright (C) 2016-2019 Intel Corporation
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
#include "gurt/errno.h"
#include "gurt/debug.h"
#include "gurt/list.h"
#include "gurt/common.h"

static D_LIST_HEAD(g_error_reg_list);

struct d_error_reg {
	d_list_t		er_link;
	int			er_base;
	int			er_limit;
	const char * const	*er_strings;
	bool			er_alloc;
};

#define D_DEFINE_COMP_ERRSTR(name, base)				\
	static const char * const g_##name##_errstr[] = {		\
		D_FOREACH_##name##_ERR(D_DEFINE_ERRSTR)			\
	};								\
	D_CASSERT((sizeof(g_##name##_errstr) /				\
			 sizeof(g_##name##_errstr[0])) ==		\
	      ((DER_ERR_##name##_LIMIT - DER_ERR_##name##_BASE - 1)),	\
			#name "is not contiguous");			\
	static struct d_error_reg g_##name##_errreg = {			\
		.er_base	= DER_ERR_##name##_BASE,		\
		.er_limit	= DER_ERR_##name##_LIMIT,		\
		.er_strings	= g_##name##_errstr,			\
		.er_alloc	= false,				\
	};


D_FOREACH_ERR_RANGE(D_DEFINE_COMP_ERRSTR)

#define D_CHECK_RANGE(name, base)				\
	do {							\
		int first = DER_ERR_##name##_BASE + 1;		\
		if (rc <= DER_ERR_##name##_BASE ||		\
		    rc >= DER_ERR_##name##_LIMIT)		\
			break;					\
		return g_##name##_errstr[rc - first];		\
	} while (0);

#define D_ADD_LIST(name, base)					\
		d_list_add_tail(&g_##name##_errreg.er_link, &g_error_reg_list);

#define D_ABS(value) ((value) > 0 ? (value) : (-value))

static __attribute__((constructor)) void
d_err_init(void)
{
	D_FOREACH_ERR_RANGE(D_ADD_LIST)
}

const char *d_errstr(int rc)
{
	struct d_error_reg	*entry;

	if (rc == 0)
		return "DER_SUCCESS";

	rc = D_ABS(rc);

	d_list_for_each_entry(entry, &g_error_reg_list, er_link) {
		if (rc <= entry->er_base || rc >= entry->er_limit)
			continue;
		return entry->er_strings[rc - entry->er_base - 1];
	}

	return "DER_UNKNOWN";
}

int
d_errno_register_range(int start, int end, const char * const *error_strings)
{
	struct	d_error_reg	*entry;

	D_ALLOC_PTR(entry);
	if (entry == NULL) {
		D_ERROR("No memory to register error code range %d - %d\n",
			start, end);
		/* Not fatal.  It just means we get DER_UNKNOWN from d_errstr */
		return -DER_NOMEM;
	}

	entry->er_base = start;
	entry->er_limit = end;
	entry->er_strings = error_strings;
	entry->er_alloc = true;
	d_list_add(&entry->er_link, &g_error_reg_list);

	return 0;
}

void
d_errno_deregister_range(int start)
{
	struct d_error_reg	*entry;

	d_list_for_each_entry(entry, &g_error_reg_list, er_link) {
		if (!entry->er_alloc)
			break;
		if (entry->er_base == start) {
			d_list_del(&entry->er_link);
			D_FREE(entry);
			return;
		}
	}
	D_ERROR("Attempted to deregister non-existent error range from %d\n",
		start);
}
