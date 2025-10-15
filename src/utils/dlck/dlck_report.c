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

/**
 * Check fprintf()/snprintf() return code. Return in case of an error.
 */
#define _PRINTF_CHECK_RC(rc)                                                                       \
	do {                                                                                       \
		if (rc < 0) {                                                                      \
			return -DER_MISC;                                                          \
		}                                                                                  \
	} while (0)

#define DLCK_RESULT_FMT "[%d] result: "

static int
report_target_result(struct dlck_print_main *dpm, int tgt_id, int tgt_rc)
{
	int rc;

	if (tgt_rc == DER_SUCCESS) {
		rc = fprintf(dpm->stream, DLCK_RESULT_FMT DLCK_OK_INFIX ".\n", tgt_id);
	} else {
		rc = fprintf(dpm->stream, DLCK_RESULT_FMT DF_RC "\n", tgt_id, DP_RC(tgt_rc));
	}
	_PRINTF_CHECK_RC(rc);

	return DER_SUCCESS;
}

#define DLCK_PROGRESS_HEADER   "Targets:"
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

/**
 * Print the report header:
 *
 * ========
 * Targets:
 * ========
 *
 * \param[in]	dpm	Main print utility (just the custom payload).
 * \param[in]	targets	Number of targets.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_MISC	Print failed.
 */
static int
report_header(struct dlck_print_main *dpm, unsigned targets)
{
	char *separator = get_separator();
	int   rc;

	/** print header */
	rc = fprintf(dpm->stream, "%s\n", separator);
	_PRINTF_CHECK_RC(rc);
	rc = fprintf(dpm->stream, DLCK_PROGRESS_HEADER "\n");
	_PRINTF_CHECK_RC(rc);
	rc = fprintf(dpm->stream, "%s\n", separator);
	_PRINTF_CHECK_RC(rc);

	return 0;
}

/**
 * Print the report footer:
 *
 * ========
 *
 * \param[in]	dpm	Main print utility (just the custom payload).
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_MISC	Print failed.
 */
static int
report_footer(struct dlck_print_main *dpm)
{
	char *separator = get_separator();
	int   rc;

	rc = fprintf(dpm->stream, "%s\n", separator);
	_PRINTF_CHECK_RC(rc);

	return DER_SUCCESS;
}

/**
 * \note This function is called when no other threads are running in parallel. No locks are
 * necessary.
 */
int
dlck_report_results(int *rcs, unsigned targets, unsigned warnings_num, struct dlck_print *dp)
{
	struct dlck_print_main *dpm = dlck_print_main_get_custom(dp);
	int                     rc;

	if (DAOS_FAIL_CHECK(DLCK_FAULT_REPORT)) { /** fault injection */
		return daos_errno2der(daos_fail_value_get());
	}

	/** print header */
	rc = report_header(dpm, targets);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	/** print records */
	for (int i = 0; i < targets; ++i) {
		rc = report_target_result(dpm, i, rcs[i]);
		if (rc != DER_SUCCESS) {
			return rc;
		}
	}

	rc = report_footer(dpm);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	if (warnings_num > 0) {
		DLCK_PRINTF(dp, "Total: %u warning(s).\n", warnings_num);
	} else {
		DLCK_PRINT(dp, "No warnings.\n");
	}

	/** print footer */
	return report_footer(dpm);
}
