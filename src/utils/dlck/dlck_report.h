/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_REPORT__
#define __DLCK_REPORT__

/**
 * Report targets' results.
 *
 * \param[in]	rcs	        Array of return codes for all targets.
 * \param[in]	targets	        Number of targets.
 * \param[in]	warnings_num	Number of warnings.
 * \param[in]	dp	        Main print utility.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_MISC	Printing error.
 * \retval -DER_*	Other errors.
 */
void
dlck_report_results(int *rcs, unsigned targets, unsigned warnings_num, struct dlck_print *dp);

#endif /** __DLCK_REPORT__ */
