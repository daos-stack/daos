/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pool)

#include "srv_internal.h"
#include <abt.h>
#include <gurt/telemetry_producer.h>

/*
 * Size in bytes of each per-pool metric directory.
 */
#define POOL_METRICS_DIR_BYTES	(96 * 1024)

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

	rc = d_tm_add_metric(&metrics->open_hdl_gauge, D_TM_GAUGE,
			     "Number of open pool handles", "hdls",
			     "%s/pool_handles", path);
	if (rc != 0)
		D_ERROR("Couldn't add open handle gauge: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&started, D_TM_TIMESTAMP,
			     "Last time the pool started", NULL,
			     "%s/started_at", path);
	if (rc != 0) /* Probably a bad sign, but not fatal */
		D_ERROR("Failed to add started_timestamp metric, " DF_RC "\n",
			 DP_RC(rc));
	else
		d_tm_record_timestamp(started);

	return metrics;
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
	rc = d_tm_add_ephemeral_dir(NULL, POOL_METRICS_DIR_BYTES, pool->sp_path);
	if (rc != 0) {
		D_ERROR(DF_UUID ": unable to create metrics dir for pool, "
			DF_RC "\n", DP_UUID(pool->sp_uuid), DP_RC(rc));
		return rc;
	}

	/* initialize metrics on the system xstream for each module */
	rc = dss_module_init_metrics(DAOS_SYS_TAG, pool->sp_metrics,
				     pool->sp_path, -1);
	if (rc != 0) {
		D_ERROR(DF_UUID ": failed to initialize module metrics, "
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
		D_ERROR(DF_UUID ": unable to remove metrics dir for pool, "
			DF_RC "\n", DP_UUID(pool->sp_uuid), DP_RC(rc));
		return;
	}

	D_INFO(DF_UUID ": destroyed ds_pool metrics\n", DP_UUID(pool->sp_uuid));
}
