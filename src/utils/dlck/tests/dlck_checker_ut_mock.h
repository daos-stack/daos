/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DLCK_CHECKER_UT_MOCK_H__
#define __DLCK_CHECKER_UT_MOCK_H__

#include <abt.h>
#include <stdio.h>

#include "../dlck_checker.h"

extern struct dlck_checker_main Dcm;
extern struct dlck_checker_worker Dcw;
extern const ABT_mutex Mock_mutex_handle;
extern void *last_freed_payload;
extern int mock_vfprintf_enabled;
extern int mock_fflush_enabled;

#endif /* __DLCK_CHECKER_UT_MOCK_H__ */
