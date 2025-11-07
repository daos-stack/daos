/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(dlck)

#include <abt.h>
#include <stdbool.h>

#include <daos_errno.h>
#include <daos_srv/daos_engine.h>

#include "dlck_checker.h"
#include "dlck_report.h"

#define DLCK_PROGRESS_LINE_LEN 32

/**
 * Produce and provide a simple separator:
 *
 * ========
 */
static inline char *
get_separator()
{
	static char separator[DLCK_PROGRESS_LINE_LEN] = {0};
	static bool initialized                       = false;

	if (unlikely(!initialized)) {
		memset(separator, '=', DLCK_PROGRESS_LINE_LEN);
		initialized = true;
	}

	return separator;
}

#define DLCK_PRINT_SEPARATOR(ck) CK_PRINTF(ck, "%s\n", get_separator())

/**
 * \note This function is called when no other threads are running in parallel. No locks are
 * necessary.
 */
void
dlck_report_results(int *rcs, unsigned targets, unsigned warnings_num, struct checker *ck)
{
	/** print header */
	DLCK_PRINT_SEPARATOR(ck);
	CK_PRINT(ck, "Targets:\n");
	DLCK_PRINT_SEPARATOR(ck);

	/** print records */
	for (int i = 0; i < targets; ++i) {
		CK_PRINTFL_RC(ck, rcs[i], "[%d] result", i);
	}

	/** print footer */
	DLCK_PRINT_SEPARATOR(ck);
	CK_PRINTF(ck, "Total: %u warning(s).\n", warnings_num);
	DLCK_PRINT_SEPARATOR(ck);
}
