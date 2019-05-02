/* Copyright (C) 2018-2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <cart/api.h>
#include <gurt/common.h>
#include <semaphore.h>

#include "test_ep_cred_common.h"

static void
attach_to_server()
{
	int	num_retries = 120;
	int	retry;
	int	rc;

	/* try until success to avoid intermittent failures under valgrind. */
	D_DEBUG(DB_TRACE, "about attach to server\n");
	for (retry = 0; retry < num_retries; retry++) {
		sleep(1);
		printf("Attaching to group %s\n", test.tg_remote_group_name);
		rc = crt_group_attach(test.tg_remote_group_name,
				      &test.tg_remote_group);
		if (rc == DER_SUCCESS)
			break;
	}
	D_ASSERTF(rc == 0, "crt_group_attach failed, rc: %d\n", rc);
	D_ASSERTF(test.tg_remote_group != NULL, "NULL attached srv_grp\n");
}


static void
test_init()
{
	int			rc;
	crt_init_options_t	opt = {0};

	fprintf(stderr, "local group: %s remote group: %s\n",
		test.tg_local_group_name, test.tg_remote_group_name);

	rc = d_log_init();
	assert(rc == 0);

	rc = sem_init(&test.tg_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	rc = sem_init(&test.tg_queue_front_token, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	opt.cio_use_credits = 1;
	opt.cio_ep_credits = test.tg_credits;

	D_DEBUG(DB_TRACE, "Number of credits: %d Number of burst: %d\n",
		test.tg_credits, test.tg_burst_count);

	rc = crt_init_opt(test.tg_local_group_name,
			CRT_FLAG_BIT_SINGLETON, &opt);
	D_ASSERTF(rc == 0, "crt_init() failed, rc: %d\n", rc);

	if (test.tg_should_attach)
		attach_to_server();

	rc = crt_group_rank(NULL, &test.tg_my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank() failed. rc: %d\n", rc);

	rc = crt_proto_register(&my_proto_fmt_0);
	D_ASSERTF(rc == 0, "registration failed with rc: %d\n", rc);

	rc = crt_context_create(&test.tg_crt_ctx);
	D_ASSERTF(rc == 0, "crt_context_create() failed. rc: %d\n", rc);

	rc = pthread_create(&test.tg_tid, NULL, progress_thread,
			    &test.tg_thread_id);
	D_ASSERTF(rc == 0, "pthread_create() failed. rc: %d\n", rc);
}

static int resp_count;
static int sent_count;

static void
rpc_handle_reply(const struct crt_cb_info *info)
{
	D_ASSERTF(info->cci_rc == 0, "rpc response failed. rc: %d\n",
		info->cci_rc);
	resp_count++;
	D_DEBUG(DB_TRACE, "Response count=%d\n", resp_count);

	if (resp_count == sent_count) {
		D_DEBUG(DB_ALL, "received all expected replies\n");
		sem_post(&test.tg_token_to_proceed);
	}
}

static void
rpc_handle_ping_front_q(const struct crt_cb_info *info)
{
	D_DEBUG(DB_TRACE, "Response from front queued rpc\n");
	D_ASSERTF(info->cci_rc == 0, "rpc response failed. rc: %d\n",
		info->cci_rc);
	sem_post(&test.tg_queue_front_token);
}

static void
test_run()
{
	crt_endpoint_t	ep;
	crt_rpc_t	*rpc;
	int		i;
	int		rc;
	struct ping_in	*input;

	ep.ep_grp = NULL;
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	D_DEBUG(DB_TRACE, "Sending %d rpcs\n", test.tg_burst_count);

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

		test_sem_timedwait(&test.tg_queue_front_token, 61, __LINE__);
		D_ASSERTF(sent_count != resp_count,
			"Send count matches response count\n");
	}

	D_DEBUG(DB_TRACE, "Waiting for responses to %d rpcs\n",
		test.tg_burst_count);
	test_sem_timedwait(&test.tg_token_to_proceed, 61, __LINE__);
	D_DEBUG(DB_TRACE, "Got all responses\n");

	if (test.tg_send_shutdown) {
		/* Send shutdown to the server */
		rc = crt_req_create(test.tg_crt_ctx, &ep, OPC_SHUTDOWN, &rpc);
		D_ASSERTF(rc == 0, "crt_req_create() failed; rc=%d\n", rc);

		rc = crt_req_send(rpc, NULL, NULL);
		D_ASSERTF(rc == 0, "crt_req_send() failed; rc=%d\n", rc);
	}
}


static void
test_fini()
{
	int	rc;

	test.tg_shutdown = 1;

	rc = pthread_join(test.tg_tid, NULL);
	D_ASSERTF(rc == 0, "pthread_join failed. rc: %d\n", rc);
	D_DEBUG(DB_TRACE, "joined progress thread.\n");

	rc = crt_context_destroy(test.tg_crt_ctx, 1);
	D_ASSERTF(rc == 0, "crt_context_destroy() failed. rc: %d\n", rc);


	if (test.tg_should_attach) {
		rc = crt_group_detach(test.tg_remote_group);
		D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	}

	rc = sem_destroy(&test.tg_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	rc = sem_destroy(&test.tg_queue_front_token);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);
	D_DEBUG(DB_TRACE, "exiting.\n");

	d_log_fini();
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
	test_init();
	test_run();
	test_fini();

	return rc;
}
