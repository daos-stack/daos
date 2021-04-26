/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pool)

#include "srv_internal.h"
#include <gurt/telemetry_producer.h>

/**
 * Global pool metrics
 */
struct pool_metrics ds_pool_metrics;

/**
 * Initializes the pool metrics
 */
int
ds_pool_metrics_init(void)
{
	int rc;

	memset(&ds_pool_metrics, 0, sizeof(ds_pool_metrics));

	rc = d_tm_add_metric(&ds_pool_metrics.open_hdl_gauge, D_TM_GAUGE,
			     "Number of open pool handles", "",
			     "pool/ops/open/active");
	if (rc != 0)
		D_ERROR("Couldn't add open handle gauge: "DF_RC"\n", DP_RC(rc));

	return rc;
}

/**
 * Finalizes the pool metrics
 */
int
ds_pool_metrics_fini(void)
{
	return 0;
}
