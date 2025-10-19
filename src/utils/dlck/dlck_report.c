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

#include "dlck_print.h"
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

#define DLCK_PRINT_SEPARATOR(dp) DLCK_PRINTF(dp, "%s\n", get_separator())

/**
 * \note This function is called when no other threads are running in parallel. No locks are
 * necessary.
 */
void
dlck_report_results(int *rcs, unsigned targets, unsigned warnings_num, struct dlck_print *dp)
{
	/** print header */
	DLCK_PRINT_SEPARATOR(dp);
	DLCK_PRINT(dp, "Targets:\n");
	DLCK_PRINT_SEPARATOR(dp);

	/** print records */
	for (int i = 0; i < targets; ++i) {
		DLCK_PRINTFL_RC(dp, rcs[i], "[%d] result", i);
	}

	/** print footer */
	DLCK_PRINT_SEPARATOR(dp);
	DLCK_PRINTF(dp, "Total: %u warning(s).\n", warnings_num);
	DLCK_PRINT_SEPARATOR(dp);
}
