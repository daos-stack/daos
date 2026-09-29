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
#include <uuid/uuid.h>

#include "../dlck_checker.h"

/* Static payload instances returned by the checker allocation mock. */
extern struct dlck_checker_main   Dcm;
extern struct dlck_checker_worker Dcw;
/* Sentinel handle installed and checked by the ABT mutex mocks. */
extern const ABT_mutex            Mock_mutex_handle;
extern FILE *const                Mock_file_stream;
extern char                       Mock_log_file[];
extern uuid_t                     Mock_pool_uuid;
/* Checks rendered output during the next mocked vfprintf call. */
extern int                        mock_vfprintf_check_output;

/* Queue expectations for a checker payload allocation; NULL simulates failure. */
void
expect_checker_d_calloc(size_t size, void *payload);
#define EXPECT_CHECKER_D_CALLOC(payload) expect_checker_d_calloc(sizeof(payload), &(payload))

#endif /* __DLCK_CHECKER_UT_MOCK_H__ */
