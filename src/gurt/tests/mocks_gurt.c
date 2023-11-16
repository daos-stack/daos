/*
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "mocks_gurt.h"

void
mock_getenv_setup(void)
{
	getenv_return = NULL;
}

char *getenv_return; /* value to be returned */
char *
__wrap_getenv(const char *name)
{
	return getenv_return;
}
