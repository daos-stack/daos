/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pool)

#include "srv_internal.h"
#include <abt.h>
#include <gurt/telemetry_producer.h>


/* Estimate of bytes per typical metric node */
#define NODE_BYTES		(sizeof(struct d_tm_node_t) + \
				 sizeof(struct d_tm_metric_t) + \
				 64 /* buffer for metadata */)
/* Estimate of bytes per histogram bucket */
#define BUCKET_BYTES		(sizeof(struct d_tm_bucket_t) + NODE_BYTES)
/*
   Estimate of bytes per metric.
   This is a generous high-water mark assuming most metrics are not using
   histograms. May need adjustment if the balance of metrics changes.
*/
#define PER_METRIC_BYTES	(NODE_BYTES + sizeof(struct d_tm_stats_t) + \
				 sizeof(struct d_tm_histogram_t) + \
				 BUCKET_BYTES)

/**
 * Initializes the pool metrics
 */
void *
ds_pool_metrics_alloc(const char *path, int tgt_id)
{
	struct pool_metrics	*metrics;
	struct d_tm_node_t	*started;
	int			 rc;

	D_ASSERT(tgt_id < 0);

	D_ALLOC_PTR(metrics);
	if (metrics == NULL)
		return NULL;

	rc = d_tm_add_metric(&started, D_TM_TIMESTAMP,
			     "Last time the pool started", NULL,
			     "%s/started_at", path);
	if (rc != 0) /* Probably a bad sign, but not fatal */
		D_WARN("Failed to create started_timestamp metric: "DF_RC"\n", DP_RC(rc));
	else
		d_tm_record_timestamp(started);

	rc = d_tm_add_metric(&metrics->evict_total, D_TM_COUNTER,
			     "Total number of pool handle evict operations", "ops",
			     "%s/ops/pool_evict", path);
	if (rc != 0)
		D_WARN("Failed to create evict hdl counter: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&metrics->connect_total, D_TM_COUNTER,
			     "Total number of processed pool connect operations", "ops",
			     "%s/ops/pool_connect", path);
	if (rc != 0)
		D_WARN("Failed to create pool connect counter: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&metrics->disconnect_total, D_TM_COUNTER,
			     "Total number of processed pool disconnect operations", "ops",
			     "%s/ops/pool_disconnect", path);
	if (rc != 0)
		D_WARN("Failed to create pool disconnect counter: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&metrics->query_total, D_TM_COUNTER,
			     "Total number of processed pool query operations", "ops",
			     "%s/ops/pool_query", path);
	if (rc != 0)
		D_WARN("Failed to create pool query counter: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&metrics->query_space_total, D_TM_COUNTER,
			     "Total number of processed pool query (with space) operations", "ops",
			     "%s/ops/pool_query_space", path);
	if (rc != 0)
		D_WARN("Failed to create pool query space counter: "DF_RC"\n", DP_RC(rc));

	return metrics;
}

int
ds_pool_metrics_count(void)
{
	return (sizeof(struct pool_metrics) / sizeof(struct d_tm_node_t *));
}

/**
 * Release the pool metrics
 */
void
ds_pool_metrics_free(void *data)
{
	D_FREE(data);
}

/**
 * Generate the metrics path for a specific pool UUID.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[out]	path		Path to the pool metrics
 * \param[in]	path_len	Length of path array
 */
static void
pool_metrics_gen_path(const uuid_t pool_uuid, char *path, size_t path_len)
{
	snprintf(path, path_len, "pool/"DF_UUIDF, DP_UUID(pool_uuid));
	path[path_len - 1] = '\0';
}

static int
get_pool_dir_size(void)
{
	return dss_module_nr_pool_metrics() * PER_METRIC_BYTES;
}

/**
 * Add metrics for a specific pool.
 *
 * \param[in]	pool	pointer to ds_pool structure
 */
int
ds_pool_metrics_start(struct ds_pool *pool)
{
	int rc;

	pool_metrics_gen_path(pool->sp_uuid, pool->sp_path,
			      sizeof(pool->sp_path));

	/** create new shmem space for per-pool metrics */
	rc = d_tm_add_ephemeral_dir(NULL, get_pool_dir_size(), pool->sp_path);
	if (rc != 0) {
		D_WARN(DF_UUID ": failed to create metrics dir for pool: "
		       DF_RC "\n", DP_UUID(pool->sp_uuid), DP_RC(rc));
		return rc;
	}

	/* initialize metrics on the system xstream for each module */
	rc = dss_module_init_metrics(DAOS_SYS_TAG, pool->sp_metrics,
				     pool->sp_path, -1);
	if (rc != 0) {
		D_WARN(DF_UUID ": failed to initialize module metrics: "
		       DF_RC"\n", DP_UUID(pool->sp_uuid), DP_RC(rc));
		ds_pool_metrics_stop(pool);
		return rc;
	}

	D_INFO(DF_UUID ": created metrics for pool\n", DP_UUID(pool->sp_uuid));

	return 0;
}

/**
 * Destroy metrics for a specific pool.
 *
 * \param[in]	pool	pointer to ds_pool structure
 */
void
ds_pool_metrics_stop(struct ds_pool *pool)
{
	int rc;

	dss_module_fini_metrics(DAOS_SYS_TAG, pool->sp_metrics);

	rc = d_tm_del_ephemeral_dir(pool->sp_path);
	if (rc != 0) {
		D_WARN(DF_UUID ": failed to remove pool metrics dir for pool: "
		       DF_RC"\n", DP_UUID(pool->sp_uuid), DP_RC(rc));
		return;
	}

	D_INFO(DF_UUID ": destroyed ds_pool metrics\n", DP_UUID(pool->sp_uuid));
}
