/*
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This tests a threaded client sending RPCs on a single context
 */

#include "threaded_rpc.h"

#define NUM_THREADS 16
#define STOP        1

struct msg_info {
	int        msg_type;
	ATOMIC int rc;
};

static crt_context_t  crt_ctx;
static crt_endpoint_t target_ep;
static ATOMIC int     msg_counts[MSG_COUNT];

void
complete_cb(const struct crt_cb_info *cb_info)
{
	struct msg_info         *info = cb_info->cci_arg;
	struct threaded_rpc_out *output;
	int                      rc;

	if (cb_info->cci_rc == -DER_TIMEDOUT) {
		printf("timeout detected\n");
		rc = -DER_TIMEDOUT;
	} else if (cb_info->cci_rc != 0) {
		printf("error detected rc=%d\n", cb_info->cci_rc);
		rc = cb_info->cci_rc;
	} else {
		output = crt_reply_get(cb_info->cci_rpc);
		if (output->msg != MSG_OUT_VALUE || output->value != msg_values[info->msg_type]) {
			printf("bad output %#x %#x\n", output->msg, output->value);
			rc = -DER_INVAL;
		} else {
			rc = 1;
		}
	}

	atomic_store(&info->rc, rc);
}

static bool
send_message(int msg)
{
	crt_rpc_t              *req;
	struct threaded_rpc_in *input;
	int                     rc;
	struct msg_info         info = {0};

	rc = crt_req_create(crt_ctx, &target_ep, RPC_ID, &req);
	if (rc != 0) {
		printf("Failed to create req %d\n", rc);
		return false;
	}
	info.msg_type  = msg;
	input          = crt_req_get(req);
	input->msg     = msg_values[msg];
	input->payload = MSG_IN_VALUE;

	rc = crt_req_send(req, complete_cb, &info);
	if (rc != 0) {
		printf("Failed to send req %d\n", rc);
		return false;
	}

	rc = atomic_load(&info.rc);
	while (!rc) {
		sched_yield();

		rc = crt_progress(crt_ctx, 1);
		if (rc == -DER_TIMEDOUT)
			sched_yield();
		else if (rc != 0) {
			printf("crt_progress failed rc: %d", rc);
			return false;
		}

		rc = atomic_load(&info.rc);
	}

	if (rc != 1)
		return false;

	atomic_fetch_add(&msg_counts[msg], 1);

	return true;
}

static void *
send_rpcs(void *arg)
{
	ATOMIC int *status = (ATOMIC int *)arg;
	int         num;
	bool        working = true;

	num = atomic_fetch_add(status, -1);

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
	} while (atomic_load(status) != STOP);

	if (!working) {
		send_message(MSG_STOP);
		return arg;
	} else if (num == 0) {
		if (!send_message(MSG_STOP))
			return arg;
	}

	return NULL;
}

#define check_return(cmd, saved_rc)                                                                \
	do {                                                                                       \
		int rc_chk_rtn = (cmd);                                                            \
		if ((rc_chk_rtn) != 0) {                                                           \
			(saved_rc) = rc_chk_rtn;                                                   \
			printf("Error executing " #cmd ": rc = %d\n", rc_chk_rtn);                 \
		}                                                                                  \
	} while (0)

static struct crt_proto_rpc_format my_proto_rpc_fmt_threaded_client[] = {{
    .prf_flags   = 0,
    .prf_req_fmt = &CQF_threaded_rpc,
    .prf_hdlr    = NULL,
    .prf_co_ops  = NULL,
}};

static struct crt_proto_format     my_proto_fmt_threaded_client = {
	.cpf_name  = "my-proto-threaded_client",
	.cpf_ver   = TEST_THREADED_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_threaded_client),
	.cpf_prf   = &my_proto_rpc_fmt_threaded_client[0],
	.cpf_base  = TEST_THREADED_BASE,
};

int
main(int argc, char **argv)
{
	pthread_t    thread[NUM_THREADS];
	crt_group_t *grp;
	void        *ret;
	int          saved_rc;
	ATOMIC int   status = 0;
	int          i;

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

	saved_rc = crt_context_create(&crt_ctx);
	if (saved_rc != 0) {
		printf("Failed to create context: " DF_RC "\n", DP_RC(saved_rc));
		return -1;
	}

	for (;;) {
		int rc_tmp;

		rc_tmp = crt_group_attach("threaded_server", &grp);
		if (rc_tmp == 0)
			break;
		printf("Attach not yet available, sleeping...\n");
		sleep(1);
	}

	target_ep.ep_grp  = grp;
	target_ep.ep_rank = 0;
	target_ep.ep_tag  = 0;

	while (!send_message(MSG_START)) {
		printf("Server not ready yet\n");
		sleep(1);
	}

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

	if (atomic_load(&status) != -NUM_THREADS) {
		printf("Problem starting threads\n");
		saved_rc = 1;
	}

	atomic_store(&status, STOP);
	printf("Waiting for threads to stop\n");

	for (i = 0; i < NUM_THREADS; i++) {
		pthread_join(thread[i], &ret);
		if (ret != NULL)
			saved_rc = 1;
	}

	printf("Client message counts:\n");
	for (i = 0; i < MSG_COUNT; i++)
		printf("\tCLIENT\t%-10s:\t%10d\n", msg_strings[i], atomic_load(&msg_counts[i]));

	check_return(crt_group_detach(grp), saved_rc);
	check_return(crt_context_destroy(crt_ctx, false), saved_rc);
	check_return(crt_finalize(), saved_rc);

	d_log_fini();

	return saved_rc;
}
