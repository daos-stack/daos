/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_MOCKS_GURT_H__
#define __DAOS_MOCKS_GURT_H__

#include <stdlib.h>

void mock_getenv_setup(void);
extern char *getenv_return; /* value to be returned */

#endif /* __DAOS_MOCKS_GURT_H__ */
