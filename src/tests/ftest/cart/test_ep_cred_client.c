/*
 * (C) Copyright 2018-2020 Intel Corporation.
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
#include <semaphore.h>

#include "tests_common.h"
#include "test_ep_cred_common.h"

static int resp_count;
static int sent_count;

static void
rpc_handle_shutdown_reply(const struct crt_cb_info *info)
{
	DBG_PRINT("Shutdown response handler called\n");
	sem_post(&test.tg_token_to_proceed);
}

static void
rpc_handle_reply(const struct crt_cb_info *info)
{
	D_ASSERTF(info->cci_rc == 0, "rpc response failed. rc: %d\n",
		  info->cci_rc);
	resp_count++;
	DBG_PRINT("Response count=%d\n", resp_count);

	if (resp_count == sent_count) {
		DBG_PRINT("received all expected replies\n");
		sem_post(&test.tg_token_to_proceed);
	}
}

static void
rpc_handle_ping_front_q(const struct crt_cb_info *info)
{
	DBG_PRINT("Response from front queued rpc\n");
	D_ASSERTF(info->cci_rc == 0, "rpc response failed. rc: %d\n",
		  info->cci_rc);
	sem_post(&test.tg_queue_front_token);
}

static void
test_run()
{
	crt_group_t		*grp = NULL;
	d_rank_list_t		*rank_list = NULL;
	crt_rpc_t		*rpc = NULL;
	crt_endpoint_t		 ep = {0};
	int			 rc;
	int			 i;
	struct ping_in		*input;
	crt_init_options_t	 opt = {0};

	DBG_PRINT("local group: %s remote group: %s\n",
		   test.tg_local_group_name, test.tg_remote_group_name);

	if (test.tg_save_cfg) {
		rc = crt_group_config_path_set(test.tg_cfg_path);
		D_ASSERTF(rc == 0, "crt_group_config_path_set failed %d\n", rc);
	}

	opt.cio_use_credits = 1;
	opt.cio_ep_credits = test.tg_credits;

	DBG_PRINT("Number of credits: %d Number of burst: %d\n",
		   test.tg_credits, test.tg_burst_count);

	tc_cli_start_basic(test.tg_local_group_name,
			   test.tg_remote_group_name,
			   &grp, &rank_list, &test.tg_crt_ctx,
			   &test.tg_tid, true, test.tg_save_cfg, &opt);

	rc = sem_init(&test.tg_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	rc = sem_init(&test.tg_queue_front_token, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	rc = crt_group_rank(NULL, &test.tg_my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank() failed. rc: %d\n", rc);

	rc = crt_proto_register(&my_proto_fmt_0);
	D_ASSERTF(rc == 0, "registration failed with rc: %d\n", rc);

	ep.ep_grp = grp;
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	DBG_PRINT("Sending %d rpcs\n", test.tg_burst_count);

	for (i = 0; i < test.tg_burst_count; i++) {
		rc = crt_req_create(test.tg_crt_ctx, &ep, OPC_PING, &rpc);
		D_ASSERTF(rc == 0, "crt_req_create() failed. rc: %d\n", rc);

		input = crt_req_get(rpc);

		if (i == 0) {
			/* If we test 'send to front of queue' flag we
			 * want to increase response delay of first rpc to 3
			 * seconds in order to allow sufficient queue up
			 */
			if (test.tg_send_queue_front)
				input->pi_delay = 3;
			else
				input->pi_delay = 1;
		} else {
			input->pi_delay = 0;
		}

		rc = crt_req_send(rpc, rpc_handle_reply, NULL);
		D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);
		sent_count++;
	}

	/* Send RPC message to be in front of the queue. This option
	 * should only be used when test.tg_burst_count is large while
	 * test.tg_credits is small, allowing sufficient queue up of
	 * rpcs
	 */
	if (test.tg_send_queue_front) {
		rc = crt_req_create(test.tg_crt_ctx, &ep, OPC_PING_FRONT,
				&rpc);
		D_ASSERTF(rc == 0, "crt_req_create() failed. rc: %d\n", rc);

		rc = crt_req_send(rpc, rpc_handle_ping_front_q, NULL);
		D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);

		tc_sem_timedwait(&test.tg_queue_front_token, 61, __LINE__);
		D_ASSERTF(sent_count != resp_count,
			"Send count matches response count\n");
	}

	DBG_PRINT("Waiting for responses to %d rpcs\n",
		test.tg_burst_count);
	tc_sem_timedwait(&test.tg_token_to_proceed, 61, __LINE__);
	DBG_PRINT("Got all responses\n");

	if (test.tg_send_shutdown) {
		/* Send shutdown to the server */
		rc = crt_req_create(test.tg_crt_ctx, &ep, OPC_SHUTDOWN, &rpc);
		D_ASSERTF(rc == 0, "crt_req_create() failed; rc=%d\n", rc);

		rc = crt_req_send(rpc, rpc_handle_shutdown_reply, NULL);
		D_ASSERTF(rc == 0, "crt_req_send() failed; rc=%d\n", rc);
		tc_sem_timedwait(&test.tg_token_to_proceed, 61, __LINE__);
	}

	D_FREE(rank_list->rl_ranks);
	D_FREE(rank_list);

	if (test.tg_save_cfg) {
		rc = crt_group_detach(grp);
		D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	}

	g_shutdown = 1;

	rc = pthread_join(test.tg_tid, NULL);
	D_ASSERTF(rc == 0, "pthread_join failed. rc: %d\n", rc);
	DBG_PRINT("joined progress thread.\n");

	rc = sem_destroy(&test.tg_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	rc = sem_destroy(&test.tg_queue_front_token);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	d_log_fini();
	DBG_PRINT("exiting.\n");
}

int
main(int argc, char **argv)
{
	int	rc;

	rc = test_parse_args(argc, argv);
	if (rc != 0) {
		fprintf(stderr, "test_parse_args() failed, rc: %d.\n", rc);
		return rc;
	}

	/* rank, num_attach_retries, is_server, assert_on_error */
	tc_test_init(0, 40, false, true);

	test_run();

	return rc;
}
