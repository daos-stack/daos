/*
 * (C) Copyright 2020-2021 Intel Corporation.
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
void read_metrics(uint64_t *shmem_root, struct d_tm_node_t *root, char *dirname,
		  int filter, bool show_meta)
{
	struct d_tm_nodeList_t	*nodelist = NULL;
	struct d_tm_nodeList_t	*head = NULL;
	struct d_tm_stats_t	stats = {0};
	struct d_tm_node_t	*node = NULL;
	struct timespec		tms;
	uint64_t		val;
	time_t			clk;
	char			*shortDesc;
	char			*longDesc;
	char			*name;
	int			rc;

	node = root;
	if (dirname != NULL) {
		if (strncmp(dirname, "/", D_TM_MAX_NAME_LEN) != 0) {
			node = d_tm_find_metric(shmem_root, dirname);
			if (node == NULL) {
				printf("Cannot find directory or metric: %s\n",
				       dirname);
				return;
			}
		}
	}

	rc = d_tm_list(&nodelist, shmem_root, node, filter);
	if (rc != D_TM_SUCCESS) {
		printf("d_tm_list failure: " DF_RC "\n", DP_RC(rc));
		return;
	}
	head = nodelist;

	printf("\nThere are %" PRIu64 " metrics in the directory %s\n",
	       d_tm_count_metrics(shmem_root, node, filter),
	       dirname ? dirname : "/");

	while (nodelist) {
		name = d_tm_conv_ptr(shmem_root, nodelist->dtnl_node->dtn_name);
		if (name == NULL)
			return;

		switch (nodelist->dtnl_node->dtn_type) {
		case D_TM_DIRECTORY:
			fprintf(stdout, "Directory: %-20s\n", name);
			break;
		case D_TM_COUNTER:
			rc = d_tm_get_counter(&val, shmem_root,
					      nodelist->dtnl_node, NULL);
			if (rc != D_TM_SUCCESS) {
				printf("Error on counter read: " DF_RC "\n",
				       DP_RC(rc));
				break;
			}
			d_tm_print_counter(val, name, stdout);
			break;
		case D_TM_TIMESTAMP:
			rc = d_tm_get_timestamp(&clk, shmem_root,
						nodelist->dtnl_node, NULL);
			if (rc != D_TM_SUCCESS) {
				printf("Error on timestamp read: " DF_RC "\n",
				       DP_RC(rc));
				break;
			}
			d_tm_print_timestamp(&clk, name, stdout);
			break;
		case (D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_REALTIME):
		case (D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_PROCESS_CPUTIME):
		case (D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_THREAD_CPUTIME):
			rc = d_tm_get_timer_snapshot(&tms, shmem_root,
						     nodelist->dtnl_node, NULL);
			if (rc != D_TM_SUCCESS) {
				printf("Error on highres timer read: " DF_RC
				       "\n", DP_RC(rc));
				break;
			}
			d_tm_print_timer_snapshot(&tms, name,
						  nodelist->dtnl_node->dtn_type,
						  stdout);
			break;
		case D_TM_DURATION | D_TM_CLOCK_REALTIME:
		case D_TM_DURATION | D_TM_CLOCK_PROCESS_CPUTIME:
		case D_TM_DURATION | D_TM_CLOCK_THREAD_CPUTIME:
			rc = d_tm_get_duration(&tms, &stats, shmem_root,
					       nodelist->dtnl_node, NULL);
			if (rc != D_TM_SUCCESS) {
				printf("Error on duration read: " DF_RC "\n",
				       DP_RC(rc));
				break;
			}
			d_tm_print_duration(&tms, &stats, name,
					    nodelist->dtnl_node->dtn_type,
					    stdout);
			break;
		case D_TM_GAUGE:
			rc = d_tm_get_gauge(&val, &stats, shmem_root,
					    nodelist->dtnl_node, NULL);
			if (rc != D_TM_SUCCESS) {
				printf("Error on gauge read: " DF_RC "\n",
				       DP_RC(rc));
				break;
			}
			d_tm_print_gauge(val, &stats, name, stdout);
			break;
		default:
			printf("Item: %s has unknown type: 0x%x\n",
			       name, nodelist->dtnl_node->dtn_type);
			break;
		}

		if (show_meta) {
			d_tm_get_metadata(&shortDesc, &longDesc, shmem_root,
					  nodelist->dtnl_node, NULL);
			printf("\tMetadata short description: %s\n"
			       "\tMetadata long description: %s\n",
			       shortDesc ? shortDesc : "N/A",
			       longDesc ? longDesc : "N/A");
			D_FREE_PTR(shortDesc);
			D_FREE_PTR(longDesc);
		}
		nodelist = nodelist->dtnl_next;
	}
	d_tm_list_free(head);
}

int
main(int argc, char **argv)
{
	struct d_tm_node_t	*root = NULL;
	uint64_t		*shmem_root = NULL;
	char			dirname[D_TM_MAX_NAME_LEN] = {0};
	bool			show_meta = false;
	int			simulated_srv_idx = 0;
	int			iteration = 0;
	int			filter;

	if (argc < 2) {
		printf("Specify an integer that identifies the producer's "
		       "server instance to monitor.\n");
		exit(0);
	}

	simulated_srv_idx = atoi(argv[1]);
	printf("This simulated server instance has ID: %d\n",
	       simulated_srv_idx);

	shmem_root = d_tm_get_shared_memory(simulated_srv_idx);
	if (!shmem_root)
		goto failure;

	root = d_tm_get_root(shmem_root);

	while (1) {
		printf("\niteration: %d\n", iteration);
		printf("Full directory tree from root node:\n");
		d_tm_print_my_children(shmem_root, root, 0, stdout);

		sprintf(dirname, "src/gurt/examples/telem_producer_example.c"
			"/main");
		filter = (D_TM_COUNTER | D_TM_TIMESTAMP | D_TM_TIMER_SNAPSHOT |
			  D_TM_DURATION | D_TM_GAUGE);
		show_meta = false;
		read_metrics(shmem_root, root, dirname, filter, show_meta);

		filter = D_TM_COUNTER;
		show_meta = true;
		sprintf(dirname, "src/gurt/examples/telem_producer_example.c"
			"/manually added");
		read_metrics(shmem_root, root, dirname, filter, show_meta);
		iteration++;
		sleep(1);
		printf("\n\n");
	}

	return 0;

failure:
	printf("Unable to attach to the shared memory for the server instance: "
	       "%d\n"
	       "Make sure to run the producer with the same server instance to "
	       "initialize the shared memory and populate it with metrics.\n",
	       simulated_srv_idx);
	return -1;
}
