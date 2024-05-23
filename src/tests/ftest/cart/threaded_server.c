/*
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This tests a threaded server handling RPCs on a single context
 */
#include <stdio.h>
#include "threaded_rpc.h"

static int		done;
static crt_context_t	crt_ctx;
static int		msg_counts[MSG_COUNT];
static pthread_mutex_t	lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	cond = PTHREAD_COND_INITIALIZER;

#define NUM_THREADS 16
#define STOP 1

static int check_status(void *arg)
{
	int	*status = (int *)arg;

	return (*status == STOP);
}

static void *progress(void *arg)
{
	int	*status = (int *)arg;
	int	 rc;

	__sync_fetch_and_add(status, -1);

	do {
		rc = crt_progress_cond(crt_ctx, 1000*1000, check_status,
				       status);
		if (rc == -DER_TIMEDOUT)
			sched_yield();
		else if (rc != 0)
			printf("crt_progress failed rc: %d", rc);
	} while (*status != STOP);

	return NULL;
}
static void signal_done(void)
{
	D_MUTEX_LOCK(&lock);
	done = 1;
	pthread_cond_signal(&cond);
	D_MUTEX_UNLOCK(&lock);
}

static void rpc_handler(crt_rpc_t *rpc)
{
	struct threaded_rpc_in	*in;
	struct threaded_rpc_out	*output;
	int			 rc;
	int			 i;

	in = crt_req_get(rpc);
	output = crt_reply_get(rpc);

	for (i = 0; i < MSG_COUNT; i++) {
		if (in->msg == msg_values[i] && in->payload == MSG_IN_VALUE) {
			__sync_fetch_and_add(&msg_counts[i], 1);
			output->msg = MSG_OUT_VALUE;
			output->value = in->msg;
			break;
		}
	}

	rc = crt_reply_send(rpc);
	if (rc != 0)
		printf("Failed to send reply, rc = %d\n", rc);

	if (i == MSG_STOP) {
		printf("Received stop rpc\n");
		signal_done();
	} else if (i == MSG_COUNT) {
		printf("Bad rpc message received %#x %#x\n", in->msg,
		       in->payload);
		signal_done();
	}
}

static struct crt_proto_rpc_format my_proto_rpc_fmt_threaded_server[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_threaded_rpc,
		.prf_hdlr	= rpc_handler,
		.prf_co_ops	= NULL,
	}
};

static struct crt_proto_format my_proto_fmt_threaded_server = {
	.cpf_name = "my-proto-threaded_server",
	.cpf_ver = TEST_THREADED_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_threaded_server),
	.cpf_prf = &my_proto_rpc_fmt_threaded_server[0],
	.cpf_base = TEST_THREADED_BASE,
};

int main(int argc, char **argv)
{
	pthread_t		thread[NUM_THREADS];
	ATOMIC int              status = 0;
	int                     rc;
	int			i;
	int                     thread_count;

	rc = d_log_init();
	assert(rc == 0);

	rc = crt_init("manyserver", CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	if (rc != 0) {
		printf("Could not start server, rc = %d", rc);
		goto log_fini;
	}

	rc = crt_proto_register(&my_proto_fmt_threaded_server);
	if (rc != 0) {
		printf("Could not register rpc protocol , rc = %d", rc);
		goto fini;
	}

	rc = crt_context_create(&crt_ctx);
	if (rc != 0) {
		printf("Failed to create context: " DF_RC "\n", DP_RC(rc));
		goto fini;
	}

	for (thread_count = 0; thread_count < NUM_THREADS; thread_count++) {
		rc = pthread_create(&thread[thread_count], NULL, progress, &status);
		if (rc != 0) {
			printf("Failed to create thread: %d (%s), exiting\n", rc, strerror(rc));
			goto stop;
		}
	}

	printf("Waiting for threads to start\n");
	while (atomic_load_relaxed(&status) != -NUM_THREADS)
		sched_yield();

	printf("Waiting for stop rpc\n");
	D_MUTEX_LOCK(&lock);
	while (done == 0)
		rc = pthread_cond_wait(&cond, &lock);

	D_MUTEX_UNLOCK(&lock);
	printf("Stop rpc exited with rc = %d\n", rc);

	rc = 0;
stop:
	status = STOP;
	printf("Waiting for threads to stop\n");

	for (i = 0; i < thread_count; i++)
		pthread_join(thread[i], NULL);

	drain_queue(crt_ctx);

	printf("Server message counts:\n");
	for (i = 0; i < MSG_COUNT; i++)
		printf("\tSERVER\t%-10s:\t%10d\n", msg_strings[i],
		       msg_counts[i]);

	crt_context_destroy(crt_ctx, false);
fini:
	crt_finalize();

log_fini:
	d_log_fini();

	return rc;
}
