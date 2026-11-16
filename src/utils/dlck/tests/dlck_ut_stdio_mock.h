/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DLCK_UT_STDIO_MOCK_H__
#define __DLCK_UT_STDIO_MOCK_H__

#include <stdbool.h>
#include <stdio.h>

extern FILE *const Mock_file_stream;
extern bool        mock_vfprintf_check_output;

#endif /* __DLCK_UT_STDIO_MOCK_H__ */
