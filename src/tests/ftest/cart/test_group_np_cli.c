/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This is a simple example of cart test_group client running with no pmix.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <getopt.h>
#include <semaphore.h>
#include <ctype.h>

#include "crt_utils.h"
#include "test_group_rpc.h"
#include "test_group_np_common.h"
#include "test_group_np_common_cli.h"

static void
send_rpc_swim_check(crt_endpoint_t server_ep, crt_rpc_t *rpc_req)
{
	struct test_swim_status_in	*rpc_req_input;

	int rc = crt_req_create(test_g.t_crt_ctx[0], &server_ep,
				TEST_OPC_SWIM_STATUS, &rpc_req);
	D_ASSERTF(rc == 0 && rpc_req != NULL,
		  "crt_req_create() failed. "
		  "rc: %d, rpc_req: %p\n", rc, rpc_req);

	rpc_req_input = crt_req_get(rpc_req);
	D_ASSERTF(rpc_req_input != NULL, "crt_req_get() failed."
		  " rpc_req_input: %p\n", rpc_req_input);

	/* Set rank and expected swim status based on CLI options */
	rpc_req_input->rank = test_g.t_verify_swim_status.rank;
	rpc_req_input->exp_status = test_g.t_verify_swim_status.swim_status;

	rc = crt_req_send(rpc_req, client_cb_common, NULL);
	D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);

	crtu_sem_timedwait(&test_g.t_token_to_proceed, 61, __LINE__);
}

static void
send_rpc_disable_swim(crt_endpoint_t server_ep, crt_rpc_t *rpc_req)
{
	struct test_disable_swim_in	*rpc_req_input;

	int rc = crt_req_create(test_g.t_crt_ctx[0], &server_ep,
				TEST_OPC_DISABLE_SWIM, &rpc_req);
	D_ASSERTF(rc == 0 && rpc_req != NULL,
		  "crt_req_create() failed. "
		  "rc: %d, rpc_req: %p\n", rc, rpc_req);

	rpc_req_input = crt_req_get(rpc_req);
	D_ASSERTF(rpc_req_input != NULL, "crt_req_get() failed."
		  " rpc_req_input: %p\n", rpc_req_input);

	/* Set rank and expected swim status based on CLI options */
	rpc_req_input->rank = server_ep.ep_rank;

	rc = crt_req_send(rpc_req, client_cb_common, NULL);
	D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);

	crtu_sem_timedwait(&test_g.t_token_to_proceed, 61, __LINE__);
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
	int			 i;
	int			 rc = 0;
	uint32_t		*_cg_ranks;
	int			 _cg_num_ranks;
	char			msg[256];

	if (test_g.t_skip_init) {
		DBG_PRINT("Skipping init stage.\n");

	} else {
		if (test_g.t_save_cfg) {
			rc = crt_group_config_path_set(test_g.t_cfg_path);
			D_ASSERTF(rc == 0,
				  "crt_group_config_path_set failed %d\n", rc);
		}

		crtu_cli_start_basic(test_g.t_local_group_name,
				     test_g.t_remote_group_name,
				     &grp, &rank_list, &test_g.t_crt_ctx[0],
				     &test_g.t_tid[0], test_g.t_srv_ctx_num,
				     test_g.t_use_cfg, NULL, test_g.t_use_daos_agent_env);

		rc = sem_init(&test_g.t_token_to_proceed, 0, 0);
		D_ASSERTF(rc == 0, "sem_init() failed.\n");

		/* register RPCs */
		rc = crt_proto_register(&my_proto_fmt_test_group1);
		D_ASSERTF(rc == 0, "crt_proto_register() failed. rc: %d\n",
			  rc);

		/* Process the --rank option, e.g., --rank 1,2-4 */
		if (test_g.cg_num_ranks > 0) {
			_cg_ranks = (uint32_t *)test_g.cg_ranks;
			_cg_num_ranks = test_g.cg_num_ranks;

			/* free up rank list from crtu_cli_start_basic */
			if (rank_list != NULL) {
				/* avoid checkpatch warning */
				d_rank_list_free(rank_list);
			}
			rank_list = uint32_array_to_rank_list(_cg_ranks,
							      _cg_num_ranks);
		}

		rc = crtu_wait_for_ranks(test_g.t_crt_ctx[0],
					 grp,
					 rank_list,
					 test_g.t_srv_ctx_num - 1,
					 test_g.t_srv_ctx_num,
					 60,
					 test_g.t_wait_ranks_time);
		D_ASSERTF(rc == 0, "wait_for_ranks() failed; rc=%d\n", rc);
	}

	if (test_g.t_init_only) {
		DBG_PRINT("Init only. Returning now.\n");
		D_GOTO(clean_up, rc = 0);
	}

	test_g.t_fault_attr_1000 = d_fault_attr_lookup(1000);
	test_g.t_fault_attr_5000 = d_fault_attr_lookup(5000);

	if (!test_g.t_shut_only && !test_g.t_skip_check_in &&
	    (rank_list != NULL)) {

		for (i = 0; i < rank_list->rl_nr; i++) {
			rank = rank_list->rl_ranks[i];

			snprintf(msg, sizeof(msg), "Sending message to %d",
				 rank);
			crtu_log_msg(test_g.t_crt_ctx[0], grp, rank, msg);

			for (tag = 0; tag < test_g.t_srv_ctx_num; tag++) {
				DBG_PRINT("Sending rpc to %d:%d\n", rank, tag);
				send_rpc_check_in(grp, rank, tag);
			}
		}

		for (i = 0; i < rank_list->rl_nr; i++) {
			for (tag = 0; tag < test_g.t_srv_ctx_num; tag++) {
				crtu_sem_timedwait(&test_g.t_token_to_proceed,
						   61, __LINE__);
			}
		}
	}

	server_ep.ep_grp = grp;

	if ((test_g.t_verify_swim_status.rank >= 0) &&
	    (rank_list != NULL)) {
		/* Check swim status on all (remaining) ranks */
		for (i = 0; i < rank_list->rl_nr; i++) {
			server_ep.ep_rank = rank_list->rl_ranks[i];
			send_rpc_swim_check(server_ep, rpc_req);
		}
	}

	/* Disable swim */
	if (test_g.t_disable_swim &&
	    (rank_list != NULL)) {

		crt_rank_abort_all(NULL);

		for (i = 0; i < rank_list->rl_nr; i++) {
			DBG_PRINT("Disabling swim on rank %d.\n",
				  rank_list->rl_ranks[i]);
			server_ep.ep_rank = rank_list->rl_ranks[i];
			send_rpc_disable_swim(server_ep, rpc_req);
		}
	}

	if ((test_g.t_skip_shutdown) || (rank_list == NULL)) {
		DBG_PRINT("Skipping shutdown stage. Rank_list %p\n",
			  rank_list);
	} else {
		/* Shutdown all ranks or those specified by --rank option */
		for (i = 0; i < rank_list->rl_nr; i++) {
			DBG_PRINT("Shutting down rank %d.\n",
				  rank_list->rl_ranks[i]);
			server_ep.ep_rank = rank_list->rl_ranks[i];
			send_rpc_shutdown(server_ep, rpc_req);
		}
	}

clean_up:
	if (rank_list != NULL) {
		/* avoid checkpatch warning */
		d_rank_list_free(rank_list);
	}
	rank_list = NULL;

	if (test_g.t_save_cfg) {
		DBG_PRINT("Detach Group %p\n", grp);
		rc = crt_group_detach(grp);
		D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	} else {
		DBG_PRINT("Destroy Group %p\n", grp);
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
	D_DEBUG(DB_TEST, "exiting.\n");

	if (test_g.t_hold)
		sleep(test_g.t_hold_time);

	d_log_fini();
}

int main(int argc, char **argv)
{
	int		 rc;

	rc = test_parse_args(argc, argv);
	if (rc != 0) {
		fprintf(stderr, "test_parse_args() failed, rc: %d.\n", rc);
		return rc;
	}

	if (D_ON_VALGRIND) {
		test_g.t_hold_time *= 4;
		test_g.t_wait_ranks_time *= 4;
	}

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(0, 40, false, true);

	test_run();

	return rc;
}
