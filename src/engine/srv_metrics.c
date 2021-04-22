/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "srv_internal.h"
#include <gurt/telemetry_producer.h>

/*
 * Global engine metrics
 */
static struct engine_metrics metrics;

/**
 * Initialize the I/O engine metrics.
 */
int
dss_engine_metrics_init(void)
{
	int rc;

	memset(&metrics, 0, sizeof(metrics));

	rc = d_tm_add_metric(&metrics.started_time, D_TM_TIMESTAMP,
			     "Timestamp of last engine startup", NULL,
			     "started_at");
	if (rc != 0) {
		D_ERROR("unable to add metric for startup timestamp: "
			DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = d_tm_add_metric(&metrics.ready_time, D_TM_TIMESTAMP,
			     "Timestamp when the engine became ready", NULL,
			     "servicing_at");
	if (rc != 0) {
		D_ERROR("unable to add metric for ready timestamp: "
			DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = d_tm_add_metric(&metrics.rank_id, D_TM_COUNTER,
			     "Rank ID of this engine", "",
			     "rank");
	if (rc != 0) {
		D_ERROR("unable to add metric for rank ID: "
			DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = d_tm_add_metric(&metrics.dead_rank_events, D_TM_COUNTER,
			     "Number of dead rank events received", NULL,
			     "events/dead_ranks");
	if (rc != 0) {
		D_ERROR("unable to add metric for dead ranks: "
			DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = d_tm_add_metric(&metrics.last_event_time, D_TM_TIMESTAMP,
			     "Timestamp of last received event", NULL,
			     "events/last_event_ts");
	if (rc != 0) {
		D_ERROR("unable to add metric for last event timestamp: "
			DF_RC "\n", DP_RC(rc));
		return rc;
	}

	return 0;
}

/**
 * Finalize the I/O engine metrics.
 */
int
dss_engine_metrics_fini(void)
{
	/* nothing to do */
	return 0;
}

/**
 * Fetch the engine metrics.
 *
 * \return pointer to the engine metrics
 */
struct engine_metrics *
dss_get_engine_metrics(void)
{
	return &metrics;
}
