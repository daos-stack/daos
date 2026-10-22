/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(dlck)

#include <abt.h>
#include <stdbool.h>

#include <daos_errno.h>
#include <daos_srv/daos_engine.h>

#include "dlck_checker.h"
#include "dlck_engine.h"
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
		memset(separator, '=', DLCK_PROGRESS_LINE_LEN - 1);
		initialized = true;
	}

	return separator;
}

#define DLCK_PRINT_SEPARATOR(ck) CK_PRINTF(ck, "%s\n", get_separator())

static void
dlck_report_status(struct checker_status *st, struct checker *ck)
{
	if (st->errors_num + st->warnings_num != 0) {
		ck_common_printf(ck, "%u error(s), %u warning(s)", st->errors_num,
				 st->warnings_num);

		if (st->rc != DER_SUCCESS) {
			CK_APPENDL_RC(ck, st->rc);
		} else {
			CK_APPENDL(ck, ".");
		}
	} else {
		CK_APPENDL_RC(ck, st->rc);
	}
}

/**
 * \note This function is called when no other threads are running in parallel. No locks are
 * necessary.
 */
void
dlck_report_results(struct checker_status *sts, unsigned targets, struct checker *ck)
{
	struct checker_status total = {0};

	/** print header */
	DLCK_PRINT_SEPARATOR(ck);
	CK_PRINT(ck, "Targets:\n");
	DLCK_PRINT_SEPARATOR(ck);

	/** print records */
	for (int i = 0; i < targets; ++i) {
		CK_PRINTF(ck, "[%d] result: ", i);
		dlck_report_status(&sts[i], ck);
		dlck_status_set(&total, &sts[i]);
	}

	/** print footer */
	DLCK_PRINT_SEPARATOR(ck);
	CK_PRINT(ck, "Total: ");
	dlck_report_status(&total, ck);
	DLCK_PRINT_SEPARATOR(ck);
}
