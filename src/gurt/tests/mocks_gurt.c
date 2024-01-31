/*
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdbool.h>

#include "mocks_gurt.h"

static bool mock_getenv = false;
char       *getenv_return; /* value to be returned */

void
mock_getenv_setup(void)
{
	mock_getenv = true;
}

void
mock_getenv_teardown(void)
{
	mock_getenv = false;
}

char *
__real_getenv(const char *name);

char *
__wrap_getenv(const char *name)
{
	if (mock_getenv)
		return getenv_return;

	return __real_getenv(name);
}

static bool mock_strdup = false;

void
mock_strdup_setup(void)
{
	mock_strdup = true;
}

void
mock_strdup_teardown(void)
{
	mock_strdup = false;
}

char *
__real_strdup(const char *name);

char *
__wrap_strdup(const char *name)
{
	if (mock_strdup)
		return NULL;

	return __real_strdup(name);
}
