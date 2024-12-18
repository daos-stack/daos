/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(dfs)

#include <uuid/uuid.h>
#include <fcntl.h>

#include <daos.h>
#include <daos_fs.h>
#include <daos_fs_sys.h>
#include <daos/common.h>
#include <daos/container.h>
#include <daos/metrics.h>
#include <daos/pool.h>
#include <daos/job.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>
#include <gurt/telemetry_consumer.h>

#include "metrics.h"
#include "dfs_internal.h"

#define DFS_METRICS_ROOT  "dfs"

#define STAT_METRICS_SIZE (D_TM_METRIC_SIZE * DOS_LIMIT)
#define FILE_METRICS_SIZE (((D_TM_METRIC_SIZE * NR_SIZE_BUCKETS) * 2) + D_TM_METRIC_SIZE * 2)
#define DFS_METRICS_SIZE  (STAT_METRICS_SIZE + FILE_METRICS_SIZE)

#define SPRINTF_TM_PATH(buf, pool_uuid, cont_uuid, path)                                           \
	snprintf(buf, sizeof(buf), "pool/" DF_UUIDF "/container/" DF_UUIDF "/%s",                  \
		 DP_UUID(pool_uuid), DP_UUID(cont_uuid), path);

#define ADD_STAT_METRIC(name, ...)                                                                 \
	SPRINTF_TM_PATH(tmp_path, pool_uuid, cont_uuid, DFS_METRICS_ROOT "/ops/" #name);           \
	rc = d_tm_add_metric(&metrics->dm_op_stats[i], D_TM_COUNTER, "Count of " #name " calls",   \
			     "calls", tmp_path);                                                   \
	if (rc != 0) {                                                                             \
		DL_ERROR(rc, "failed to create " #name " counter");                                \
		return;                                                                            \
	}                                                                                          \
	i++;

static void
op_stats_init(struct dfs_metrics *metrics, uuid_t pool_uuid, uuid_t cont_uuid)
{
	char tmp_path[D_TM_MAX_NAME_LEN] = {0};
	int  i                           = 0;
	int  rc;

	if (metrics == NULL)
		return;

	D_FOREACH_DFS_OP_STAT(ADD_STAT_METRIC);
}

static void
cont_stats_init(struct dfs_metrics *metrics, uuid_t pool_uuid, uuid_t cont_uuid)
{
	char tmp_path[D_TM_MAX_NAME_LEN] = {0};
	int  rc                          = 0;

	if (metrics == NULL)
		return;

	SPRINTF_TM_PATH(tmp_path, pool_uuid, cont_uuid, "mount_time");
	rc = d_tm_add_metric(&metrics->dm_mount_time, D_TM_TIMESTAMP, "container mount time", NULL,
			     tmp_path);
	if (rc != 0)
		DL_ERROR(rc, "failed to create mount_time timestamp");
}

static void
file_stats_init(struct dfs_metrics *metrics, uuid_t pool_uuid, uuid_t cont_uuid)
{
	char tmp_path[D_TM_MAX_NAME_LEN] = {0};
	int  rc                          = 0;

	if (metrics == NULL)
		return;

	SPRINTF_TM_PATH(tmp_path, pool_uuid, cont_uuid, DFS_METRICS_ROOT "/read_bytes");
	rc = d_tm_add_metric(&metrics->dm_read_bytes, D_TM_STATS_GAUGE, "dfs read bytes", "bytes",
			     tmp_path);
	if (rc != 0)
		DL_ERROR(rc, "failed to create dfs read_bytes counter");
	rc =
	    d_tm_init_histogram(metrics->dm_read_bytes, tmp_path, NR_SIZE_BUCKETS, 256, 2, "bytes");
	if (rc)
		DL_ERROR(rc, "Failed to init dfs read size histogram");

	SPRINTF_TM_PATH(tmp_path, pool_uuid, cont_uuid, DFS_METRICS_ROOT "/write_bytes");
	rc = d_tm_add_metric(&metrics->dm_write_bytes, D_TM_STATS_GAUGE, "dfs write bytes", "bytes",
			     tmp_path);
	if (rc != 0)
		DL_ERROR(rc, "failed to create dfs write_bytes counter");
	rc = d_tm_init_histogram(metrics->dm_write_bytes, tmp_path, NR_SIZE_BUCKETS, 256, 2,
				 "bytes");
	if (rc)
		DL_ERROR(rc, "Failed to init dfs write size histogram");
}

bool
dfs_metrics_enabled()
{
	/* set in client/api/metrics.c */
	return daos_client_metric;
}

void
dfs_metrics_init(dfs_t *dfs)
{
	uuid_t pool_uuid;
	uuid_t cont_uuid;
	char   root_name[D_TM_MAX_NAME_LEN];
	pid_t  pid       = getpid();
	size_t root_size = DFS_METRICS_SIZE + (D_TM_METRIC_SIZE * 3);
	int    rc;

	if (dfs == NULL)
		return;

	rc = dc_pool_hdl2uuid(dfs->poh, NULL, &pool_uuid);
	if (rc != 0) {
		DL_ERROR(rc, "failed to get pool UUID");
		goto error;
	}

	rc = dc_cont_hdl2uuid(dfs->coh, NULL, &cont_uuid);
	if (rc != 0) {
		DL_ERROR(rc, "failed to get container UUID");
		goto error;
	}

	snprintf(root_name, sizeof(root_name), "%d", pid);
	/* if only container-level metrics are enabled; this will init a root for them */
	rc = d_tm_init_with_name(d_tm_cli_pid_key(pid), root_size, D_TM_OPEN_OR_CREATE, root_name);
	if (rc != 0 && rc != -DER_ALREADY) {
		DL_ERROR(rc, "failed to init DFS metrics");
		goto error;
	}

	D_ALLOC_PTR(dfs->metrics);
	if (dfs->metrics == NULL) {
		D_ERROR("failed to alloc DFS metrics");
		goto error;
	}

	SPRINTF_TM_PATH(root_name, pool_uuid, cont_uuid, DFS_METRICS_ROOT);
	rc = d_tm_add_ephemeral_dir(NULL, DFS_METRICS_SIZE, root_name);
	if (rc != 0) {
		DL_ERROR(rc, "failed to add DFS metrics dir");
		goto error;
	}

	cont_stats_init(dfs->metrics, pool_uuid, cont_uuid);
	op_stats_init(dfs->metrics, pool_uuid, cont_uuid);
	file_stats_init(dfs->metrics, pool_uuid, cont_uuid);

	d_tm_record_timestamp(dfs->metrics->dm_mount_time);
	return;

error:
	if (dfs->metrics != NULL)
		D_FREE(dfs->metrics);
}

void
dfs_metrics_fini(dfs_t *dfs)
{
	D_FREE(dfs->metrics);
}