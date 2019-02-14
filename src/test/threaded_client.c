/* Copyright (C) 2017-2019 Intel Corporation
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
		rc = crt_progress(crt_ctx, 1, check_status, status);
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

	CRT_RPC_REGISTER(RPC_ID, 0, threaded_rpc);

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
