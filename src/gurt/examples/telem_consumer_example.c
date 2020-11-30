/*
 * (C) Copyright 2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/*
 * This file shows an example of using the telemetry API to consume metrics
 */

#include "gurt/common.h"
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
		  int filter, bool show_meta, int iteration)
{
	struct d_tm_nodeList_t	*nodelist = NULL;
	struct d_tm_nodeList_t	*head = NULL;
	struct timespec		tms;
	uint64_t		val;
	time_t			clk;
	char			*shortDesc;
	char			*longDesc;
	char			*name;
	int			rc;

	printf("\niteration: %d - %s/\n", iteration, dirname);
	rc = d_tm_list(&nodelist, shmem_root, dirname, filter);

	if (rc == D_TM_SUCCESS)
		head = nodelist;

	printf("There are %"PRIu64" objects in the unfiltered list\n",
	       d_tm_get_num_objects(shmem_root, dirname,
				    D_TM_DIRECTORY | D_TM_COUNTER |
				    D_TM_TIMESTAMP |
				    D_TM_TIMER_SNAPSHOT |
				    D_TM_DURATION | D_TM_GAUGE));

	printf("There are %"PRIu64" objects in the filtered list\n",
	       d_tm_get_num_objects(shmem_root, dirname,
				    D_TM_COUNTER | D_TM_TIMESTAMP));

	printf("There are %"PRIu64" metrics in the tree\n",
	       d_tm_count_metrics(shmem_root, root));

	while (nodelist) {
		name = d_tm_conv_ptr(shmem_root, nodelist->dtnl_node->dtn_name);
		if (name == NULL)
			return;

		switch (nodelist->dtnl_node->dtn_type) {
		case D_TM_DIRECTORY:
			fprintf(stdout, "%-20s\n", name);
			break;
		case D_TM_COUNTER:
			rc = d_tm_get_counter(&val, shmem_root,
					      nodelist->dtnl_node, NULL);
			if (rc != D_TM_SUCCESS) {
				printf("Error on counter read: %d\n", rc);
				break;
			}
			d_tm_print_counter(val, name, stdout);
			break;
		case D_TM_TIMESTAMP:
			rc = d_tm_get_timestamp(&clk, shmem_root,
						nodelist->dtnl_node, NULL);
			if (rc != D_TM_SUCCESS) {
				printf("Error on timestamp read: %d\n", rc);
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
				printf("Error on highres timer read: %d\n", rc);
				break;
			}
			d_tm_print_timer_snapshot(&tms, name,
						  nodelist->dtnl_node->dtn_type,
						  stdout);
			break;
		case D_TM_DURATION | D_TM_CLOCK_REALTIME:
		case D_TM_DURATION | D_TM_CLOCK_PROCESS_CPUTIME:
		case D_TM_DURATION | D_TM_CLOCK_THREAD_CPUTIME:
			rc = d_tm_get_duration(&tms, shmem_root,
					       nodelist->dtnl_node, NULL);
			if (rc != D_TM_SUCCESS) {
				printf("Error on duration read: %d\n", rc);
				break;
			}
			d_tm_print_duration(&tms, name,
					    nodelist->dtnl_node->dtn_type,
					    stdout);
			break;
		case D_TM_GAUGE:
			rc = d_tm_get_gauge(&val, shmem_root,
					    nodelist->dtnl_node, NULL);
			if (rc != D_TM_SUCCESS) {
				printf("Error on gauge read: %d\n", rc);
				break;
			}
			d_tm_print_gauge(val, name, stdout);
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
	bool			show_meta;
	int			simulated_rank = 0;
	int			iteration = 0;
	int			filter;

	if (argc < 2) {
		printf("Specify an integer that identifies the producer's "
		       "rank to monitor.\n");
		exit(0);
	}

	simulated_rank = atoi(argv[1]);
	printf("This simulated rank has ID: %d\n", simulated_rank);

	shmem_root = d_tm_get_shared_memory(simulated_rank);
	if (!shmem_root)
		goto failure;

	printf("Base address of client shared memory for rank %d is "
	       "0x%" PRIx64 "\n", simulated_rank, (uint64_t)shmem_root);

	root = d_tm_get_root(shmem_root);
	while (1) {
		d_tm_print_my_children(shmem_root, root, 0, stdout);

		sprintf(dirname, "src/gurt/examples/telem_producer_example.c"
			"/main");
		filter = (D_TM_DIRECTORY | D_TM_COUNTER | D_TM_TIMESTAMP |
			  D_TM_TIMER_SNAPSHOT | D_TM_DURATION | D_TM_GAUGE);
		show_meta = false;
		read_metrics(shmem_root, root, dirname, filter, show_meta,
			     iteration);

		filter = D_TM_COUNTER;
		show_meta = true;
		sprintf(dirname, "src/gurt/examples/telem_producer_example.c"
			"/manually added");
		read_metrics(shmem_root, root, dirname, filter, show_meta,
			     iteration);

		iteration++;
		sleep(1);
	}

	return 0;

failure:
	printf("Unable to attach to the shared memory for the rank: %d\n"
	       "Make sure to run the producer with the same rank to initialize "
	       "the shared memory and populate it with metrics.\n",
	       simulated_rank);
	return -1;
}
