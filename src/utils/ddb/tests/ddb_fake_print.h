/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_FAKE_PRINT_H
#define DAOS_DDB_FAKE_PRINT_H

/**
 * Shared struct ddb_ctx print/error callback: appends formatted output to fake_print_buf
 * instead of stdout/stderr, so tests run hermetically and can assert on what would have been
 * printed. Reset fake_print_buf with fake_print_reset() before exercising the code under test.
 */
extern char fake_print_buf[];
int
fake_print(const char *fmt, ...);
void
fake_print_reset(void);

#endif /* DAOS_DDB_FAKE_PRINT_H */
