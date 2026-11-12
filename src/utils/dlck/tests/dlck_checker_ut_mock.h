/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DLCK_CHECKER_UT_MOCK_H__
#define __DLCK_CHECKER_UT_MOCK_H__

#include <stddef.h>
#include <uuid/uuid.h>

#include <abt.h>

#include "../dlck_checker.h"
#include "dlck_ut_abt_mock.h"
#include "dlck_ut_mem_mock.h"
#include "dlck_ut_stdio_mock.h"

/* Static payload instances returned by the checker allocation mock. */
extern struct dlck_checker_main   Dcm;
extern struct dlck_checker_worker Dcw;
extern char                       Mock_log_file[];
extern uuid_t                     Mock_pool_uuid;

#endif /* __DLCK_CHECKER_UT_MOCK_H__ */
