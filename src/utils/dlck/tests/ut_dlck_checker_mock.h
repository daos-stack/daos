/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __UT_DLCK_CHECKER_MOCK_H__
#define __UT_DLCK_CHECKER_MOCK_H__

#include <uuid/uuid.h>

#include "../dlck_checker.h"

/* Static payload instances returned by the checker allocation mock. */
extern struct dlck_checker_main   Dcm;
extern struct dlck_checker_worker Dcw;
extern char                       Mock_log_file[];
extern uuid_t                     Mock_pool_uuid;
extern FILE *const                Mock_file_stream;

#endif /* __UT_DLCK_CHECKER_MOCK_H__ */
