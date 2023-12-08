/*
 * (C) Copyright 2020-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <daos/common.h>
#include <daos/job.h>
#include <daos/tls.h>
#include <daos/metrics.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_consumer.h>
#include <gurt/telemetry_producer.h>

#define INIT_JOB_NUM 1024
bool daos_client_metric;
bool daos_client_metric_retain;

#define MAX_IDS_SIZE(num) (num * D_TM_METRIC_SIZE)
/* The client side metrics structure looks like
 * root/job_id/pid/....
 */
int
dc_tm_init(void)
{
	int   metrics_tag;
	pid_t pid;
	int   rc;

	d_getenv_bool(DAOS_CLIENT_METRICS_ENABLE, &daos_client_metric);
	if (!daos_client_metric)
		return 0;

	rc = dc_tls_key_create();
	if (rc)
		D_GOTO(out, rc);

	metrics_tag = D_TM_OPEN_OR_CREATE | D_TM_MULTIPLE_WRITER_LOCK;
	d_getenv_bool(DAOS_CLIENT_METRICS_RETAIN, &daos_client_metric_retain);
	if (daos_client_metric_retain)
		metrics_tag |= D_TM_RETAIN_SHMEM;
	else
		metrics_tag |= D_TM_RETAIN_SHMEM_IF_NON_EMPTY;

	rc = d_tm_init(DC_TM_JOB_ROOT_ID, MAX_IDS_SIZE(INIT_JOB_NUM), metrics_tag);
	if (rc != 0) {
		DL_ERROR(rc, "init job root id.");
		return rc;
	}

	D_INFO("INIT %s metrics\n", dc_jobid);
	rc = d_tm_add_ephemeral_dir(NULL, MAX_IDS_SIZE(INIT_JOB_NUM), "%s", dc_jobid);
	if (rc != 0 && rc != -DER_EXIST) {
		DL_ERROR(rc, "add metric %s failed", dc_jobid);
		D_GOTO(out, rc);
	}

	pid = getpid();
	D_INFO("INIT %s/%u metrics\n", dc_jobid, pid);
	rc = d_tm_add_ephemeral_dir(NULL, MAX_IDS_SIZE(INIT_JOB_NUM), "%s/%u", dc_jobid, pid);
	if (rc != 0) {
		DL_ERROR(rc, "add metric %s/%u failed.\n", dc_jobid, pid);
		D_GOTO(out, rc);
	}

out:
	if (rc)
		d_tm_fini();

	return rc;
}

static void
iter_dump(struct d_tm_context *ctx, struct d_tm_node_t *node, int level, char *path, int format,
	  int opt_fields, void *arg)
{
	d_tm_print_node(ctx, node, level, path, format, opt_fields, (FILE *)arg);
}

static int
dump_tm_file(const char *dump_path)
{
	struct d_tm_context *ctx;
	struct d_tm_node_t  *root;
	char                 dirname[D_TM_MAX_NAME_LEN] = {0};
	uint32_t             filter;
	FILE                *dump_file;
	int                  rc = 0;

	dump_file = fopen(dump_path, "w+");
	if (dump_file == NULL) {
		D_INFO("cannot open %s", dump_path);
		return -DER_INVAL;
	}

	filter = D_TM_COUNTER | D_TM_DURATION | D_TM_TIMESTAMP | D_TM_MEMINFO |
		 D_TM_TIMER_SNAPSHOT | D_TM_GAUGE | D_TM_STATS_GAUGE;

	ctx = d_tm_open(DC_TM_JOB_ROOT_ID);
	if (ctx == NULL)
		D_GOTO(close, rc = -DER_NOMEM);

	snprintf(dirname, sizeof(dirname), "%s/%u", dc_jobid, getpid());
	root = d_tm_find_metric(ctx, dirname);
	if (root == NULL) {
		printf("No metrics found at: '%s'\n", dirname);
		D_GOTO(close_ctx, rc = -DER_NONEXIST);
	}

	d_tm_print_field_descriptors(0, dump_file);

	d_tm_iterate(ctx, root, 0, filter, NULL, D_TM_CSV, 0, iter_dump, dump_file);

close_ctx:
	d_tm_close(&ctx);
close:
	fclose(dump_file);
	return rc;
}

void
dc_tm_fini()
{
	pid_t pid = getpid();
	char *dump_path;
	int   rc;

	if (!daos_client_metric)
		return;

	dump_path = getenv(DAOS_CLIENT_METRICS_DUMP_PATH);
	if (dump_path != NULL) {
		D_INFO("dump path is %s\n", dump_path);
		dump_tm_file(dump_path);
	}

	dc_tls_fini();
	dc_tls_key_delete();

	if (!daos_client_metric_retain) {
		rc = d_tm_del_ephemeral_dir("%s/%d", dc_jobid, pid);
		if (rc != 0)
			DL_ERROR(rc, "delete tm directory %s/%d.", dc_jobid, pid);

		rc = d_tm_try_del_ephemeral_dir("%s", dc_jobid);
		if (rc != 0)
			DL_ERROR(rc, "delete tm directory %s/%d.", dc_jobid, pid);
	}

	D_INFO("delete pid %s/%u\n", dc_jobid, pid);
	d_tm_fini();
}
