/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pool)

#include "srv_internal.h"
#include <gurt/telemetry_producer.h>

/*
 * Parent directory in metrics tree for all the per-pool metric directories
 */
#define POOL_METRICS_DIR_FMT	"pool/current/%s"

/*
 * Size in bytes of each per-pool metric directory.
 */
#define POOL_METRICS_DIR_BYTES	(8 * 1024)

/**
 * Global pool metrics
 */
struct pool_metrics ds_pool_metrics;

/**
 * Per-pool metrics
 */
static d_list_t per_pool_metrics;

/**
 * Initializes the pool metrics
 */
int
ds_pool_metrics_init(void)
{
	int rc;

	memset(&ds_pool_metrics, 0, sizeof(ds_pool_metrics));

	D_INIT_LIST_HEAD(&per_pool_metrics);

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
	struct active_pool_metrics *cur = NULL;
	struct active_pool_metrics *next = NULL;

	d_list_for_each_entry_safe(cur, next, &per_pool_metrics, link) {
		d_list_del(&cur->link);
		D_FREE(cur);
	}
	return 0;
}

/**
 * Fetch metrics path for a specific pool.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[out]	path		Path to the pool metrics
 * \param[in]	path_len	Length of path array
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid inputs
 */
int
ds_pool_metrics_get_path(const uuid_t pool_uuid, char *path, size_t path_len)
{
	char uuid_str[DAOS_UUID_STR_SIZE];

	if (!daos_uuid_valid(pool_uuid)) {
		D_ERROR(DF_UUID ": invalid uuid\n", DP_UUID(pool_uuid));
		return -DER_INVAL;
	}
	uuid_unparse(pool_uuid, uuid_str);

	snprintf(path, path_len, POOL_METRICS_DIR_FMT, uuid_str);
	path[path_len - 1] = '\0';
	return 0;
}

/**
 * Add metrics for a specific pool UUID.
 *
 * \param[in]	pool_uuid	Pool UUID
 */
void
ds_pool_metrics_start(const uuid_t pool_uuid)
{
	struct active_pool_metrics	*metrics;
	char				 path[D_TM_MAX_NAME_LEN] = {0};
	int				 rc;

	metrics = ds_pool_metrics_get(pool_uuid);
	if (metrics != NULL) /* already exists - nothing to do */
		return;

	rc = ds_pool_metrics_get_path(pool_uuid, path, sizeof(path));
	if (rc != 0) {
		D_ERROR(DF_UUID ": unable to get pool metrics path, "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		return;
	}

	rc = d_tm_add_ephemeral_dir(NULL, POOL_METRICS_DIR_BYTES, path);
	if (rc != 0) {
		D_ERROR(DF_UUID ": unable to create metrics dir for pool, "
			DF_RC "\n", DP_UUID(pool_uuid), DP_RC(rc));
		return;
	}

	D_ALLOC_PTR(metrics);
	if (metrics == NULL) {
		D_ERROR(DF_UUID ": failed to allocate metrics struct\n",
			DP_UUID(pool_uuid));
		return;
	}

	uuid_copy(metrics->pool_uuid, pool_uuid);

	/* Init all of the per-pool metrics */
	rc = d_tm_add_metric(&metrics->started_timestamp, D_TM_TIMESTAMP,
			     "Last time the pool started", NULL,
			     "%s/started_at", path);
	if (rc != 0)
		D_ERROR(DF_UUID ": failed to add started_timestamp metric, "
			DF_RC "\n", DP_UUID(pool_uuid), DP_RC(rc));

	/* Track with the per-pool metrics */
	d_list_add(&metrics->link, &per_pool_metrics);

	D_INFO(DF_UUID ": created metrics for pool\n", DP_UUID(pool_uuid));
}

/**
 * Destroy metrics for a specific pool UUID.
 *
 * \param[in]	pool_uuid	Pool UUID
 */
void
ds_pool_metrics_stop(const uuid_t pool_uuid)
{
	struct active_pool_metrics	*metrics;
	char				 path[D_TM_MAX_NAME_LEN] = {0};
	int				 rc;

	if (!daos_uuid_valid(pool_uuid)) {
		D_ERROR(DF_UUID ": invalid uuid\n", DP_UUID(pool_uuid));
		return;
	}

	metrics = ds_pool_metrics_get(pool_uuid);
	if (metrics != NULL) {
		d_list_del(&metrics->link);
		D_FREE(metrics);
	}

	rc = ds_pool_metrics_get_path(pool_uuid, path, sizeof(path));
	if (rc != 0) {
		D_ERROR(DF_UUID ": unable to get pool metrics path, "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		return;
	}

	rc = d_tm_del_ephemeral_dir(path);
	if (rc != 0) {
		D_ERROR(DF_UUID ": unable to remove metrics dir for pool, "
			DF_RC "\n", DP_UUID(pool_uuid), DP_RC(rc));
		return;
	}

	D_INFO(DF_UUID ": destroyed metrics for pool\n", DP_UUID(pool_uuid));
}

/**
 * Get metrics for a specific active pool.
 *
 * \param[in]	pool_uuid	Pool UUID
 *
 * \return	Pool's metrics structure, or NULL if not found
 */
struct active_pool_metrics *
ds_pool_metrics_get(const uuid_t pool_uuid)
{
	struct active_pool_metrics *cur = NULL;

	d_list_for_each_entry(cur, &per_pool_metrics, link) {
		if (uuid_compare(pool_uuid, cur->pool_uuid) == 0)
			return cur;
	}

	return NULL;
}
