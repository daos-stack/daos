/*
 * (C) Copyright 2021-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * This utility shows metrics from the specified I/O Engine
 */

#include <getopt.h>
#include <string.h>
#include <daos/metrics.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_consumer.h>

static void
print_usage(const char *prog_name)
{
	printf("Usage: %s [optional arguments]\n"
	       "\n"
	       "--srv_idx, -S\n"
	       "\tShow telemetry data from this I/O Engine local index "
	       "(default 0)\n"
	       "--path, -p\n"
	       "\tDisplay metrics at or below the specified path\n"
	       "\tDefault is root directory\n"
	       "--iterations, -i\n"
	       "\tSpecifies the number of iterations to show "
	       "(default is 1 iteration.  Set to 0 for continuous output)\n"
	       "--delay, -D\n"
	       "\tDelay in seconds between each iteration\n"
	       "\tDefault is 1 second\n"
	       "--csv, -C\n"
	       "\tDisplay data in CSV format\n"
	       "--meta, -M\n"
	       "\tDisplay associated metric metadata\n"
	       "--meminfo, -m\n"
	       "\tDisplay memory allocation metrics\n"
	       "--type, -T\n"
	       "\tDisplay metric type\n"
	       "--help, -h\n"
	       "\tThis help text\n\n"
	       "Customize the displayed data by specifying one or more "
	       "filters:\n"
	       "\tDefault is include everything\n\n"
	       "--counter, -c\n"
	       "\tInclude counters\n"
	       "--duration, -d\n"
	       "\tInclude durations\n"
	       "--timestamp, -t\n"
	       "\tInclude timestamps\n"
	       "--snapshot, -s\n"
	       "\tInclude timer snapshots\n"
	       "--gauge, -g\n"
	       "\tInclude gauges\n"
	       "--read, -r\n"
	       "\tInclude timestamp of when metric was read\n"
	       "--reset, -e\n"
	       "\tReset all metrics to zero\n"
	       "--jobid, -j\n"
	       "\tDisplay metrics of the specified job (if agent-managed)\n"
	       "--cli_pid, -P\n"
	       "\tDisplay metrics of the specified client process\n",
	       prog_name);
}

static int
process_metrics(int metric_id, char *dirname, int format, int filter, int extra_descriptors,
		int delay, int num_iter, d_tm_iter_cb_t iter_cb, void *arg)
{
	struct d_tm_node_t	*root = NULL;
	struct d_tm_node_t	*node = NULL;
	struct d_tm_context	*ctx = NULL;
	int                      iteration = 0;
	int                      rc        = 0;

	ctx = d_tm_open(metric_id);
	if (!ctx)
		D_GOTO(out, rc = 0);

	root = d_tm_get_root(ctx);
	if (!root)
		D_GOTO(out, rc = -DER_NONEXIST);

	if (strncmp(dirname, "/", D_TM_MAX_NAME_LEN) != 0) {
		node = d_tm_find_metric(ctx, dirname);
		if (node != NULL) {
			root = node;
		} else {
			printf("No metrics found at: '%s'\n", dirname);
			D_GOTO(out, rc = 0);
		}
	}

	if (format == D_TM_CSV)
		d_tm_print_field_descriptors(extra_descriptors, (FILE *)arg);

	while ((num_iter == 0) || (iteration < num_iter)) {
		d_tm_iterate(ctx, root, 0, filter, NULL, format, extra_descriptors, iter_cb, arg);
		iteration++;
		sleep(delay);
		if (format == D_TM_STANDARD)
			printf("\n\n");
	}

out:
	if (ctx != NULL)
		d_tm_close(&ctx);
	return rc;
}

static void
iter_print(struct d_tm_context *ctx, struct d_tm_node_t *node, int level, char *path, int format,
	   int opt_fields, void *arg)
{
	d_tm_print_node(ctx, node, level, path, format, opt_fields, (FILE *)arg);
}

static void
iter_reset(struct d_tm_context *ctx, struct d_tm_node_t *node, int level, char *path, int format,
	   int opt_fields, void *arg)
{
	d_tm_reset_node(ctx, node, level, path, format, opt_fields, (FILE *)arg);
}

int
main(int argc, char **argv)
{
	char			dirname[D_TM_MAX_NAME_LEN] = {0};
	char                    jobid[D_TM_MAX_NAME_LEN]   = {0};
	int                     cli_pid                    = 0;
	bool			show_meta = false;
	bool			show_when_read = false;
	bool			show_type = false;
	int                     srv_idx                    = 0;
	int			num_iter = 1;
	int			filter = 0;
	int			delay = 1;
	int			format = D_TM_STANDARD;
	int			opt;
	int			extra_descriptors = 0;
	d_tm_iter_cb_t          iter_cb           = NULL;
	int                     rc;

	sprintf(dirname, "/");

	/********************* Parse user arguments *********************/
	while (1) {
		static struct option long_options[] = {{"srv_idx", required_argument, NULL, 'S'},
						       {"counter", no_argument, NULL, 'c'},
						       {"csv", no_argument, NULL, 'C'},
						       {"duration", no_argument, NULL, 'd'},
						       {"timestamp", no_argument, NULL, 't'},
						       {"snapshot", no_argument, NULL, 's'},
						       {"gauge", no_argument, NULL, 'g'},
						       {"iterations", required_argument, NULL, 'i'},
						       {"path", required_argument, NULL, 'p'},
						       {"delay", required_argument, NULL, 'D'},
						       {"meta", no_argument, NULL, 'M'},
						       {"meminfo", no_argument, NULL, 'm'},
						       {"type", no_argument, NULL, 'T'},
						       {"read", no_argument, NULL, 'r'},
						       {"reset", no_argument, NULL, 'e'},
						       {"jobid", required_argument, NULL, 'j'},
						       {"cli_pid", required_argument, NULL, 'P'},
						       {"help", no_argument, NULL, 'h'},
						       {NULL, 0, NULL, 0}};

		opt = getopt_long_only(argc, argv, "S:cCdtsgi:p:D:MmTrj:P:he", long_options, NULL);
		if (opt == -1)
			break;

		switch (opt) {
		case 'S':
			srv_idx = atoi(optarg);
			break;
		case 'c':
			filter |= D_TM_COUNTER;
			break;
		case 'C':
			format = D_TM_CSV;
			break;
		case 'd':
			filter |= D_TM_DURATION;
			break;
		case 't':
			filter |= D_TM_TIMESTAMP;
			break;
		case 's':
			filter |= D_TM_TIMER_SNAPSHOT;
			break;
		case 'g':
			filter |= D_TM_GAUGE | D_TM_STATS_GAUGE;
			break;
		case 'i':
			num_iter = atoi(optarg);
			break;
		case 'p':
			snprintf(dirname, sizeof(dirname), "%s", optarg);
			break;
		case 'M':
			show_meta = true;
			break;
		case 'm':
			filter |= D_TM_MEMINFO;
			break;
		case 'T':
			show_type = true;
			break;
		case 'r':
			show_when_read = true;
			break;
		case 'D':
			delay = atoi(optarg);
			break;
		case 'e':
			iter_cb = iter_reset;
			break;
		case 'j':
			snprintf(jobid, sizeof(jobid), "%s", optarg);
			break;
		case 'P':
			cli_pid = atoi(optarg);
			break;
		case 'h':
		case '?':
		default:
			print_usage(argv[0]);
			exit(0);
		}
	}

	if (iter_cb == NULL)
		iter_cb = iter_print;

	if (filter == 0)
		filter = D_TM_COUNTER | D_TM_DURATION | D_TM_TIMESTAMP | D_TM_MEMINFO |
			 D_TM_TIMER_SNAPSHOT | D_TM_GAUGE | D_TM_STATS_GAUGE;

	if (show_when_read)
		extra_descriptors |= D_TM_INCLUDE_TIMESTAMP;
	if (show_meta)
		extra_descriptors |= D_TM_INCLUDE_METADATA;
	if (show_type)
		extra_descriptors |= D_TM_INCLUDE_TYPE;

	if (format == D_TM_CSV)
		filter &= ~D_TM_DIRECTORY;
	else
		filter |= D_TM_DIRECTORY;

	if (strlen(jobid) > 0) {
		srv_idx = DC_TM_JOB_ROOT_ID;
		snprintf(dirname, sizeof(dirname), "%s", jobid);
	} else if (cli_pid > 0) {
		srv_idx = d_tm_cli_pid_key(cli_pid);
	}

	/* fetch metrics from server side */
	rc = process_metrics(srv_idx, dirname, format, filter, extra_descriptors, delay, num_iter,
			     iter_cb, stdout);
	if (rc)
		printf("Unable to attach to the shared memory for the server index: %d"
		       "\nMake sure to run the I/O Engine with the same index to "
		       "initialize the shared memory and populate it with metrics.\n"
		       "Verify user/group settings match those that started the I/O "
		       "Engine.\n",
		       srv_idx);
	return rc != 0 ? -1 : 0;
}
