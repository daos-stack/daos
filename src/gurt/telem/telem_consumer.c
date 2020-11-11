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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdbool.h>
#include "gurt/common.h"
#include "gurt/telemetry.h"

/*
 * An example that shows how metrics are read.
 * The caller provides the directory name, and this function performs the
 * directory listing of all items found there.  It shows how to iterate through
 * a d_tm_nodeList to access all of the metrics.
 * This example doesn't _do_ anything with the data it reads other than print
 * it out.
 */
void readMetrics(uint64_t *shmemRoot, struct d_tm_node_t *root, char *dirname,
		 int iteration)
{
	uint64_t val;
	struct d_tm_nodeList_t *nodelist = NULL;
	struct d_tm_nodeList_t *head = NULL;
	char *shortDesc;
	char *longDesc;
	char *name;
	time_t clk;
	char tmp[64];
	int len = 0;
	int rc;
	struct timespec tms;

	printf("----------------------------------------\n");

	printf("iteration: %d - %s/\n", iteration, dirname);
	rc = d_tm_list(&nodelist, shmemRoot, dirname,
		       D_TM_DIRECTORY | D_TM_COUNTER | D_TM_TIMESTAMP |
		       D_TM_HIGH_RES_TIMER | D_TM_DURATION |
		       D_TM_GAUGE);

	if (rc == D_TM_SUCCESS)
		head = nodelist;

	printf("There are %"PRIu64" objects in the unfiltered list\n",
	       d_tm_get_num_objects(shmemRoot, dirname,
				    D_TM_DIRECTORY | D_TM_COUNTER |
				    D_TM_TIMESTAMP |
				    D_TM_HIGH_RES_TIMER |
				    D_TM_DURATION | D_TM_GAUGE));

	printf("There are %"PRIu64" objects in the filtered list\n",
	       d_tm_get_num_objects(shmemRoot, dirname,
				    D_TM_COUNTER | D_TM_TIMESTAMP));

	printf("There are %"PRIu64" metrics in the tree\n",
	       d_tm_count_metrics(shmemRoot, root));

	while (nodelist) {
		name = d_tm_convert_char_ptr(shmemRoot, nodelist->node->name);
		switch (nodelist->node->d_tm_type) {
		case D_TM_DIRECTORY:
			printf("\tDIRECTORY: %s has %"PRIu64
			       " metrics underneath it\n",
			       name ? name : "Unavailable",
			d_tm_count_metrics(shmemRoot, nodelist->node));
			break;
		case D_TM_COUNTER:
			rc = d_tm_get_counter(&val, shmemRoot,
					      nodelist->node, NULL);
			if (rc == D_TM_SUCCESS) {
				d_tm_get_metadata(&shortDesc, &longDesc,
						  shmemRoot,
						  nodelist->node, NULL);
				printf("\tCOUNTER: %s %" PRIu64
				       " With metadata: %s and %s\n",
				       name ? name : "Unavailable", val,
				       shortDesc, longDesc);
				free(shortDesc);
				free(longDesc);
			} else
				printf("Error on counter read: %d\n", rc);
			break;
		case D_TM_TIMESTAMP:
			rc = d_tm_get_timestamp(&clk, shmemRoot,
						nodelist->node, NULL);
			if (rc == D_TM_SUCCESS) {
				strncpy(tmp, ctime(&clk), sizeof(tmp) - 1);
				len = strlen(tmp);
				if (len) {
					if (tmp[len - 1] == '\n') {
						tmp[len - 1] = 0;
					}
				}
				printf("\tTIMESTAMP %s: %s\n", name ? name :
				       "Unavailable", tmp);
			} else
				printf("Error on timestamp read: %d\n", rc);
			break;
		case D_TM_HIGH_RES_TIMER:
			rc = d_tm_get_highres_timer(&tms, shmemRoot,
						    nodelist->node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tHIGH RES TIMER %s: %lds, "
				       "%ldns\n", name ? name :
				       "Unavailable", tms.tv_sec,
				       tms.tv_nsec);
			} else
				printf("Error on highres timer read: %d\n", rc);
			break;
		case D_TM_DURATION | D_TM_CLOCK_REALTIME:
			rc = d_tm_get_duration(&tms, shmemRoot,
					       nodelist->node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tD_TM_CLOCK_REALTIME DURATION"
				       " %s: %.9fs\n", name ? name :
				       "Unavailable", tms.tv_sec +
				       tms.tv_nsec / 1e9);
			} else
				printf("Error on duration read: %d\n", rc);
			break;
		case D_TM_DURATION | D_TM_CLOCK_PROCESS_CPUTIME:
			rc = d_tm_get_duration(&tms, shmemRoot,
					       nodelist->node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tD_TM_CLOCK_PROCESS_CPUTIME "
				       "DURATION %s: %.9fs\n",
				       name ? name : "Unavailable",
				       tms.tv_sec + tms.tv_nsec / 1e9);
			} else
				printf("Error on duration read: %d\n", rc);
			break;
		case D_TM_DURATION | D_TM_CLOCK_THREAD_CPUTIME:
			rc = d_tm_get_duration(&tms, shmemRoot,
					       nodelist->node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tD_TM_CLOCK_THREAD_CPUTIME "
				       "DURATION %s: %.9fs\n",
				       name ? name : "Unavailable",
				       tms.tv_sec + tms.tv_nsec / 1e9);
			} else
				printf("Error on duration read: %d\n", rc);
			break;
		case D_TM_DURATION:
			rc = d_tm_get_duration(&tms, shmemRoot,
					       nodelist->node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tDEFAULT REALTIME DURATION %s:"
				       " %.9fs\n", name ? name :
				       "Unavailable", tms.tv_sec +
				       tms.tv_nsec / 1e9);
			} else
				printf("Error on duration read: %d\n", rc);
			break;
		case D_TM_GAUGE:
			rc = d_tm_get_gauge(&val, shmemRoot,
					    nodelist->node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tGAUGE: %s %" PRIu64 "\n",
				       name ? name :
				       "Unavailable", val);
			} else
				printf("Error on gauge read: %d\n", rc);
			break;
		default:
			printf("\tUNKNOWN!: %s Type: %d\n",
			       name ? name : "Unavailable",
			       nodelist->node->d_tm_type);
			break;
		}
		nodelist = nodelist->next;
	}
	d_tm_list_free(head);
}

int
main(int argc, char **argv)
{
	int simulatedRank = 0;
	uint64_t *shmemRoot = NULL;
	struct d_tm_node_t *root = NULL;
	char dirname[1024] = {0};
	int iteration = 0;

	if (argc < 2) {
		printf("Specify an integer that identifies the producer's "
		       "rank to monitor.\n");
		exit(0);
	}

	simulatedRank = atoi(argv[1]);
	printf("This simulatedRank has ID: %d\n", simulatedRank);

	shmemRoot = (uint64_t *)d_tm_get_shared_memory(simulatedRank);
	if (!shmemRoot)
		goto failure;

	printf("Base address of client shared memory for rank %d is "
	       "0x%" PRIx64 "\n", simulatedRank, (uint64_t)shmemRoot);

	root = d_tm_get_root(shmemRoot);

	sprintf(dirname, "src/gurt/telem/telem_producer.c/main");

	while (1) {
		d_tm_print_my_children(shmemRoot, root, 0);
		readMetrics(shmemRoot, root, dirname, iteration++);
		sleep(1);
	}

	return 0;
failure:
	return -1;
}
