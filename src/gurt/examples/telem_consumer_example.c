/*
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * This file shows an example of using the telemetry API to consume metrics
 */

#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"

/**
 * An example that shows how metrics are read.
 * The caller provides the directory name, and this function performs the
 * directory listing of all items found there.  It shows how to iterate through
 * a d_tm_nodeList to access all of the metrics.
 * This example doesn't _do_ anything with the data it reads other than print
 * it out.
 */
void read_metrics(struct d_tm_context *ctx, struct d_tm_node_t *root,
		  char *dirname, int filter, bool show_meta)
{
	struct d_tm_nodeList_t	*nodelist = NULL;
	struct d_tm_nodeList_t	*head = NULL;
	struct d_tm_stats_t	stats = {0};
	struct d_tm_node_t	*node = NULL;
	struct timespec		tms;
	uint64_t		val;
	time_t			clk;
	char			*desc;
	char			*units;
	char			*name;
	int			options = 0;
	int			rc;

	node = root;
	if (dirname != NULL) {
		if (strncmp(dirname, "/", D_TM_MAX_NAME_LEN) != 0) {
			node = d_tm_find_metric(ctx, dirname);
			if (node == NULL) {
				printf("Cannot find directory or metric: %s\n",
				       dirname);
				return;
			}
		}
	}

	rc = d_tm_list(ctx, &nodelist, node, filter);
	if (rc != DER_SUCCESS) {
		printf("d_tm_list failure: " DF_RC "\n", DP_RC(rc));
		return;
	}
	head = nodelist;

	printf("\nThere are %" PRIu64 " metrics in the directory %s\n",
	       d_tm_count_metrics(ctx, node, filter),
	       dirname ? dirname : "/");

	while (nodelist) {
		node = nodelist->dtnl_node;

		name = d_tm_get_name(ctx, node);
		if (name == NULL)
			return;

		d_tm_get_metadata(ctx, &desc, &units, node);

		switch (node->dtn_type) {
		case D_TM_DIRECTORY:
			fprintf(stdout, "Directory: %-20s\n", name);
			break;
		case D_TM_COUNTER:
			rc = d_tm_get_counter(ctx, &val, node);
			if (rc != DER_SUCCESS) {
				printf("Error on counter read: " DF_RC "\n",
				       DP_RC(rc));
				break;
			}
			d_tm_print_counter(val, name, D_TM_STANDARD, units,
					   options, stdout);
			break;
		case D_TM_TIMESTAMP:
			rc = d_tm_get_timestamp(ctx, &clk, node);
			if (rc != DER_SUCCESS) {
				printf("Error on timestamp read: " DF_RC "\n",
				       DP_RC(rc));
				break;
			}
			d_tm_print_timestamp(&clk, name, D_TM_STANDARD, options,
					     stdout);
			break;
		case (D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_REALTIME):
		case (D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_PROCESS_CPUTIME):
		case (D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_THREAD_CPUTIME):
			rc = d_tm_get_timer_snapshot(ctx, &tms, node);
			if (rc != DER_SUCCESS) {
				printf("Error on highres timer read: " DF_RC
				       "\n", DP_RC(rc));
				break;
			}
			d_tm_print_timer_snapshot(&tms, name,
						  node->dtn_type,
						  D_TM_STANDARD, options,
						  stdout);
			break;
		case D_TM_DURATION | D_TM_CLOCK_REALTIME:
		case D_TM_DURATION | D_TM_CLOCK_PROCESS_CPUTIME:
		case D_TM_DURATION | D_TM_CLOCK_THREAD_CPUTIME:
			rc = d_tm_get_duration(ctx, &tms, &stats, node);
			if (rc != DER_SUCCESS) {
				printf("Error on duration read: " DF_RC "\n",
				       DP_RC(rc));
				break;
			}
			d_tm_print_duration(&tms, &stats, name,
					    node->dtn_type,
					    D_TM_STANDARD, options, stdout);
			break;
		case D_TM_GAUGE:
			rc = d_tm_get_gauge(ctx, &val, &stats, node);
			if (rc != DER_SUCCESS) {
				printf("Error on gauge read: " DF_RC "\n",
				       DP_RC(rc));
				break;
			}
			d_tm_print_gauge(val, &stats, name, D_TM_STANDARD,
					 units, options, stdout);
			break;
		default:
			printf("Item: %s has unknown type: 0x%x\n",
			       name, node->dtn_type);
			break;
		}

		if (show_meta)
			d_tm_print_metadata(desc, units, D_TM_STANDARD,
					    stdout);
		D_FREE(desc);
		D_FREE(units);

		if (node->dtn_type != D_TM_DIRECTORY)
			printf("\n");

		nodelist = nodelist->dtnl_next;
	}
	d_tm_list_free(head);
}

int
main(int argc, char **argv)
{
	struct d_tm_node_t	*root = NULL;
	struct d_tm_context	*ctx = NULL;
	char			dirname[D_TM_MAX_NAME_LEN] = {0};
	bool			show_meta = false;
	int			simulated_srv_idx = 0;
	int			filter;

	if (argc < 2) {
		printf("Specify an integer that identifies the producer's "
		       "server instance to monitor.\n");
		exit(0);
	}

	simulated_srv_idx = atoi(argv[1]);
	printf("This simulated server instance has ID: %d\n",
	       simulated_srv_idx);

	ctx = d_tm_open(simulated_srv_idx);
	if (ctx == NULL)
		goto failure;

	root = d_tm_get_root(ctx);

	printf("Full directory tree from root node:\n");
	filter = (D_TM_COUNTER | D_TM_TIMESTAMP | D_TM_TIMER_SNAPSHOT |
		  D_TM_DURATION | D_TM_GAUGE | D_TM_DIRECTORY);
	show_meta = true;
	d_tm_print_my_children(ctx, root, 0, filter, NULL,
			       D_TM_STANDARD, D_TM_INCLUDE_METADATA, stdout);

	sprintf(dirname, "manually added");
	filter = (D_TM_COUNTER | D_TM_TIMESTAMP | D_TM_TIMER_SNAPSHOT |
			D_TM_DURATION | D_TM_GAUGE);
	show_meta = false;
	read_metrics(ctx, root, dirname, filter, show_meta);

	filter = D_TM_COUNTER;
	show_meta = true;
	read_metrics(ctx, root, dirname, filter, show_meta);

	d_tm_close(&ctx);
	return 0;

failure:
	printf("Unable to attach to the shared memory for the server instance: "
	       "%d\n"
	       "Make sure to run the producer with the same server instance to "
	       "initialize the shared memory and populate it with metrics.\n",
	       simulated_srv_idx);
	return -1;
}
