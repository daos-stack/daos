/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include <cmocka.h>

#include "dlck_ut_mem_mock.h"

void *
__wrap_d_calloc(size_t nmemb, size_t size)
{
	check_expected(nmemb);
	check_expected(size);

	void *ptr = mock_ptr_type(void *);

	if (ptr != NULL) {
		memset(ptr, 0, nmemb * size);
	}

	return ptr;
}

void
__wrap_d_free(void *ptr)
{
	check_expected_ptr(ptr);
}
