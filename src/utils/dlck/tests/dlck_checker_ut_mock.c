/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include "dlck_checker_ut_mock.h"

/* Static payload instances returned by the checker allocation mock. */
struct dlck_checker_main   Dcm;
struct dlck_checker_worker Dcw;
char                       Mock_log_file[] = "mock-vos-log";
uuid_t                     Mock_pool_uuid  = {0x66, 0x88, 0xdb, 0xaa, 0x95, 0x42, 0x46, 0x2d,
					      0xaa, 0x6d, 0xd7, 0x8b, 0xcd, 0x28, 0xd5, 0x59};
