/*
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_MOCKS_GURT_H__
#define __DAOS_MOCKS_GURT_H__

#include <stdlib.h>

extern char *getenv_return; /* value to be returned */

void
mock_getenv_setup(void);
void
mock_getenv_teardown(void);

void
mock_strdup_setup(void);
void
mock_strdup_teardown(void);

#endif /* __DAOS_MOCKS_GURT_H__ */
