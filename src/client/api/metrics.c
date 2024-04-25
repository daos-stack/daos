/*
 * (C) Copyright 2020-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(client)

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <sys/shm.h>
#include <daos/common.h>
#include <daos/job.h>
#include <daos/tls.h>
#include <daos/metrics.h>
#include <daos/mgmt.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_consumer.h>
#include <gurt/telemetry_producer.h>

#define INIT_JOB_NUM 1024
bool daos_client_metric        = false;
bool daos_client_metric_retain = false;

#define MAX_IDS_SIZE(num) (num * D_TM_METRIC_SIZE)
/* The client side metrics structure looks like
 * root/job_id/pid/....
 */

static int
shm_chown(key_t key, uid_t new_owner)
{
	struct shmid_ds shmid_ds;
	int             shmid;
	int             rc;

	rc = shmget(key, 0, 0);
	if (rc < 0) {
		D_ERROR("shmget(0x%x) failed: %s (%d)\n", key, strerror(errno), errno);
		return daos_errno2der(errno);
	}
	shmid = rc;

	rc = shmctl(shmid, IPC_STAT, &shmid_ds);
	if (rc < 0) {
		D_ERROR("shmctl(0x%x, IPC_STAT) failed: %s (%d)\n", shmid, strerror(errno), errno);
		return daos_errno2der(errno);
	}

	shmid_ds.shm_perm.uid = new_owner;
	rc                    = shmctl(shmid, IPC_SET, &shmid_ds);
	if (rc < 0) {
		D_ERROR("shmctl(0x%x, IPC_SET) failed: %s (%d)\n", shmid, strerror(errno), errno);
		return daos_errno2der(errno);
	}

	return 0;
}

static int
init_managed_root(const char *name, pid_t pid, int flags)
{
	uid_t agent_uid;
	key_t key;
	int   rc;

	/* Set the key based on our pid so that it can be easily found. */
	key = pid - D_TM_SHARED_MEMORY_KEY;
	rc  = d_tm_init_with_name(key, MAX_IDS_SIZE(INIT_JOB_NUM), flags, name);
	if (rc != 0) {
		DL_ERROR(rc, "failed to initialize root for %s.", name);
		return rc;
	}

	/* Request that the agent adds our segment into the tree. */
	rc = dc_mgmt_tm_register(NULL, dc_jobid, pid, &agent_uid);
	if (rc != 0) {
		DL_ERROR(rc, "client telemetry setup failed.");
		return rc;
	}

	/* Change ownership of the segment so that the agent can manage it. */
	D_INFO("setting shm segment 0x%x to be owned by uid %d\n", pid, agent_uid);
	rc = shm_chown(pid, agent_uid);
	if (rc != 0) {
		DL_ERROR(rc, "failed to chown shm segment.");
		return rc;
	}

	return 0;
}

int
dc_tm_init(void)
{
	struct d_tm_node_t *started_at;
	pid_t               pid = getpid();
	int                 metrics_tag;
	char                root_name[D_TM_MAX_NAME_LEN];
	int                 rc;

	d_getenv_bool(DAOS_CLIENT_METRICS_ENABLE, &daos_client_metric);
	if (!daos_client_metric && d_isenv_def(DAOS_CLIENT_METRICS_DUMP_PATH))
		daos_client_metric = true;

	if (!daos_client_metric)
		return 0;

	D_INFO("Setting up client telemetry for %s/%d\n", dc_jobid, pid);

	rc = dc_tls_key_create();
	if (rc)
		D_GOTO(out, rc);

	metrics_tag = D_TM_OPEN_OR_CREATE | D_TM_MULTIPLE_WRITER_LOCK;
	d_getenv_bool(DAOS_CLIENT_METRICS_RETAIN, &daos_client_metric_retain);
	if (daos_client_metric_retain)
		metrics_tag |= D_TM_RETAIN_SHMEM;

	snprintf(root_name, sizeof(root_name), "%d", pid);
	rc = init_managed_root(root_name, pid, metrics_tag);
	if (rc != 0) {
		DL_ERROR(rc, "failed to initialize client telemetry");
		D_GOTO(out, rc);
	}

	rc = d_tm_add_metric(&started_at, D_TM_TIMESTAMP, "Timestamp of client startup", NULL,
			     "started_at");
	if (rc != 0) {
		DL_ERROR(rc, "add metric started_at failed.");
		D_GOTO(out, rc);
	}

	d_tm_record_timestamp(started_at);
out:
	if (rc != 0) {
		daos_client_metric = false;
		d_tm_fini();
	}

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
	char *dump_path;
	int   rc;

	if (!daos_client_metric)
		return;

	rc = d_agetenv_str(&dump_path, DAOS_CLIENT_METRICS_DUMP_PATH);
	if (rc != 0)
		D_GOTO(out, rc);
	if (dump_path != NULL) {
		D_INFO("dump path is %s\n", dump_path);
		dump_tm_file(dump_path);
	}
	d_freeenv_str(&dump_path);

out:
	dc_tls_fini();
	dc_tls_key_delete();

	d_tm_fini();
}
