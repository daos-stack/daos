/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DLCK_UT_STDIO_MOCK_H__
#define __DLCK_UT_STDIO_MOCK_H__

#include <stdbool.h>
#include <stdio.h>

/*
 * Shared stdio and gurt I/O mocks. Link dlck_ut_stdio_mock.c and enable the wraps
 * (-Wl,--wrap=vfprintf, -Wl,--wrap=__vfprintf_chk, -Wl,--wrap=fflush, -Wl,--wrap=fopen,
 * -Wl,--wrap=fclose, -Wl,--wrap=d_asprintf2, -Wl,--wrap=d_log_sync) for the calls controlled by
 * the test binary.
 */

/** Sentinel stream installed and checked by the stdio mocks; never passed to real stdio. */
extern FILE *const Mock_file_stream;

/** Checks rendered output during the next mocked vfprintf call. */
extern bool        mock_vfprintf_check_output;

#endif /* __DLCK_UT_STDIO_MOCK_H__ */
