/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos_srv/daos_engine.h>

#include "dlck_engine.h"

/**
 * Initialize ABT thread attributes as they are required for the use by the engine.
 *
 * \param[out]	attr	Engine for which ABT is initialized for.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
static int
dlck_abt_attr_create(ABT_thread_attr *attr)
{
	int rc;

	rc = ABT_thread_attr_create(attr);
	if (rc != 0) {
		return dss_abterr2der(rc);
	}

	rc = ABT_thread_attr_set_stacksize(*attr, DSS_DEEP_STACK_SZ);
	if (rc != 0) {
		(void)ABT_thread_attr_free(attr);
		return dss_abterr2der(rc);
	}

	return DER_SUCCESS;
}

/**
 * Free ABT thread attributes.
 *
 * \param[in]	attr	Attributes to free.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
static int
dlck_abt_attr_free(ABT_thread_attr *attr)
{
	return dss_abterr2der(ABT_thread_attr_free(attr));
}

int
dlck_ult_create(ABT_pool pool, dlck_ult_func func, void *arg, ABT_thread *thread)
{
	ABT_thread_attr attr;
	int             rc;

	rc = dlck_abt_attr_create(&attr);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	rc = ABT_thread_create(pool, func, arg, attr, thread);
	if (rc != ABT_SUCCESS) {
		(void)dlck_abt_attr_free(&attr);
		return dss_abterr2der(rc);
	}

	rc = dlck_abt_attr_free(&attr);
	if (rc != DER_SUCCESS) {
		(void)ABT_thread_free(thread);
		return rc;
	}

	return DER_SUCCESS;
}
