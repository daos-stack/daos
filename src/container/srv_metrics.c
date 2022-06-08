/**
 * (C) Copyright 2021-2022 Intel Corporation.
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

	rc = d_tm_add_metric(&metrics->open_total, D_TM_COUNTER,
			     "Total number of successful container open operations",
			     "ops", "%s/ops/cont_open", path);
	if (rc != 0)
		D_WARN("Failed to create container open counter: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&metrics->close_total, D_TM_COUNTER,
			     "Total number of successful container close operations",
			     "ops", "%s/ops/cont_close", path);
	if (rc != 0)
		D_WARN("Failed to create container close counter: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&metrics->query_total, D_TM_COUNTER,
			     "Total number of successful container query operations",
			     "ops", "%s/ops/cont_query", path);
	if (rc != 0)
		D_WARN("Failed to create container query counter: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&metrics->create_total, D_TM_COUNTER,
			     "Total number of successful container create operations",
			     "ops", "%s/ops/cont_create", path);
	if (rc != 0)
		D_WARN("Failed to create container create counter: "DF_RC"\n", DP_RC(rc));


	rc = d_tm_add_metric(&metrics->destroy_total, D_TM_COUNTER,
			     "Total number of successful container destroy operations",
			     "ops", "%s/ops/cont_destroy", path);
	if (rc != 0)
		D_WARN("Failed to create container destroy counter: "DF_RC"\n", DP_RC(rc));

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
