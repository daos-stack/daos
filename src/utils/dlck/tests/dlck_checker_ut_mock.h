/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DLCK_CHECKER_UT_MOCK_H__
#define __DLCK_CHECKER_UT_MOCK_H__

#include <stddef.h>
#include <abt.h>
#include <stdio.h>

#include "../dlck_checker.h"

/* Static payload instances returned by the checker allocation mock. */
extern struct dlck_checker_main Dcm;
extern struct dlck_checker_worker Dcw;
/* Sentinel handle installed and checked by the ABT mutex mocks. */
extern const ABT_mutex Mock_mutex_handle;
/* Arms the next d_calloc call to return the payload queued by expect_checker_d_calloc(). */
extern int mock_d_calloc_enabled;
/* Makes the next vfprintf call consume its queued mock result. */
extern int mock_vfprintf_enabled;
/* Checks format and rendered arguments during the next mocked vfprintf call. */
extern int mock_vfprintf_check_args;
/* Makes the next fflush call consume its queued mock result. */
extern int mock_fflush_enabled;
/* Makes the next fopen call return the fake stream instead of opening a file. */
extern int mock_fopen_fake_stream_enable;

/* Queue expectations for a checker payload allocation; NULL simulates failure. */
void expect_checker_d_calloc(size_t size, void *payload);

#endif /* __DLCK_CHECKER_UT_MOCK_H__ */
