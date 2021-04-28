/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * ds_cont: Container Server
 *
 * This is part of daos_server. This file manages the container-related metrics.
 */
#define D_LOGFAC	DD_FAC(container)

#include "srv_internal.h"
#include <gurt/telemetry_producer.h>

/* Global container metrics */
struct cont_metrics ds_cont_metrics;

/**
 * Initialize global metrics used in the server container module.
 */
int
ds_cont_metrics_init(void)
{
	int rc;

	memset(&ds_cont_metrics, 0, sizeof(ds_cont_metrics));

	rc = d_tm_add_metric(&ds_cont_metrics.op_open_ctr, D_TM_COUNTER,
			     "Number of times cont_open has been called", "",
			     "container/ops/open/total");
	if (rc != 0)
		D_ERROR("failed to add open counter: "
			DF_RC "\n", DP_RC(rc));

	rc = d_tm_add_metric(&ds_cont_metrics.open_cont_gauge, D_TM_GAUGE,
			     "Number of open container handles", "",
			     "container/ops/open/active");
	if (rc != 0)
		D_ERROR("failed to add open cont gauge: "
			DF_RC "\n", DP_RC(rc));

	rc = d_tm_add_metric(&ds_cont_metrics.op_close_ctr, D_TM_COUNTER,
			     "Number of times cont_close has been called", "",
			     "container/ops/close/total");
	if (rc != 0)
		D_ERROR("failed to add close counter: "
			DF_RC "\n", DP_RC(rc));

	rc = d_tm_add_metric(&ds_cont_metrics.op_destroy_ctr, D_TM_COUNTER,
			     "Number of times cont_destroy has been called", "",
			     "container/ops/destroy/total");
	if (rc != 0)
		D_ERROR("failed to add close counter: "
			DF_RC "\n", DP_RC(rc));

	return 0;
}

/**
 * Finalize global metrics used in the server container module.
 */
int
ds_cont_metrics_fini(void)
{
	/* nothing to do - shared memory will be cleaned up automatically */
	return 0;
}
