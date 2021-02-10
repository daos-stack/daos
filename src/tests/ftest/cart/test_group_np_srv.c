/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This is a simple example of cart test_group server, running with no pmix.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <getopt.h>
#include <semaphore.h>

#include "tests_common.h"
#include "test_group_rpc.h"
#include "test_group_np_common.h"

/* Callback to process a SWIM message */
static void
swim_crt_event_cb(d_rank_t rank, enum crt_event_source src,
		  enum crt_event_type type, void *arg)
{
	char type_to_a[2];
	int max = MAX_SWIM_STATUSES;

	/* Example output for SWIM CRT_EVT_DEAD on rank #2:
	 *	 rank = 2, crt_event_source = 1, crt_event_type = 1
	 *
	 *		enum crt_event_type {
	 *			CRT_EVT_ALIVE,
	 *			CRT_EVT_DEAD,
	 *		};
	 *		enum crt_event_source {
	 *			CRT_EVS_UNKNOWN,
	 *			CRT_EVS_SWIM,
	 *		};
	 */

	D_DEBUG(DB_TEST, "Cart callback event: "
		"rank = %d, "
		"crt_event_source = %d, "
		"crt_event_type = %d\n",
		 rank, src, type);

	/* convert integer to string */
	snprintf(type_to_a, 2, "%d", type);

	if (swim_status_by_rank[rank] == NULL) {
		swim_status_by_rank[rank] = (char *)malloc(max * sizeof(char));
		strcpy(swim_status_by_rank[rank], "\0");
	}

	strcat(swim_status_by_rank[rank], type_to_a);

	/* Remove rank from context, so we stop sending swim RPCs to it. */
	if (src == CRT_EVS_SWIM && type == CRT_EVT_DEAD) {
		crt_group_rank_remove(NULL, rank);
	}
}

void
test_run(d_rank_t my_rank)
{
	crt_group_t		*grp = NULL;
	uint32_t		 grp_size;
	int			 i;
	int			 rc = 0;

	tc_srv_start_basic(test_g.t_local_group_name, &test_g.t_crt_ctx[0],
			   &test_g.t_tid[0], &grp, &grp_size, NULL);

	/* Register event callback after CaRT has initialized */
	if (test_g.t_register_swim_callback) {
		crt_register_event_cb(swim_crt_event_cb, NULL);
	}

	DBG_PRINT("Basic server started, group_size=%d\n", grp_size);
	rc = sem_init(&test_g.t_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	test_g.t_fault_attr_1000 = d_fault_attr_lookup(1000);
	test_g.t_fault_attr_5000 = d_fault_attr_lookup(5000);

	rc = crt_proto_register(&my_proto_fmt_test_group1);
	D_ASSERTF(rc == 0, "crt_proto_register() failed. rc: %d\n",
			rc);

	/* Do not delay shutdown for this server */
	tc_set_shutdown_delay(test_g.t_shutdown_delay);

	DBG_PRINT("Protocol registered\n");
	for (i = 1; i < test_g.t_srv_ctx_num; i++) {
		rc = crt_context_create(&test_g.t_crt_ctx[i]);
		D_ASSERTF(rc == 0, "crt_context_create() failed. rc: %d\n", rc);
		DBG_PRINT("Context %d created\n", i);

		rc = pthread_create(&test_g.t_tid[i], NULL, tc_progress_fn,
				    &test_g.t_crt_ctx[i]);
		D_ASSERTF(rc == 0, "pthread_create() failed. rc: %d\n", rc);
		DBG_PRINT("Progress thread %d started\n", i);
	}
	DBG_PRINT("Contexts created %d\n", test_g.t_srv_ctx_num);

	if (my_rank == 0) {
		rc = crt_group_config_save(NULL, true);
		D_ASSERTF(rc == 0,
			  "crt_group_config_save() failed. rc: %d\n", rc);
		DBG_PRINT("Group config file saved\n");
	}


	if (test_g.t_hold)
		sleep(test_g.t_hold_time);

	for (i = 0; i < test_g.t_srv_ctx_num; i++) {

		rc = pthread_join(test_g.t_tid[i], NULL);
		if (rc != 0)
			fprintf(stderr, "pthread_join failed. rc: %d\n", rc);
		D_DEBUG(DB_TEST, "joined progress thread.\n");
	}

	DBG_PRINT("Exiting server\n");
	rc = sem_destroy(&test_g.t_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	if (my_rank == 0) {
		rc = crt_group_config_remove(NULL);
		D_ASSERTF(rc == 0,
			  "crt_group_config_remove() failed. rc: %d\n", rc);
	}

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	d_log_fini();

	D_DEBUG(DB_TEST, "exiting.\n");
}

int main(int argc, char **argv)
{
	char		*env_self_rank;
	d_rank_t	 my_rank;
	int		 rc;

	rc = test_parse_args(argc, argv);

	if (rc != 0) {
		fprintf(stderr, "test_parse_args() failed, rc: %d.\n", rc);
		return rc;
	}

	env_self_rank = getenv("CRT_L_RANK");
	my_rank = atoi(env_self_rank);

	/* rank, num_attach_retries, is_server, assert_on_error */
	tc_test_init(my_rank, 20, true, true);

	DBG_PRINT("STARTING SERVER\n");
	test_run(my_rank);

	return rc;
}
