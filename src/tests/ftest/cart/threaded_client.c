/*
 * (C) Copyright 2017-2020 Intel Corporation.
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
/**
 * This tests a threaded client sending RPCs on a single context
 */

#include <stdio.h>

#include "threaded_rpc.h"

static crt_context_t crt_ctx;

#define NUM_THREADS 16
#define RESET    0
#define STARTED  1
#define STOPPING 2
#define SHUTDOWN 3


static int check_status(void *arg)
{
	int	*status = (int *)arg;

	return (*status == SHUTDOWN);
}

static void *progress(void *arg)
{
	int	*status = (int *)arg;
	int	 rc;

	crt_context_create(&crt_ctx);
	__sync_fetch_and_add(status, 1);

	do {
		rc = crt_progress_cond(crt_ctx, 1, check_status, status);
		if (rc == -DER_TIMEDOUT)
			sched_yield();
		else if (rc != 0)
			printf("crt_progress failed rc: %d", rc);
	} while (*status != SHUTDOWN);

	return NULL;
}

static crt_endpoint_t target_ep;

struct msg_info {
	int msg_type;
	int status;
};

void complete_cb(const struct crt_cb_info *cb_info)
{
	struct msg_info	*info = cb_info->cci_arg;
	struct threaded_rpc_out	*output;

	if (cb_info->cci_rc == -DER_TIMEDOUT) {
		printf("timeout detected\n");
		info->status = -DER_TIMEDOUT;
		return;
	}
	if (cb_info->cci_rc != 0) {
		printf("error detected rc=%d\n", cb_info->cci_rc);
		info->status = cb_info->cci_rc;
		return;
	}

	output = crt_reply_get(cb_info->cci_rpc);
	if (output->msg != MSG_OUT_VALUE ||
	    output->value != msg_values[info->msg_type]) {
		printf("bad output %#x %#x\n", output->msg, output->value);
		info->status = -DER_INVAL;
		return;
	}

	info->status = 1;
}

static int	msg_counts[MSG_COUNT];

static bool send_message(int msg)
{
	crt_rpc_t		*req;
	struct threaded_rpc_in	*input;
	int			 rc;
	struct msg_info		 info = {0};

	rc = crt_req_create(crt_ctx, &target_ep, RPC_ID, &req);
	if (rc != 0) {
		printf("Failed to create req %d\n", rc);
		return false;
	}
	info.msg_type = msg;
	input = crt_req_get(req);
	input->msg = msg_values[msg];
	input->payload = MSG_IN_VALUE;

	rc = crt_req_send(req, complete_cb, &info);
	if (rc != 0) {
		printf("Failed to send req %d\n", rc);
		return false;
	}

	while (!info.status)
		sched_yield();

	if (info.status != 1)
		return false;

	__sync_fetch_and_add(&msg_counts[msg], 1);

	return true;
}

static void *send_rpcs(void *arg)
{
	int	*status = (int *)arg;
	int	 num;
	bool	 working = true;

	num = __sync_fetch_and_add(status, -1);

	do {
		working = send_message(MSG_TYPE1);
		if (!working)
			break;
		working = send_message(MSG_TYPE2);
		if (!working)
			break;
		working = send_message(MSG_TYPE3);
		if (!working)
			break;
	} while (*status != STOPPING);

	if (!working) {
		send_message(MSG_STOP);
		return arg;
	} else if (num == 0) {
		if (!send_message(MSG_STOP))
			return arg;
	}

	return NULL;
}

#define check_return(cmd, saved_rc)					\
	do {								\
		int	rc_chk_rtn = (cmd);				\
		if ((rc_chk_rtn) != 0) {				\
			(saved_rc) = rc_chk_rtn;			\
			printf("Error executing " #cmd ": rc = %d\n",	\
			       rc_chk_rtn);				\
		}							\
	} while (0)

static struct crt_proto_rpc_format my_proto_rpc_fmt_threaded_client[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_threaded_rpc,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}
};

static struct crt_proto_format my_proto_fmt_threaded_client = {
	.cpf_name = "my-proto-threaded_client",
	.cpf_ver = TEST_THREADED_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_threaded_client),
	.cpf_prf = &my_proto_rpc_fmt_threaded_client[0],
	.cpf_base = TEST_THREADED_BASE,
};

int main(int argc, char **argv)
{
	pthread_t		 thread[NUM_THREADS];
	pthread_t		 progress_thread;
	crt_group_t		*grp;
	void			*ret;
	int			 saved_rc;
	int			 status = RESET;
	int			 i;

	saved_rc = d_log_init();
	assert(saved_rc == 0);

	saved_rc = crt_init(NULL, 0);
	if (saved_rc != 0) {
		printf("Could not start server, rc = %d", saved_rc);
		return -1;
	}

	saved_rc = crt_proto_register(&my_proto_fmt_threaded_client);
	if (saved_rc != 0) {
		printf("Could not register rpc protocol , rc = %d", saved_rc);
		return -1;
	}
	pthread_create(&progress_thread, NULL, progress, &status);
	while (status != STARTED)
		sched_yield();

	for (;;) {
		int	rc_tmp;

		rc_tmp = crt_group_attach("manyserver", &grp);
		if (rc_tmp == 0)
			break;
		printf("Attach not yet available, sleeping...\n");
		sleep(1);
	}

	target_ep.ep_grp = grp;
	target_ep.ep_rank = 0;
	target_ep.ep_tag = 0;

	while (!send_message(MSG_START)) {
		printf("Server not ready yet\n");
		sleep(1);
	}

	status = RESET;

	for (i = 0; i < NUM_THREADS; i++)
		pthread_create(&thread[i], NULL, send_rpcs, &status);

	/* Run test for 10 seconds */
	printf("Running test for 10 seconds");
	for (i = 0; i < 10; i++) {
		printf(".");
		fflush(stdout);
		sleep(1);
	}
	printf("\n");

	if (status != -NUM_THREADS) {
		printf("Problem starting threads\n");
		saved_rc = 1;
	}

	status = STOPPING;

	for (i = 0; i < NUM_THREADS; i++) {
		pthread_join(thread[i], &ret);
		if (ret != NULL)
			saved_rc = 1;
	}

	printf("Client message counts:\n");
	for (i = 0; i < MSG_COUNT; i++)
		printf("\tCLIENT\t%-10s:\t%10d\n", msg_strings[i],
		       msg_counts[i]);

	status = SHUTDOWN;
	pthread_join(progress_thread, NULL);

	check_return(drain_queue(crt_ctx), saved_rc);
	check_return(crt_group_detach(grp), saved_rc);
	check_return(crt_context_destroy(crt_ctx, false), saved_rc);
	check_return(crt_finalize(), saved_rc);

	d_log_fini();

	return saved_rc;
}
