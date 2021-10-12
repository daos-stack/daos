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

/**
 * Initialize global metrics used in the server container module.
 */

void *
ds_cont_metrics_alloc(const char *path, int tgt_id)
{
	struct cont_pool_metrics	*metrics;
	int				 rc;

	D_ASSERT(tgt_id < 0);

	D_ALLOC_PTR(metrics);
	if (metrics == NULL)
		return NULL;

	rc = d_tm_add_metric(&metrics->cpm_open_count, D_TM_COUNTER,
			     "Number of times cont_open has been called",
			     "ops", "%s/ops/cont_open", path);
	if (rc != 0)
		D_ERROR("failed to add open counter: " DF_RC "\n", DP_RC(rc));

	rc = d_tm_add_metric(&metrics->cpm_open_cont_gauge, D_TM_GAUGE,
			     "Number of open container handles", "hdls",
			     "%s/container_handles", path);
	if (rc != 0)
		D_ERROR("failed to add open cont gauge: " DF_RC "\n",
			DP_RC(rc));

	rc = d_tm_add_metric(&metrics->cpm_close_count, D_TM_COUNTER,
			     "Number of times cont_close has been called",
			     "ops", "%s/ops/cont_close", path);
	if (rc != 0)
		D_ERROR("failed to add close counter: "
			DF_RC "\n", DP_RC(rc));

	rc = d_tm_add_metric(&metrics->cpm_destroy_count, D_TM_COUNTER,
			     "Number of times cont_destroy has been called",
			     "ops", "%s/ops/cont_destroy", path);
	if (rc != 0)
		D_ERROR("failed to add close counter: " DF_RC "\n", DP_RC(rc));

	return metrics;
}

int
ds_cont_metrics_count(void)
{
	return (sizeof(struct cont_pool_metrics) / sizeof(struct d_tm_node_t *));
}

/**
 * Finalize global metrics used in the server container module.
 */
void
ds_cont_metrics_free(void *data)
{
	D_FREE(data);
}
