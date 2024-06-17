/*
 * (C) Copyright 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Simple client that sends ping RPCs to the simple_server with
 * specified response delay and count of rpcs
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <getopt.h>
#include <cart/api.h>
#include <gurt/common.h>
#include "simple_serv_cli_common.h"

#define TEST_IOV_SIZE_IN 4096

static int
g_do_shutdown;

/* stubs, not needed for client */
int
handler_ping(crt_rpc_t *rpc)
{
	return 0;
}

int
handler_shutdown(crt_rpc_t *rpc)
{
	return 0;
}

/* main loop progress function */
static void *
progress_function(void *data)
{
	crt_context_t *p_ctx = (crt_context_t *)data;

	while (g_do_shutdown == 0)
		crt_progress(*p_ctx, 1000);

	crt_context_destroy(*p_ctx, 1);

	return NULL;
}

static void
rpc_handle_reply(const struct crt_cb_info *info)
{
	sem_t	*sem;

	D_ASSERTF(info->cci_rc == 0, "rpc response failed. rc: %d\n", info->cci_rc);
	sem = (sem_t *)info->cci_arg;
	sem_post(sem);
}

int
wait_for_sem(sem_t *sem, int sec)
{
	struct timespec	deadline;
	int		rc;

	rc = clock_gettime(CLOCK_REALTIME, &deadline);
	D_ASSERTF(rc == 0, "clock_gettime() failed; rc=%d\n", rc);

	deadline.tv_sec += sec;
	rc = sem_timedwait(sem, &deadline);
	D_ASSERTF(rc == 0, "sem_timedwait() failed; rc=%d\n", rc);

	return rc;
}

int main(int argc, char **argv)
{
	crt_context_t		crt_ctx;
	crt_group_t		*grp;
	int			rc;
	sem_t			sem;
	pthread_t		progress_thread;
	crt_rpc_t		*rpc = NULL;
	struct RPC_PING_in	*input;
	crt_endpoint_t		server_ep;
	d_rank_t		rank;
	d_iov_t			iov;
	int			tag;
	int			opt_idx;
	int			rpc_count = 4096;
	bool			send_shutdown = false;
	int			rpc_delay = 0;
	int			seq = 0;
	int			rpc_timeout = 2;
	int			i;

	struct option		long_options[] = {
		{"count", required_argument, 0, 'c'},
		{"delay", required_argument, 0, 'd'},
		{"shutdown", no_argument, 0, 's'},
		{0, 0, 0, 0}
	};

	rc = d_log_init();
	assert(rc == 0);

	if (getenv("D_PROVIDER") == NULL) {
		DBG_PRINT("Warning: D_PROVIDER was not set, assuming 'ofi+tcp'\n");
		setenv("D_PROVIDER", "ofi+tcp", 1);
	}

	if (getenv("D_INTERFACE") == NULL) {
		DBG_PRINT("Warning: D_INTERFACE was not set, assuming 'eth0'\n");
		setenv("D_INTERFACE", "eth0", 1);
	}

	while (1) {

		rc = getopt_long(argc, argv, "c:d:s", long_options, &opt_idx);
		if (rc == -1)
			break;
		switch (rc) {
		case 0:
			if (long_options[opt_idx].flag != 0)
				break;

		case 'c':
			rpc_count = atoi(optarg);
			break;
		case 'd':
			rpc_delay = atoi(optarg);
			break;
		case 's':
			send_shutdown = true;
			break;
		default:
			DBG_PRINT("Unknown option\n");
			return -1;
		}

	}
	DBG_PRINT("Client starting up. count=%d delay=%d shutdown=%d\n",
		rpc_count, rpc_delay, send_shutdown);

	rc = sem_init(&sem, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed; rc=%d\n", rc);

	rc = crt_init(NULL, 0);
	D_ASSERTF(rc == 0, "crt_init() failed; rc=%d\n", rc);

	rc = crt_proto_register(&my_proto_fmt);
	D_ASSERTF(rc == 0, "crt_proto_register() failed; rc=%d\n", rc);

	rc = crt_group_attach("simple_server", &grp);
	D_ASSERTF(rc == 0 && grp != NULL, "crt_group_view_create() failed; rc=%d\n", rc);

	rc = crt_context_create(&crt_ctx);
	D_ASSERTF(rc == 0, "crt_context_create() failed; rc=%d\n", rc);

	rc = pthread_create(&progress_thread, 0, progress_function, &crt_ctx);
	D_ASSERTF(rc == 0, "pthread_create() failed; rc=%d\n", rc);

	D_ALLOC(iov.iov_buf, TEST_IOV_SIZE_IN);
	D_ASSERTF(iov.iov_buf != NULL, "Failed to allocate iov buf\n");

	memset(iov.iov_buf, 'a', TEST_IOV_SIZE_IN);

	iov.iov_buf_len = TEST_IOV_SIZE_IN;
	iov.iov_len = TEST_IOV_SIZE_IN;


	rank = 0;
	tag = 0;

	for (i = 0; i < rpc_count; i++) {
		DBG_PRINT("Sending ping [%d/%d] to %d:%d\n", i + 1, rpc_count, rank, tag);

		server_ep.ep_rank = rank;
		server_ep.ep_tag = tag;
		server_ep.ep_grp = grp;

		rc = crt_req_create(crt_ctx, &server_ep, RPC_PING, &rpc);
		D_ASSERTF(rc == 0, "crt_req_create() failed; rc=%d\n", rc);

		input = crt_req_get(rpc);
		input->seq = seq;
		input->delay_sec = rpc_delay;

		rpc_timeout = input->delay_sec + 5;
		rc = crt_req_set_timeout(rpc, rpc_timeout);
		D_ASSERTF(rc == 0, "crt_req_set_timeout() failed; rc=%d\n", rc);

		rc = crt_req_send(rpc, rpc_handle_reply, &sem);
		D_ASSERTF(rc == 0, "crt_req_send() failed; rc=%d\n", rc);

		wait_for_sem(&sem, rpc_timeout + 1);
		DBG_PRINT("Ping response from %d:%d\n", rank, tag);
	}

	D_FREE(iov.iov_buf);

	/* Send shutdown RPC */
	if (send_shutdown) {
		DBG_PRINT("Sending shutdown to rank=%d\n", rank);
		rpc_timeout = 2;

		server_ep.ep_rank = rank;
		server_ep.ep_tag = 0;
		server_ep.ep_grp = grp;

		rc = crt_req_create(crt_ctx, &server_ep, RPC_SHUTDOWN, &rpc);
		D_ASSERTF(rc == 0, "crt_req_create() failed; rc=%d\n", rc);

		rc = crt_req_set_timeout(rpc, rpc_timeout);
		D_ASSERTF(rc == 0, "crt_req_set_timeout() failed; rc=%d\n", rc);

		rc = crt_req_send(rpc, rpc_handle_reply, &sem);
		D_ASSERTF(rc == 0, "crt_req_send() failed; rc=%d\n", rc);
	
		wait_for_sem(&sem, rpc_timeout + 1);
		DBG_PRINT("RPC response received from rank=%d\n", rank);
	}

	rc = crt_group_view_destroy(grp);
	D_ASSERTF(rc == 0, "crt_group_view_destroy() failed; rc=%d\n", rc);

	g_do_shutdown = true;
	pthread_join(progress_thread, NULL);

	sem_destroy(&sem);

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed; rc=%d\n", rc);

	DBG_PRINT("Client successfully finished\n");
	d_log_fini();

	return 0;
}
