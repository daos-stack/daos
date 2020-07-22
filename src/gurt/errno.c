/*
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/**
 * This file is part of GURT.
 */
#include <daos_errno.h>
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
