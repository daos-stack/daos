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
#include <daos_srv/pool.h>

/*
 * Format for generating the container metrics directory path
 */
#define CONT_METRICS_DIR_FMT	"%s/cont/%s"

/*
 * Size in bytes of each per-container metric directory.
 */
#define CONT_METRICS_DIR_BYTES	(2 * 1024)

/* Global container metrics */
struct cont_metrics ds_cont_metrics;

/**
 * Per-container metrics
 */
static d_list_t per_cont_metrics;

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

	D_INIT_LIST_HEAD(&per_cont_metrics);

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

/**
 * Fetch metrics path for a specific container.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[in]	cont_uuid	UUID of the container
 * \param[out]	path		Path to the container metrics
 * \param[in]	path_len	Length of path array
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid inputs
 */
int
ds_cont_metrics_get_path(const uuid_t pool_uuid, const uuid_t cont_uuid,
			 char *path, size_t path_len)
{
	char	pool_path[D_TM_MAX_NAME_LEN] = {0};
	char	c_uuid_str[DAOS_UUID_STR_SIZE];
	int	rc;

	if (!daos_uuid_valid(cont_uuid)) {
		D_ERROR(DF_CONT ": bad container UUID\n",
			DP_CONT(pool_uuid, cont_uuid));
		return -DER_INVAL;
	}
	uuid_unparse(cont_uuid, c_uuid_str);

	rc = ds_pool_metrics_get_path(pool_uuid, pool_path, sizeof(pool_path));
	if (rc != 0) {
		D_ERROR(DF_CONT ": unable to get path for pool uuid, " DF_RC
			"\n", DP_CONT(pool_uuid, cont_uuid), DP_RC(rc));
		return rc;
	}

	snprintf(path, path_len, CONT_METRICS_DIR_FMT, pool_path, c_uuid_str);
	path[path_len - 1] = '\0';
	return 0;
}

/**
 * Add metrics for a specific container.
 *
 * \param[in]	pool_uuid	Pool UUID
 * \param[in]	cont_uuid	Container UUID
 */
void
ds_cont_metrics_start(const uuid_t pool_uuid, const uuid_t cont_uuid)
{
	struct ds_active_cont_metrics	*metrics;
	char				 path[D_TM_MAX_NAME_LEN] = {0};
	int				 rc;

	metrics = ds_cont_metrics_get(pool_uuid, cont_uuid);
	if (metrics != NULL) /* already exists - nothing to do */
		return;

	rc = ds_cont_metrics_get_path(pool_uuid, cont_uuid, path, sizeof(path));
	if (rc != 0) {
		D_ERROR(DF_CONT ": unable to get pool metrics path, "DF_RC"\n",
			DP_CONT(pool_uuid, cont_uuid), DP_RC(rc));
		return;
	}

	rc = d_tm_add_ephemeral_dir(NULL, CONT_METRICS_DIR_BYTES, path);
	if (rc != 0) {
		D_ERROR(DF_CONT ": unable to create cont metrics dir, "
			DF_RC "\n", DP_CONT(pool_uuid, cont_uuid), DP_RC(rc));
		return;
	}

	D_ALLOC_PTR(metrics);
	if (metrics == NULL) {
		D_ERROR(DF_CONT ": failed to allocate metrics struct\n",
			DP_CONT(pool_uuid, cont_uuid));
		return;
	}

	uuid_copy(metrics->pool_uuid, pool_uuid);
	uuid_copy(metrics->cont_uuid, cont_uuid);

	/* Init all of the per-container metrics */
	rc = d_tm_add_metric(&metrics->start_timestamp, D_TM_TIMESTAMP,
			     "Last time the container started", NULL,
			     "%s/started_at", path);
	if (rc != 0)
		D_ERROR(DF_CONT ": failed to add started_timestamp metric, "
			DF_RC "\n", DP_CONT(pool_uuid, cont_uuid), DP_RC(rc));

	/* Track with the per-container metrics */
	d_list_add(&metrics->link, &per_cont_metrics);

	D_INFO(DF_CONT ": created metrics for cont\n",
	       DP_CONT(pool_uuid, cont_uuid));
}

/**
 * Destroy metrics for a specific container.
 *
 * \param[in]	pool_uuid	Pool UUID
 * \param[in]	cont_uuid	Container UUID
 */
void
ds_cont_metrics_stop(const uuid_t pool_uuid, const uuid_t cont_uuid)
{
	struct ds_active_cont_metrics	*metrics;
	char				 path[D_TM_MAX_NAME_LEN] = {0};
	int				 rc;

	metrics = ds_cont_metrics_get(pool_uuid, cont_uuid);
	if (metrics != NULL) {
		d_list_del(&metrics->link);
		D_FREE(metrics);
	}

	rc = ds_cont_metrics_get_path(pool_uuid, cont_uuid, path, sizeof(path));
	if (rc != 0) {
		D_ERROR(DF_CONT ": unable to get cont metrics path, "DF_RC"\n",
			DP_CONT(pool_uuid, cont_uuid), DP_RC(rc));
		return;
	}

	rc = d_tm_del_ephemeral_dir(path);
	if (rc != 0) {
		D_ERROR(DF_CONT ": unable to remove metrics dir for cont, "
			DF_RC "\n", DP_CONT(pool_uuid, cont_uuid), DP_RC(rc));
		return;
	}

	D_INFO(DF_CONT ": destroyed metrics for container\n",
	       DP_CONT(pool_uuid, cont_uuid));
}

/**
 * Get metrics for a specific active container.
 *
 * \param[in]	pool_uuid	Pool UUID
 * \param[in]	cont_uuid	Container UUID
 *
 * \return	Container's metrics structure, or NULL if not found
 */
struct ds_active_cont_metrics *
ds_cont_metrics_get(const uuid_t pool_uuid, const uuid_t cont_uuid)
{
	struct ds_active_cont_metrics *cur = NULL;

	d_list_for_each_entry(cur, &per_cont_metrics, link) {
		if (uuid_compare(pool_uuid, cur->pool_uuid) == 0 &&
		    uuid_compare(cont_uuid, cur->cont_uuid) == 0)
			return cur;
	}

	return NULL;
}
