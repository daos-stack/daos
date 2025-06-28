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

/** XXX should be shared with the DAOS engine */
#define DSS_DEEP_STACK_SZ 65536

int
dlck_abt_attr_default_create(ABT_thread_attr *attr)
{
	int rc;

	rc = ABT_thread_attr_create(attr);
	if (rc != 0) {
		/** XXX translate ABT return code */
		return rc;
	}

	rc = ABT_thread_attr_set_stacksize(*attr, DSS_DEEP_STACK_SZ);
	if (rc != 0) {
		/** XXX translate ABT return code */
		return rc;
	}

	return 0;
}

int
dlck_abt_init(struct dlck_engine *engine)
{
	int rc;

	rc = ABT_init(0, NULL);
	if (rc != ABT_SUCCESS) {
		/** XXX translate ABT return code */
		return rc;
	}

	rc = ABT_mutex_create(&engine->open_mtx);
	if (rc != ABT_SUCCESS) {
		/** XXX translate ABT return code */
		return rc;
	}

	return 0;
}

int
dlck_xstream_create(struct dlck_xstream *xs)
{
	int rc;

	rc = ABT_xstream_create(ABT_SCHED_NULL, &xs->xstream);
	if (rc != ABT_SUCCESS) {
		/** XXX translate ABT error */
		return rc;
	}
	rc = ABT_xstream_get_main_pools(xs->xstream, 1, &xs->pool);
	if (rc != ABT_SUCCESS) {
		/** XXX translate ABT error */
		return rc;
	}

	return 0;
}

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
