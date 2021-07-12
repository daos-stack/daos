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
	int maxlen;
	char swim_state_str[2];

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

	swim_state_str[0] = type + '0';
	swim_state_str[1] = 0;

	maxlen = MAX_SWIM_STATUSES - strlen(swim_state_str);
	if (strlen(swim_seq_by_rank[rank]) < maxlen)
		strcat(swim_seq_by_rank[rank], swim_state_str);

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

	if (test_g.t_load_cfg == false) {
		tc_srv_start_basic(test_g.t_local_group_name, &test_g.t_crt_ctx[0],
				   &test_g.t_tid[0], &grp, &grp_size, NULL);
	} else {
		char *grp_cfg_file;

		printf("ALEXMOD: Loading cfg by hand\n");
		crt_context_t *crt_ctx;
		pthread_t *progress_thread;

		crt_ctx = &test_g.t_crt_ctx[0];
		progress_thread = &test_g.t_tid[0];

		rc = crt_init(test_g.t_local_group_name, CRT_FLAG_BIT_SERVER |
				CRT_FLAG_BIT_AUTO_SWIM_DISABLE);

		D_ASSERTF(rc == 0, "crt_init() failed\n");

		grp = crt_group_lookup(NULL);


		rc = crt_rank_self_set(my_rank);
		D_ASSERTF(rc == 0, "crt_rank_self_set(%d) failed; rc=%d\n",
			  my_rank, rc);

		rc = crt_context_create(crt_ctx);
		D_ASSERTF(rc == 0, "crt_context_create() failed; rc=%d\n", rc);

		rc = pthread_create(progress_thread, NULL, tc_progress_fn, crt_ctx);
		D_ASSERTF(rc == 0, "pthread_create() failed; rc=%d\n", rc);

		grp_cfg_file = getenv("CRT_L_GRP_CFG");

		if (my_rank == 2) {
			FILE *f;

			f = fopen(test_g.t_relaunch_file, "w+");
			if (!f) {
				D_ERROR("Failed to open '%s' for writing\n", test_g.t_relaunch_file);
				assert(0);
			}

			fprintf(f, "kill -9 %d\n", getpid());
			fprintf(f, "kill -9 %d\n", getpid());
			fprintf(f, "sleep 5\n");
			fprintf(f, "export CRT_L_GRP_CFG=%s\n", grp_cfg_file);
			fprintf(f, "export CRT_L_RANK=%d\n", my_rank);
			fprintf(f, "export OFI_INTERFACE=%s\n", getenv("OFI_INTERFACE"));
			fprintf(f, "export OFI_DOMAIN=%s\n", getenv("OFI_DOMAIN"));
			fprintf(f, "export OFI_PORT=%s\n", getenv("OFI_PORT"));
			fprintf(f, "export CRT_PHY_ADDR_STR=\"%s\"\n", getenv("CRT_PHY_ADDR_STR"));
			fprintf(f, "install/lib/daos/TESTING/tests/test_group_np_srv --name selftest_srv_grp --cfg_path=. -l\n");
			fclose(f);
			DBG_PRINT("Run sh '%s' to kill and restart rank=2\n", test_g.t_relaunch_file);
		}
		rc = tc_load_group_from_file(grp_cfg_file, crt_ctx[0], grp, my_rank, false);
		D_ASSERTF(rc == 0, "tc_load_group_from_file failed\n");

		rc = crt_group_size(NULL, &grp_size);
		D_ASSERTF(rc == 0, "crt_group_size() failed; rc=%d\n", rc);
	}

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

	if (my_rank == 0) {
		while (1) {
			crt_rpc_t *rpc_req;
			crt_endpoint_t server_ep;

			server_ep.ep_rank = 2;
			server_ep.ep_tag = 0;
			server_ep.ep_grp = NULL;
			DBG_PRINT("Sending PING rpc to rank 2\n");
			rc = crt_req_create(test_g.t_crt_ctx[0], &server_ep,
						CRT_PROTO_OPC(TEST_GROUP_BASE,
								TEST_GROUP_VER, 0),
						&rpc_req);


			D_ASSERTF(rc == 0, "crt_req_create() failed\n");
			rc = crt_req_send(rpc_req, ping_cb_common, NULL);

			tc_sem_timedwait(&test_g.t_token_to_proceed, 61, __LINE__);
			DBG_PRINT("(run sh %s to restart server)\n\n", test_g.t_relaunch_file);
			sleep(1);
		}
	}

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

	if (test_g.t_register_swim_callback)
		tc_test_swim_enable(true);

	DBG_PRINT("STARTING SERVER\n");
	test_run(my_rank);

	return rc;
}
