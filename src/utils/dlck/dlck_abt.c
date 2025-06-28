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

	return 0;
}
