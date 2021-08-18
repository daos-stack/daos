/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Test cart server to be ran standalone
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <getopt.h>
#include <semaphore.h>

#include "crt_utils.h"

static void
error_exit(void)
{
	assert(0);
}

#define MY_BASE 0x010000000
#define MY_VER  0

#define NUM_CLIENT_CTX 32

#define RPC_DECLARE(name)					\
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)	\
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)

enum {
	RPC_PING = CRT_PROTO_OPC(MY_BASE, MY_VER, 0),
	RPC_SHUTDOWN
} rpc_id_t;

#define CRT_ISEQ_RPC_PING	/* input fields */		 \
	((crt_bulk_t)		(bulk_hdl)		CRT_VAR) \
	((uint64_t)		(file_size)		CRT_VAR) \
	((uint64_t)		(src_rank)		CRT_VAR) \
	((uint64_t)		(dst_tag)		CRT_VAR)

#define CRT_OSEQ_RPC_PING	/* output fields */		 \
	((int64_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_SHUTDOWN	/* input fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_OSEQ_RPC_SHUTDOWN	/* output fields */		 \
	((uint64_t)		(field)			CRT_VAR)

static int handler_ping(crt_rpc_t *rpc) { return 0; }
static int handler_shutdown(crt_rpc_t *rpc) { return 0; }

RPC_DECLARE(RPC_PING);
RPC_DECLARE(RPC_SHUTDOWN);

struct crt_proto_rpc_format my_proto_rpc_fmt[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_RPC_PING,
		.prf_hdlr	= (void *)handler_ping,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_RPC_SHUTDOWN,
		.prf_hdlr	= (void *)handler_shutdown,
		.prf_co_ops	= NULL,
	}
};

struct crt_proto_format my_proto_fmt = {
	.cpf_name = "my-proto",
	.cpf_ver = MY_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt),
	.cpf_prf = &my_proto_rpc_fmt[0],
	.cpf_base = MY_BASE,
};

static void
rpc_handle_reply(const struct crt_cb_info *info)
{
	sem_t	*sem;

	D_ASSERTF(info->cci_rc == 0, "rpc response failed. rc: %d\n",
		  info->cci_rc);

	sem = (sem_t *)info->cci_arg;
	sem_post(sem);
}

crt_context_t		crt_ctx[NUM_CLIENT_CTX];
pthread_t		progress_thread[NUM_CLIENT_CTX];

int main(void)
{
	int			rc;
	int			i;

	crtu_test_init(0, 20, false, true);

	rc = crt_init("cart_server", 0);
	if (rc != 0) {
		D_ERROR("crt_init() failed; rc=%d\n", rc);
		error_exit();
	}

	for (i = 0; i < NUM_CLIENT_CTX; i++) {
		rc = crt_context_create(&crt_ctx[i]);
		if (rc != 0) {
			D_ERROR("crt_context_create() ctx=%d failed; rc=%d\n",
				i, rc);
			error_exit();
		}

		rc = pthread_create(&progress_thread[i], 0,
				    crtu_progress_fn, &crt_ctx[i]);
		if (rc != 0) {
			D_ERROR("pthread_create() ctx=%d failed; rc=%d\n",
				i, rc);
			error_exit();
		}
	}

	rc = crt_proto_register(&my_proto_fmt);
	if (rc != 0) {
		D_ERROR("crt_proto_register() failed; rc=%d\n", rc);
		error_exit();
	}

	crt_group_t *grp;
	rc = crt_group_attach("cart_server", &grp);
	if (rc != 0) {
		D_ERROR("crt_group_attach() failed; rc=%d\n", rc);
		error_exit();
	}

	/**** RPC Portion *****/
	crt_endpoint_t server_ep;
	crt_rpc_t		*rpc = NULL;
	struct RPC_PING_in	*input;
	sem_t			sem;

	rc = sem_init(&sem, 0, 0);
	if (rc != 0) {
		D_ERROR("sem_init() failed; rc=%d\n", rc);
		error_exit();
	}

/*
	int tag;
	int src_ctx;

	for (src_ctx = 0; src_ctx < NUM_CLIENT_CTX; src_ctx++) {
		for (tag = 0; tag < 1; tag++) {
			DBG_PRINT("prepare rpc: source_context=%d, destination_tag=%d\n", src_ctx, tag);
*/

	int idx_start = 0;
	int idx_end = NUM_CLIENT_CTX-1;
	int idx;

	for (idx = idx_start; idx <= idx_end; idx++) {

		server_ep.ep_rank = 0;
		server_ep.ep_tag = 0;
		server_ep.ep_grp = grp;


		rc = crt_req_create(crt_ctx[idx], &server_ep, RPC_PING, &rpc);
		if (rc != 0) {
			D_ERROR("crt_req_create() failed; rc=%d\n", rc);
			error_exit();
		}

		input = crt_req_get(rpc);
		input->src_rank = 0;
		input->dst_tag = 0;
		input->bulk_hdl = CRT_BULK_NULL;
		input->file_size = 0;

		rc = crt_req_send(rpc, rpc_handle_reply, &sem);
		if (rc != 0) {
			D_ERROR("Failed to send rpc; rc=%d\n", rc);
			error_exit();
		}

		DBG_PRINT("rpc sent from context=%d\n", idx);
		crtu_sem_timedwait(&sem, 1, __LINE__);
		DBG_PRINT("response received\n\n");
	}
#if 0
	/* Wait until shutdown is issued and progress threads exit */
	for (i = 0; i < NUM_CLIENT_CTX; i++)
		pthread_join(progress_thread[i], NULL);

	rc = crt_finalize();
	if (rc != 0) {
		D_ERROR("crt_finalize() failed with rc=%d\n", rc);
		error_exit();
	}
#endif

	return 0;
}
