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

#define SPRINTF_CONT_PATH(buf, cont_uuid, path)                                                    \
	snprintf(buf, sizeof(buf), "container/" DF_UUIDF "/%s", DP_UUID(cont_uuid), path);

#define ADD_STAT_METRIC(name, ...)                                                                 \
	SPRINTF_CONT_PATH(tmp_path, cont_uuid, DFS_METRICS_ROOT "/ops/" #name);                    \
	rc = d_tm_add_metric(&metrics->dm_op_stats[i], D_TM_COUNTER, "Count of " #name " calls",   \
			     "calls", tmp_path);                                                   \
	if (rc != 0) {                                                                             \
		DL_ERROR(rc, "failed to create " #name " counter");                                \
		return;                                                                            \
	}                                                                                          \
	i++;

static void
op_stats_init(struct dfs_metrics *metrics, uuid_t cont_uuid)
{
	char tmp_path[D_TM_MAX_NAME_LEN] = {0};
	int  i                           = 0;
	int  rc;

	if (metrics == NULL)
		return;

	D_FOREACH_DFS_OP_STAT(ADD_STAT_METRIC);
}

static void
cont_stats_init(struct dfs_metrics *metrics, uuid_t cont_uuid)
{
	char tmp_path[D_TM_MAX_NAME_LEN] = {0};
	int  rc                          = 0;

	if (metrics == NULL)
		return;

	SPRINTF_CONT_PATH(tmp_path, cont_uuid, "mount_time");
	rc = d_tm_add_metric(&metrics->dm_mount_time, D_TM_TIMESTAMP, "container mount time", NULL,
			     tmp_path);
	if (rc != 0)
		DL_ERROR(rc, "failed to create mount_time timestamp");

	SPRINTF_CONT_PATH(tmp_path, cont_uuid, DFS_DUMPTIME_METRIC);
	rc = d_tm_add_metric(&metrics->dm_dump_time, D_TM_TIMESTAMP, "container dump time", NULL,
			     tmp_path);
	if (rc != 0)
		DL_ERROR(rc, "failed to create dump_time timestamp");
}

static void
file_stats_init(struct dfs_metrics *metrics, uuid_t cont_uuid)
{
	char tmp_path[D_TM_MAX_NAME_LEN] = {0};
	int  rc                          = 0;

	if (metrics == NULL)
		return;

	SPRINTF_CONT_PATH(tmp_path, cont_uuid, DFS_METRICS_ROOT "/read_bytes");
	rc = d_tm_add_metric(&metrics->dm_read_bytes, D_TM_STATS_GAUGE, "dfs read bytes", "bytes",
			     tmp_path);
	if (rc != 0)
		DL_ERROR(rc, "failed to create dfs read_bytes counter");
	rc =
	    d_tm_init_histogram(metrics->dm_read_bytes, tmp_path, NR_SIZE_BUCKETS, 256, 2, "bytes");
	if (rc)
		DL_ERROR(rc, "Failed to init dfs read size histogram");

	SPRINTF_CONT_PATH(tmp_path, cont_uuid, DFS_METRICS_ROOT "/write_bytes");
	rc = d_tm_add_metric(&metrics->dm_write_bytes, D_TM_STATS_GAUGE, "dfs write bytes", "bytes",
			     tmp_path);
	if (rc != 0)
		DL_ERROR(rc, "failed to create dfs write_bytes counter");
	rc = d_tm_init_histogram(metrics->dm_write_bytes, tmp_path, NR_SIZE_BUCKETS, 256, 2,
				 "bytes");
	if (rc)
		DL_ERROR(rc, "Failed to init dfs write size histogram");
}

void
dfs_metrics_init(dfs_t *dfs)
{
	uuid_t cont_uuid;
	char   root_name[D_TM_MAX_NAME_LEN];
	pid_t  pid       = getpid();
	size_t root_size = DFS_METRICS_SIZE + (D_TM_METRIC_SIZE * 3);
	int    rc;

	if (dfs == NULL)
		return;

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

	SPRINTF_CONT_PATH(root_name, cont_uuid, DFS_METRICS_ROOT);
	rc = d_tm_add_ephemeral_dir(NULL, DFS_METRICS_SIZE, root_name);
	if (rc != 0) {
		DL_ERROR(rc, "failed to add DFS metrics dir");
		goto error;
	}

	cont_stats_init(dfs->metrics, cont_uuid);
	op_stats_init(dfs->metrics, cont_uuid);
	file_stats_init(dfs->metrics, cont_uuid);

	d_tm_record_timestamp(dfs->metrics->dm_mount_time);
	return;

error:
	if (dfs->metrics != NULL)
		D_FREE(dfs->metrics);
}

static void
iter_dump(struct d_tm_context *ctx, struct d_tm_node_t *node, int level, char *path, int format,
	  int opt_fields, void *arg)
{
	if (strncmp(d_tm_get_name(ctx, node), DFS_DUMPTIME_METRIC, D_TM_MAX_NAME_LEN) == 0)
		d_tm_record_timestamp(node);
	d_tm_print_node(ctx, node, level, path, format, opt_fields, (FILE *)arg);
}

static int
dfs_write_buf(dfs_sys_t *dfs_sys, const char *parent_dir, const char *file_name, const char *buf,
	      size_t buf_size)
{
	dfs_obj_t *obj       = NULL;
	size_t     written   = buf_size;
	char      *full_path = NULL;
	int        rc        = 0;
	int        close_rc  = 0;

	rc = dfs_sys_mkdir_p(dfs_sys, parent_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH, 0);
	if (rc != 0) {
		DL_ERROR(rc, "failed to mkdir_p %s", parent_dir);
		return rc;
	}

	D_ASPRINTF(full_path, "%s/%s", parent_dir, file_name);
	if (full_path == NULL)
		return ENOMEM;

	rc = dfs_sys_open(dfs_sys, full_path, S_IFREG | S_IWUSR | S_IRUSR,
			  O_RDWR | O_CREAT | O_EXCL, 0, 0, NULL, &obj);
	D_FREE(full_path);
	if (rc != 0) {
		DL_ERROR(rc, "failed to open %s/%s", parent_dir, file_name);
		return rc;
	}

	rc = dfs_sys_write(dfs_sys, obj, buf, 0, &written, NULL);
	if (rc != 0)
		DL_ERROR(rc, "failed to write %s", file_name);

	if (written != buf_size)
		D_ERROR("written (%lu) != buf_size (%lu)\n", written, buf_size);

	close_rc = dfs_sys_close(obj);
	if (close_rc != 0)
		DL_ERROR(close_rc, "failed to close %s", file_name);

	return rc;
}

static int
dump_tm_container(const char *tm_pool, const char *tm_cont, const char *tm_dir)
{
	struct d_tm_context *ctx;
	struct d_tm_node_t  *root;
	dfs_sys_t           *dfs_sys         = NULL;
	char                 time_str[16]    = {0};
	char                 file_name[1024] = {0};
	char                *dump_buf;
	size_t               dump_buf_sz = 1024 * 128; // FIXME: calculate this?
	pid_t                pid         = getpid();
	time_t               now         = time(NULL);
	uint32_t             filter;
	FILE                *dump_file;
	int                  rc = 0;

	if (tm_pool == NULL || tm_cont == NULL)
		return -DER_INVAL;

	rc = dfs_init();
	if (rc != 0)
		return rc;

	rc = dfs_sys_connect(tm_pool, NULL, tm_cont, O_RDWR, 0, NULL, &dfs_sys);
	if (rc != 0) {
		DL_ERROR(rc, "failed to connect to metrics container %s/%s", tm_pool, tm_cont);
		D_GOTO(close_fini, rc);
	}

	snprintf(time_str, sizeof(time_str), ".%lu", now);
	snprintf(file_name, sizeof(file_name), "%s-%d%s.csv", dc_jobid, pid, time_str);
	if (rc > sizeof(file_name)) {
		D_ERROR("dump directory and/or jobid too long\n");
		D_GOTO(close_fini, rc = -DER_INVAL);
	}

	filter = D_TM_COUNTER | D_TM_DURATION | D_TM_TIMESTAMP | D_TM_MEMINFO |
		 D_TM_TIMER_SNAPSHOT | D_TM_GAUGE | D_TM_STATS_GAUGE;

	ctx = d_tm_open(d_tm_cli_pid_key(pid));
	if (ctx == NULL) {
		D_ERROR("failed to connect to telemetry segment for pid %d\n", pid);
		D_GOTO(close_disconnect, rc = -DER_MISC);
	}

	root = d_tm_get_root(ctx);
	if (root == NULL) {
		D_ERROR("No metrics found for dump.\n");
		D_GOTO(close_ctx, rc = -DER_NONEXIST);
	}

	D_ALLOC(dump_buf, dump_buf_sz);
	if (dump_buf == NULL)
		D_GOTO(close_ctx, rc = -DER_NOMEM);

	dump_file = fmemopen(dump_buf, dump_buf_sz, "w+");
	if (dump_file == NULL) {
		D_ERROR("failed to fmemopen() buf\n");
		D_GOTO(close_dealloc, rc = -DER_NOMEM);
	}

	D_INFO("dumping telemetry to %s/%s:%s/%s\n", tm_pool, tm_cont, tm_dir, file_name);
	d_tm_print_field_descriptors(0, dump_file);
	d_tm_iterate(ctx, root, 0, filter, NULL, D_TM_CSV, 0, iter_dump, dump_file);

	/* get actual amount written */
	fseek(dump_file, 0, SEEK_END);
	dump_buf_sz = ftell(dump_file);
	fclose(dump_file);

	rc = dfs_write_buf(dfs_sys, tm_dir, file_name, dump_buf, dump_buf_sz);
	if (rc != 0)
		DL_ERROR(rc, "failed to dump metrics buffer to container");

close_dealloc:
	D_FREE(dump_buf);
close_ctx:
	d_tm_close(&ctx);
close_disconnect:
	dfs_sys_disconnect(dfs_sys);
close_fini:
	dfs_fini();
	return rc;
}

#define DUMP_ATTR_COUNT     3
#define DUMP_ATTR_VALUE_LEN PATH_MAX

char const *const dump_attr_names[DUMP_ATTR_COUNT] = {
    "client-metrics-dump-pool", "client-metrics-dump-cont", "client-metrics-dump-dir"};

#define DUMP_ATTR_POOL 0
#define DUMP_ATTR_CONT 1
#define DUMP_ATTR_DIR  2

static int
read_tm_dump_attrs(dfs_t *dfs, char **pool, char **cont, char **dir)
{
	size_t sizes[DUMP_ATTR_COUNT];
	char  *buff;
	char  *buff_addrs[DUMP_ATTR_COUNT];
	char  *tmp_pool = NULL;
	char  *tmp_cont = NULL;
	char  *tmp_dir  = NULL;
	int    i;
	int    rc = 0;

	if (dfs == NULL || pool == NULL || cont == NULL)
		return -DER_INVAL;

	D_ALLOC(buff, DUMP_ATTR_VALUE_LEN * DUMP_ATTR_COUNT);
	if (buff == NULL)
		return ENOMEM;

	for (i = 0; i < DUMP_ATTR_COUNT; i++) {
		sizes[i]      = DUMP_ATTR_VALUE_LEN - 1;
		buff_addrs[i] = buff + i * DUMP_ATTR_VALUE_LEN;
	}

	rc = daos_cont_get_attr(dfs->coh, DUMP_ATTR_COUNT, dump_attr_names,
				(void *const *)buff_addrs, sizes, NULL);
	if (rc == -DER_NONEXIST) {
		D_GOTO(out, rc = 0);
	} else if (rc != 0) {
		DL_ERROR(rc, "Failed to read container metric attributes");
		D_GOTO(out, rc);
	}

	for (i = 0; i < DUMP_ATTR_COUNT; i++) {
		if (sizes[i] == 0)
			continue;

		/* ensure that the strings are nul-terminated */
		if (*(buff_addrs[i] + sizes[i] - 1) == '\0')
			sizes[i]--;
		else
			*(buff_addrs[i] + sizes[i]) = '\0';

		switch (i) {
		case DUMP_ATTR_POOL:
			D_STRNDUP(tmp_pool, buff_addrs[i], sizes[i]);
			if (tmp_pool == NULL)
				D_GOTO(err, rc = -DER_NOMEM);
			break;
		case DUMP_ATTR_CONT:
			D_STRNDUP(tmp_cont, buff_addrs[i], sizes[i]);
			if (tmp_cont == NULL)
				D_GOTO(err, rc = -DER_NOMEM);
			break;
		case DUMP_ATTR_DIR:
			D_STRNDUP(tmp_dir, buff_addrs[i], sizes[i]);
			if (tmp_dir == NULL)
				D_GOTO(err, rc = -DER_NOMEM);
			break;
		}
	}
	if (tmp_pool != NULL)
		*pool = tmp_pool;
	if (tmp_cont != NULL)
		*cont = tmp_cont;
	if (tmp_dir != NULL)
		*dir = tmp_dir;

	D_GOTO(out, rc = 0);

err:
	D_FREE(tmp_pool);
	D_FREE(tmp_cont);
	D_FREE(tmp_dir);
out:
	D_FREE(buff);
	return rc;
}

static void
read_dump_params(dfs_t *dfs, char **pool, char **cont, char **dir)
{
	int   rc       = 0;
	char *env_pool = NULL;
	char *env_cont = NULL;
	char *env_dir  = NULL;

	if (dfs == NULL || pool == NULL || cont == NULL)
		return;

	/* read the attributes first, if they exist */
	rc = read_tm_dump_attrs(dfs, pool, cont, dir);
	if (rc != 0)
		goto err;

	/* allow environment variables to override, if present */
	if (d_isenv_def(DAOS_CLIENT_METRICS_DUMP_POOL)) {
		rc = d_agetenv_str(&env_pool, DAOS_CLIENT_METRICS_DUMP_POOL);
		if (rc != 0)
			goto out;
		if (*pool != NULL)
			D_FREE(*pool);
		D_STRNDUP(*pool, env_pool, strnlen(env_pool, PATH_MAX));
		if (*pool == NULL)
			goto err;
	}
	if (d_isenv_def(DAOS_CLIENT_METRICS_DUMP_CONT)) {
		rc = d_agetenv_str(&env_cont, DAOS_CLIENT_METRICS_DUMP_CONT);
		if (rc != 0)
			goto out;
		if (*cont != NULL)
			D_FREE(*cont);
		D_STRNDUP(*cont, env_cont, strnlen(env_cont, PATH_MAX));
		if (*cont == NULL)
			goto err;
	}
	if (d_isenv_def(DAOS_CLIENT_METRICS_DUMP_DIR)) {
		rc = d_agetenv_str(&env_dir, DAOS_CLIENT_METRICS_DUMP_DIR);
		if (rc != 0)
			goto out;
		if (*dir != NULL)
			D_FREE(*dir);
		D_STRNDUP(*dir, env_dir, strnlen(env_dir, PATH_MAX));
		if (*dir == NULL)
			goto err;
	}
	goto out;

err:
	D_FREE(*pool);
	D_FREE(*cont);
	D_FREE(*dir);
out:
	d_freeenv_str(&env_pool);
	d_freeenv_str(&env_cont);
	d_freeenv_str(&env_dir);
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

	read_dump_params(dfs, &tm_pool, &tm_cont, &tm_dir);

	if (tm_pool == NULL || tm_cont == NULL)
		goto out;

	if (tm_dir == NULL) {
		D_STRNDUP_S(tm_dir, DEFAULT_DIR);
		if (tm_dir == NULL)
			goto out;
	}

	rc = dump_tm_container(tm_pool, tm_cont, tm_dir);
	if (rc != 0)
		DL_ERROR(rc, "failed to dump DFS metrics to %s/%s:%s", tm_pool, tm_cont, tm_dir);

out:
	D_FREE(tm_pool);
	D_FREE(tm_cont);
	D_FREE(tm_dir);
	D_FREE(dfs->metrics);
}