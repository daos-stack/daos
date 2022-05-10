/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <getopt.h>
#include <semaphore.h>

#include "crt_utils.h"
#include "test_group_rpc.h"
#include "test_group_np_common.h"
#include "test_group_np_common_cli.h"

#define NUM_ATTACH_RETRIES	10
#define TEST_NO_TIMEOUT_BASE    0x010000000
#define TEST_NO_TIMEOUT_VER     0

static void
ping_delay_reply(crt_group_t *remote_group, int rank, int tag, uint32_t delay)
{
	crt_rpc_t			*rpc_req = NULL;
	struct crt_test_ping_delay_in	*rpc_req_input;
	crt_endpoint_t			 server_ep = {0};
	char				*buffer;
	int				 rc;

	server_ep.ep_grp = remote_group;
	server_ep.ep_rank = rank;
	server_ep.ep_tag = tag;
	rc = crt_req_create(test_g.t_crt_ctx[0], &server_ep,
			    TEST_OPC_PING_DELAY, &rpc_req);
	D_ASSERTF(rc == 0 && rpc_req != NULL, "crt_req_create() failed,"
		  " rc: %d rpc_req: %p\n", rc, rpc_req);

	rpc_req_input = crt_req_get(rpc_req);
	D_ASSERTF(rpc_req_input != NULL, "crt_req_get() failed."
		  " rpc_req_input: %p\n", rpc_req_input);
	D_ALLOC(buffer, 256);
	D_ASSERTF(buffer != NULL, "Cannot allocate memory.\n");
	snprintf(buffer,  256, "Guest %d", test_g.t_my_rank);
	rpc_req_input->name = buffer;
	rpc_req_input->age = 21;
	rpc_req_input->days = 7;
	rpc_req_input->delay = delay;
	D_DEBUG(DB_TEST, "client(rank %d) sending ping rpc with tag "
		"%d, name: %s, age: %d, days: %d, delay: %u.\n",
		test_g.t_my_rank, server_ep.ep_tag, rpc_req_input->name,
		rpc_req_input->age, rpc_req_input->days, rpc_req_input->delay);

	/* send an rpc, print out reply */
	rc = crt_req_send(rpc_req, client_cb_common, NULL);
	D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);

	/* buffer will be freed in the call back function client_cb_common */
}

void
test_run(void)
{
	crt_group_t		*grp = NULL;
	d_rank_list_t		*rank_list = NULL;
	d_rank_t		 rank;
	int			 tag;
	crt_endpoint_t		 server_ep = {0};
	crt_rpc_t		*rpc_req = NULL;
	uint32_t		 delay = 22;
	int			 i;
	int			 rc = 0;

	fprintf(stderr, "local group: %s remote group: %s\n",
		test_g.t_local_group_name, test_g.t_remote_group_name);

	if (test_g.t_save_cfg) {
		rc = crt_group_config_path_set(test_g.t_cfg_path);
		D_ASSERTF(rc == 0, "crt_group_config_path_set failed %d\n", rc);
	}

	crtu_cli_start_basic(test_g.t_local_group_name,
			     test_g.t_remote_group_name,
			     &grp, &rank_list, &test_g.t_crt_ctx[0],
			     &test_g.t_tid[0], test_g.t_srv_ctx_num,
			     test_g.t_use_cfg, NULL,
			     test_g.t_use_daos_agent_env);

	rc = sem_init(&test_g.t_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	rc = crt_group_rank(NULL, &test_g.t_my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank() failed. rc: %d\n", rc);

	/* register RPCs */
	rc = crt_proto_register(&my_proto_fmt_test_group1);
	D_ASSERTF(rc == 0, "crt_proto_register() failed. rc: %d\n", rc);

	rc = crtu_wait_for_ranks(test_g.t_crt_ctx[0], grp, rank_list,
				 test_g.t_srv_ctx_num - 1,
				 test_g.t_srv_ctx_num, 60, 120);
	D_ASSERTF(rc == 0, "wait_for_ranks() failed; rc=%d\n", rc);

	crt_group_size(test_g.t_remote_group, &test_g.t_remote_group_size);
	fprintf(stderr, "size of %s is %d\n", test_g.t_remote_group_name,
		test_g.t_remote_group_size);

	for (i = 0; i < rank_list->rl_nr; i++) {
		rank = rank_list->rl_ranks[i];

		for (tag = 0; tag < test_g.t_srv_ctx_num; tag++) {
			DBG_PRINT("Sending rpc to %d:%d\n", rank, tag);
			ping_delay_reply(grp, rank, tag, delay);
		}
	}

	for (i = 0; i < rank_list->rl_nr; i++) {
		crtu_sem_timedwait(&test_g.t_token_to_proceed, 61, __LINE__);
	}

	if (test_g.t_my_rank == 0) {
		/* client rank 0 tells all servers to shut down */
		for (i = 0; i < rank_list->rl_nr; i++) {
			DBG_PRINT("Shutting down rank %d.\n",
				  rank_list->rl_ranks[i]);
			rank = rank_list->rl_ranks[i];

			server_ep.ep_grp = grp;
			server_ep.ep_rank = rank;

			rc = crt_req_create(test_g.t_crt_ctx[0], &server_ep,
					    TEST_OPC_SHUTDOWN, &rpc_req);
			D_ASSERTF(rc == 0 && rpc_req != NULL,
				  "crt_req_create() failed. "
				  "rc: %d, rpc_req: %p\n", rc, rpc_req);
			rc = crt_req_send(rpc_req, client_cb_common, NULL);
			D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n",
				  rc);

			crtu_sem_timedwait(&test_g.t_token_to_proceed, 61,
					   __LINE__);
			send_rpc_shutdown(server_ep, rpc_req);
		}
	}

	d_rank_list_free(rank_list);
	rank_list = NULL;

	if (test_g.t_save_cfg) {
		rc = crt_group_detach(grp);
		D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	} else {
		rc = crt_group_view_destroy(grp);
		D_ASSERTF(rc == 0,
			  "crt_group_view_destroy() failed; rc=%d\n", rc);
	}

	crtu_progress_stop();

	rc = pthread_join(test_g.t_tid[0], NULL);
	if (rc != 0)
		fprintf(stderr, "pthread_join failed. rc: %d\n", rc);
	D_DEBUG(DB_TEST, "joined progress thread.\n");

	rc = sem_destroy(&test_g.t_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	d_log_fini();
	D_DEBUG(DB_TEST, "exiting.\n");
}

int main(int argc, char **argv)
{
	int	rc;

	rc = test_parse_args(argc, argv);
	if (rc != 0) {
		fprintf(stderr, "test_parse_args() failed, rc: %d.\n",
			rc);
		return rc;
	}

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(0, 40, false, true);

	test_run();

	return rc;
}
