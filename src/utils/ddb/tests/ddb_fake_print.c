/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ddb_fake_print.h"

/*
 * Not ddb_test_driver.c's dvt_fake_print(): that file has its own main() (ddb_tests' entry
 * point), which would conflict with ddb_ut.c's, and calls real vos_pool_create() -- too heavy
 * for ddb_ut's hermetic unit tests.
 */
char fake_print_buf[4096];

int
fake_print(const char *fmt, ...)
{
	va_list ap;
	size_t  offset = strlen(fake_print_buf);
	size_t  left   = sizeof(fake_print_buf) - offset;

	va_start(ap, fmt);
	vsnprintf(fake_print_buf + offset, left, fmt, ap);
	va_end(ap);

	return 0;
}

void
fake_print_reset(void)
{
	memset(fake_print_buf, 0, sizeof(fake_print_buf));
}
