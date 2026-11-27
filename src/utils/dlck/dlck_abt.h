/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_ABT__
#define __DLCK_ABT__

#include <abt.h>

typedef void (*dlck_ult_func)(void *arg);

/**
 * Create an ABT thread (ULT).
 *
 * \param[in]	pool	Pool to put the created ULT in.
 * \param[in]	func	Function to start on the created ULT.
 * \param[in]	arg	Argument pointer for the function.
 * \param[out]	thread	Created ULT handle.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_ult_create(ABT_pool pool, dlck_ult_func func, void *arg, ABT_thread *thread);

#endif /** __DLCK_ABT__ */
