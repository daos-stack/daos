/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_REPORT__
#define __DLCK_REPORT__

/**
 * Report targets' results.
 *
 * \param[in]	sts	        Array of statuses for all targets.
 * \param[in]	targets	        Number of targets.
 * \param[in]	ck	        Main checker.
 */
void
dlck_report_results(struct checker_status *sts, unsigned targets, struct checker *ck);

#endif /** __DLCK_REPORT__ */
