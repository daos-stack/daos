/**
 * (C) Copyright 2024 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(dfs)

#include <uuid/uuid.h>
#include <fcntl.h>
#include <sys/utsname.h>

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

#define DFS_METRICS_ROOT    "dfs"
#define DFS_DUMPTIME_METRIC "dump_time"

#define STAT_METRICS_SIZE   (D_TM_METRIC_SIZE * DOS_LIMIT)
#define FILE_METRICS_SIZE   (((D_TM_METRIC_SIZE * NR_SIZE_BUCKETS) * 2) + D_TM_METRIC_SIZE * 2)
#define DFS_METRICS_SIZE    (STAT_METRICS_SIZE + FILE_METRICS_SIZE)

#define SPRINTF_TM_PATH(buf, pool_uuid, cont_uuid, path)                                           \
	snprintf(buf, sizeof(buf), "pool/" DF_UUIDF "/container/" DF_UUIDF "/%s",                  \
		 DP_UUID(pool_uuid), DP_UUID(cont_uuid), path);

#define ADD_STAT_METRIC(name, ...)                                                                 \
	SPRINTF_TM_PATH(tmp_path, pool_uuid, cont_uuid, DFS_METRICS_ROOT "/ops/" #name);           \
	rc = d_tm_add_metric(&metrics->dm_op_stats[i], D_TM_COUNTER, "Count of " #name " calls",   \
			     "calls", tmp_path);                                                   \
	if (rc != 0) {                                                                             \
		DL_ERROR(rc, "failed to create " #name " counter");                                \
		return rc;                                                                         \
	}                                                                                          \
	i++;

static int
op_stats_init(struct dfs_metrics *metrics, uuid_t pool_uuid, uuid_t cont_uuid)
{
	char tmp_path[D_TM_MAX_NAME_LEN] = {0};
	int  i                           = 0;
	int  rc;

	if (metrics == NULL)
		return 0;

	D_FOREACH_DFS_OP_STAT(ADD_STAT_METRIC);
	return 0;
}

static int
cont_stats_init(struct dfs_metrics *metrics, uuid_t pool_uuid, uuid_t cont_uuid)
{
	char tmp_path[D_TM_MAX_NAME_LEN] = {0};
	int  rc                          = 0;

	if (metrics == NULL)
		return 0;

	SPRINTF_TM_PATH(tmp_path, pool_uuid, cont_uuid, "mount_time");
	rc = d_tm_add_metric(&metrics->dm_mount_time, D_TM_TIMESTAMP, "container mount time", NULL,
			     tmp_path);
	if (rc != 0) {
		DL_ERROR(rc, "failed to create mount_time timestamp");
		return rc;
	}

	SPRINTF_TM_PATH(tmp_path, pool_uuid, cont_uuid, DFS_DUMPTIME_METRIC);
	rc = d_tm_add_metric(&metrics->dm_dump_time, D_TM_TIMESTAMP, "container dump time", NULL,
			     tmp_path);
	if (rc != 0) {
		DL_ERROR(rc, "failed to create dump_time timestamp");
		return rc;
	}
	return 0;
}

static int
file_stats_init(struct dfs_metrics *metrics, uuid_t pool_uuid, uuid_t cont_uuid)
{
	char tmp_path[D_TM_MAX_NAME_LEN] = {0};
	int  rc                          = 0;

	if (metrics == NULL)
		return 0;

	SPRINTF_TM_PATH(tmp_path, pool_uuid, cont_uuid, DFS_METRICS_ROOT "/read_bytes");
	rc = d_tm_add_metric(&metrics->dm_read_bytes, D_TM_STATS_GAUGE, "dfs read bytes", "bytes",
			     tmp_path);
	if (rc != 0)
		DL_ERROR(rc, "failed to create dfs read_bytes counter");
	rc =
	    d_tm_init_histogram(metrics->dm_read_bytes, tmp_path, NR_SIZE_BUCKETS, 256, 2, "bytes");
	if (rc) {
		DL_ERROR(rc, "Failed to init dfs read size histogram");
		return rc;
	}

	SPRINTF_TM_PATH(tmp_path, pool_uuid, cont_uuid, DFS_METRICS_ROOT "/write_bytes");
	rc = d_tm_add_metric(&metrics->dm_write_bytes, D_TM_STATS_GAUGE, "dfs write bytes", "bytes",
			     tmp_path);
	if (rc != 0)
		DL_ERROR(rc, "failed to create dfs write_bytes counter");
	rc = d_tm_init_histogram(metrics->dm_write_bytes, tmp_path, NR_SIZE_BUCKETS, 256, 2,
				 "bytes");
	if (rc) {
		DL_ERROR(rc, "Failed to init dfs write size histogram");
		return rc;
	}
	return 0;
}

void
dfs_metrics_init(dfs_t *dfs)
{
	uuid_t pool_uuid;
	uuid_t cont_uuid;
	char   root_name[D_TM_MAX_NAME_LEN];
	pid_t  pid       = getpid();
	size_t root_size = DFS_METRICS_SIZE + (D_TM_METRIC_SIZE * 3);
	int    tm_flags  = (D_TM_OPEN_OR_CREATE | D_TM_NO_SHMEM);
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
	rc = d_tm_init_with_name(d_tm_cli_pid_key(pid), root_size, tm_flags, root_name);
	if (rc != 0 && rc != -DER_ALREADY) {
		DL_ERROR(rc, "failed to init DFS metrics");
		goto error;
	}

	D_ALLOC_PTR(dfs->metrics);
	if (dfs->metrics == NULL) {
		D_ERROR("failed to alloc DFS metrics");
		rc = -DER_NOMEM;
		goto error;
	}

	SPRINTF_TM_PATH(root_name, pool_uuid, cont_uuid, DFS_METRICS_ROOT);
	rc = d_tm_add_ephemeral_dir(NULL, DFS_METRICS_SIZE, root_name);
	if (rc != 0) {
		DL_ERROR(rc, "failed to add DFS metrics dir");
		goto error;
	}

	rc = cont_stats_init(dfs->metrics, pool_uuid, cont_uuid);
	if (rc != 0)
		goto error;
	rc = op_stats_init(dfs->metrics, pool_uuid, cont_uuid);
	if (rc != 0)
		goto error;
	rc = file_stats_init(dfs->metrics, pool_uuid, cont_uuid);
	if (rc != 0)
		goto error;

	d_tm_record_timestamp(dfs->metrics->dm_mount_time);
	return;

error:
	DL_ERROR(rc, "failed to init DFS metrics");
	D_FREE(dfs->metrics);
	dfs->metrics = NULL;
}

static void
iter_dump(struct d_tm_context *ctx, struct d_tm_node_t *node, int level, char *path, int format,
	  int opt_fields, void *arg)
{
	if (strncmp(d_tm_get_name(ctx, node), DFS_DUMPTIME_METRIC, D_TM_MAX_NAME_LEN) == 0)
		d_tm_record_timestamp(node);
	d_tm_print_node(ctx, node, level, path, format, opt_fields, (FILE *)arg);
}

static const char *
get_process_name(void)
{
#if defined(_GNU_SOURCE)
	return program_invocation_name;
#else
	return "unknown";
#endif
}

int
csv_file_path(pid_t pid, const char *root_dir, char **file_dir, char **file_name)
{
	struct utsname name          = {0};
	time_t         now           = time(NULL);
	struct tm     *gm_now        = gmtime(&now);
	char           time_str[16]  = {0};
	char           dt_prefix[16] = {0};
	const char    *path_prefix;
	const char    *path_sep;
	char          *tmp_dir       = NULL;
	char          *tmp_name      = NULL;
	int            rc            = 0;

	if (file_dir == NULL || file_name == NULL)
		return -DER_INVAL;

	if (root_dir != NULL && root_dir[0] != '\0') {
		path_prefix = root_dir;
		path_sep    = (root_dir[strlen(root_dir) - 1] == '/') ? "" : "/";
	} else {
		path_prefix = "";
		path_sep    = "/";
	}

	/*
	 * NB: Default dc_jobid is $hostname-$pid, which is not very useful
	 * as an organizational scheme.
	 *
	 * If the jobid is not default, then the path should look like:
	 *	$root_dir/$yyyy/$mm/$dd/$hh/job/$jobid/$procname/$now-$hostname-$pid.csv
	 * If the jobid is default, then the path should look like:
	 * 	$root_dir/$yyyy/$mm/$dd/$hh/proc/$procname/$now-$hostname-$pid.csv
	 */

	if (uname(&name) != 0) {
		D_ERROR("unable to get uname: %s\n", strerror(errno));
		return -DER_MISC;
	}

	if (gm_now != NULL) {
		if (!strftime(dt_prefix, sizeof(dt_prefix), "%Y/%m/%d/%H/", gm_now))
			return -DER_MISC;
	} else {
		dt_prefix[0] = '\0';
	}

	if (dc_jobid_is_default(dc_jobid))
		D_ASPRINTF(tmp_dir, "%s%s%sproc/%s", path_prefix, path_sep, dt_prefix,
			   get_process_name());
	else
		D_ASPRINTF(tmp_dir, "%s%s%sjob/%s/%s", path_prefix, path_sep, dt_prefix, dc_jobid,
			   get_process_name());

	if (tmp_dir == NULL) {
		D_ERROR("failed to allocate memory for file dir\n");
		return -DER_NOMEM;
	}

	snprintf(time_str, sizeof(time_str), "%lu", now);
	D_ASPRINTF(tmp_name, "%s-%s-%d.csv", time_str, name.nodename, pid);
	if (tmp_name == NULL) {
		D_ERROR("failed to allocate memory for file name\n");
		D_GOTO(err_free, rc = -DER_NOMEM);
	}

	/* Check that the full path will not exceed system limits. +1 for the '/' separator. */
	if (strlen(tmp_dir) + 1 + strlen(tmp_name) >= PATH_MAX) {
		D_ERROR("csv file path too long\n");
		D_GOTO(err_free, rc = -DER_INVAL);
	}

	*file_dir  = tmp_dir;
	*file_name = tmp_name;
	return 0;

err_free:
	D_FREE(tmp_dir);
	D_FREE(tmp_name);
	return rc;
}

static int
get_metrics_csv(pid_t pid, char **dump_buf, size_t *dump_buf_sz)
{
	struct d_tm_context *ctx = NULL;
	struct d_tm_node_t  *root;
	FILE                *dump_file;
	uint32_t             filter;
	int                  rc = 0;

	ctx = d_tm_open(d_tm_cli_pid_key(pid));
	if (ctx == NULL) {
		D_ERROR("failed to connect to telemetry segment for pid %d\n", pid);
		return -DER_MISC;
	}

	root = d_tm_get_root(ctx);
	if (root == NULL) {
		D_ERROR("No metrics found for dump.\n");
		D_GOTO(close_ctx, rc = -DER_NONEXIST);
	}

	dump_file = open_memstream(dump_buf, dump_buf_sz);
	if (dump_file == NULL)
		D_GOTO(close_ctx, rc = -DER_NOMEM);

	filter = D_TM_COUNTER | D_TM_DURATION | D_TM_TIMESTAMP | D_TM_MEMINFO |
		 D_TM_TIMER_SNAPSHOT | D_TM_GAUGE | D_TM_STATS_GAUGE;

	d_tm_print_field_descriptors(0, dump_file);
	d_tm_iterate(ctx, root, 0, filter, NULL, D_TM_CSV, 0, iter_dump, dump_file);

	fclose(dump_file);

close_ctx:
	d_tm_close(&ctx);
	return rc;
}

int
write_tm_csv(const char *tm_pool, const char *tm_cont, const char *csv_file_dir,
	     const char *csv_file_name, const char *csv_buf, size_t csv_buf_sz)
{
	dfs_sys_t *dfs_sys = NULL;
	dfs_obj_t *obj;
	char      *full_path = NULL;
	int        rc;

	rc = dfs_init();
	if (rc != 0)
		return rc;

	rc = dfs_sys_connect(tm_pool, NULL, tm_cont, O_RDWR, 0, NULL, &dfs_sys);
	if (rc != 0) {
		D_ERROR("failed to connect to metrics container %s/%s", tm_pool, tm_cont);
		D_GOTO(out_fini, rc);
	}

	D_INFO("dumping telemetry to %s:%s%s/%s\n", tm_pool, tm_cont, csv_file_dir, csv_file_name);

	rc = dfs_sys_mkdir_p(dfs_sys, csv_file_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH, 0);
	if (rc != 0) {
		DL_ERROR(rc, "failed to mkdir_p %s", csv_file_dir);
		D_GOTO(out_disconnect, rc);
	}

	D_ASPRINTF(full_path, "%s/%s", csv_file_dir, csv_file_name);
	if (full_path == NULL)
		D_GOTO(out_disconnect, rc = -DER_NOMEM);

	rc = dfs_sys_open(dfs_sys, full_path, S_IFREG | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	if (rc != 0) {
		DL_ERROR(rc, "failed to open %s", full_path);
		D_GOTO(out_path, rc);
	}

	rc = dfs_sys_write(dfs_sys, obj, csv_buf, 0, &csv_buf_sz, NULL);
	if (rc != 0)
		DL_ERROR(rc, "failed to write to %s", full_path);

	int close_rc = dfs_sys_close(obj);
	if (close_rc != 0) {
		DL_ERROR(close_rc, "failed to close %s", full_path);
		if (rc == 0)
			rc = close_rc;
	}

out_path:
	D_FREE(full_path);
out_disconnect:
	dfs_sys_disconnect(dfs_sys);
out_fini:
	dfs_fini();
	return rc;
}

static int
dump_tm_container(const char *tm_pool, const char *tm_cont, const char *tm_root_dir)
{
	pid_t  pid           = getpid();
	char  *csv_file_dir  = NULL;
	char  *csv_file_name = NULL;
	char  *csv_buf       = NULL;
	size_t csv_buf_sz    = 0;
	int    rc;

	if (tm_pool == NULL || tm_cont == NULL)
		return -DER_INVAL;

	rc = get_metrics_csv(pid, &csv_buf, &csv_buf_sz);
	if (rc != 0)
		return rc;

	rc = csv_file_path(pid, tm_root_dir, &csv_file_dir, &csv_file_name);
	if (rc != 0 || csv_file_dir == NULL || csv_file_name == NULL) {
		D_ERROR("failed to get csv file path\n");
		D_GOTO(out_free, rc);
	}

	rc = write_tm_csv(tm_pool, tm_cont, csv_file_dir, csv_file_name, csv_buf, csv_buf_sz);

out_free:
	free(csv_buf);
	D_FREE(csv_file_dir);
	D_FREE(csv_file_name);

	return rc;
}

#define DUMP_ATTR_COUNT     3
#define DUMP_ATTR_VALUE_LEN PATH_MAX

char const *const dump_attr_names[DUMP_ATTR_COUNT] = {DAOS_CLIENT_METRICS_DUMP_POOL_ATTR,
						      DAOS_CLIENT_METRICS_DUMP_CONT_ATTR,
						      DAOS_CLIENT_METRICS_DUMP_DIR_ATTR};

#define DUMP_ATTR_POOL 0
#define DUMP_ATTR_CONT 1
#define DUMP_ATTR_DIR  2

static int
read_tm_dump_attrs(dfs_t *dfs, char **pool, char **cont, char **dir)
{
	size_t value_sizes[DUMP_ATTR_COUNT] = {0};
	size_t capacity[DUMP_ATTR_COUNT]    = {0};
	void  *buff_addrs[DUMP_ATTR_COUNT]  = {NULL};
	char  *tmp_pool                     = NULL;
	char  *tmp_cont                     = NULL;
	char  *tmp_dir                      = NULL;
	int    rc                           = 0;

	if (dfs == NULL || pool == NULL || cont == NULL)
		return -DER_INVAL;

	/* Pass 1: Query the sizes of the attributes first. */
	rc =
	    daos_cont_get_attr(dfs->coh, DUMP_ATTR_COUNT, dump_attr_names, NULL, value_sizes, NULL);
	if (rc == -DER_NONEXIST) {
		/* Not an error, just means the attributes are not set. */
		return 0;
	} else if (rc != 0) {
		DL_ERROR(rc, "Failed to query container metric attribute sizes");
		return rc;
	}

	/* Pass 2: Allocate buffers of the exact required size. */
	if (value_sizes[DUMP_ATTR_POOL] > 0) {
		D_ALLOC(tmp_pool, value_sizes[DUMP_ATTR_POOL] + 1);
		if (tmp_pool == NULL)
			D_GOTO(err, rc = -DER_NOMEM);
		buff_addrs[DUMP_ATTR_POOL] = tmp_pool;
		capacity[DUMP_ATTR_POOL]   = value_sizes[DUMP_ATTR_POOL] + 1;
	}
	if (value_sizes[DUMP_ATTR_CONT] > 0) {
		D_ALLOC(tmp_cont, value_sizes[DUMP_ATTR_CONT] + 1);
		if (tmp_cont == NULL)
			D_GOTO(err, rc = -DER_NOMEM);
		buff_addrs[DUMP_ATTR_CONT] = tmp_cont;
		capacity[DUMP_ATTR_CONT]   = value_sizes[DUMP_ATTR_CONT] + 1;
	}
	if (value_sizes[DUMP_ATTR_DIR] > 0) {
		D_ALLOC(tmp_dir, value_sizes[DUMP_ATTR_DIR] + 1);
		if (tmp_dir == NULL)
			D_GOTO(err, rc = -DER_NOMEM);
		buff_addrs[DUMP_ATTR_DIR] = tmp_dir;
		capacity[DUMP_ATTR_DIR]   = value_sizes[DUMP_ATTR_DIR] + 1;
	}

	/* Pass 2: Actually get the attribute data into the new buffers. */
	rc = daos_cont_get_attr(dfs->coh, DUMP_ATTR_COUNT, dump_attr_names, buff_addrs, capacity,
				NULL);
	if (rc != 0) {
		DL_ERROR(rc, "Failed to read container metric attributes");
		D_GOTO(err, rc);
	}

	*pool = tmp_pool;
	*cont = tmp_cont;
	*dir  = tmp_dir;
	return 0;

err:
	D_FREE(tmp_pool);
	D_FREE(tmp_cont);
	D_FREE(tmp_dir);
	return rc;
}

#define DEFAULT_DIR "/"

void
dfs_metrics_fini(dfs_t *dfs)
{
	char *tm_pool = NULL;
	char *tm_cont = NULL;
	char *tm_dir  = NULL;
	int   rc;

	if (dfs == NULL || dfs->metrics == NULL)
		return;

	rc = read_tm_dump_attrs(dfs, &tm_pool, &tm_cont, &tm_dir);
	if (rc != 0)
		goto out;

	if (tm_pool == NULL || tm_cont == NULL)
		goto out;

	if (tm_dir == NULL) {
		D_STRNDUP_S(tm_dir, DEFAULT_DIR);
		if (tm_dir == NULL)
			goto out;
	}

	rc = dump_tm_container(tm_pool, tm_cont, tm_dir);
	if (rc != 0)
		D_ERROR("failed to dump DFS metrics to %s/%s:%s", tm_pool, tm_cont, tm_dir);

out:
	D_FREE(tm_pool);
	D_FREE(tm_cont);
	D_FREE(tm_dir);
	D_FREE(dfs->metrics);
}

static bool
cont_attrs_set(dfs_t *dfs)
{
	char *tm_pool   = NULL;
	char *tm_cont   = NULL;
	char *tm_dir    = NULL;
	bool  attrs_set = false;
	int   rc;

	if (dfs == NULL)
		return false;

	rc = read_tm_dump_attrs(dfs, &tm_pool, &tm_cont, &tm_dir);
	if (rc != 0) {
		return false;
	}

	if (tm_pool != NULL && tm_cont != NULL)
		attrs_set = true;

	D_FREE(tm_pool);
	D_FREE(tm_cont);
	D_FREE(tm_dir);
	return attrs_set;
}

bool
dfs_metrics_should_init(dfs_t *dfs)
{
	return (daos_client_metric || cont_attrs_set(dfs));
}

bool
dfs_metrics_enabled(dfs_t *dfs)
{
	return dfs->metrics != NULL;
}
