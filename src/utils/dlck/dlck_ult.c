/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <libgen.h>
#include <stdlib.h>
#include <stdio.h>
#include <daos/mem.h>
#include <daos/btree_class.h>
#include <gurt/telemetry_producer.h>
#include <daos_srv/vos.h>
#include <daos_srv/dlck.h>
#include <daos_version.h>

#include "dlck_engine.h"

/**
 * XXX missing teardown
 */
int
dlck_ult_create(ABT_pool pool, dlck_ult_func func, void *arg, struct dlck_ult *ult)
{
	ABT_thread_attr attr;
	int             rc;

	rc = dlck_abt_attr_default_create(&attr);
	if (rc) {
		return rc;
	}

	rc = ABT_thread_create(pool, func, arg, attr, &ult->thread);
	if (rc != ABT_SUCCESS) {
		/** XXX translate ABT error */
		return rc;
	}

	/** XXX teardown attr */

	return DER_SUCCESS;
}

int
dlck_ult_create_on_xstream(struct dlck_xstream *xs, dlck_ult_func func, void *arg,
			   struct dlck_ult *ult)
{
	ABT_thread_attr attr;
	int             rc;

	rc = dlck_abt_attr_default_create(&attr);
	if (rc) {
		return rc;
	}

	rc = ABT_thread_create_on_xstream(xs->xstream, func, arg, attr, &ult->thread);
	if (rc != ABT_SUCCESS) {
		/** XXX translate ABT error */
		return rc;
	}

	/** XXX teardown attr */

	return DER_SUCCESS;
}