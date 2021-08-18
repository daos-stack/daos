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

#define NUM_SERVER_CTX 8

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

static int handler_ping(crt_rpc_t *rpc);
static int handler_shutdown(crt_rpc_t *rpc);

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

static int
bulk_transfer_done_cb(const struct crt_bulk_cb_info *info)
{
	void	*buff;
	int	rc;

	if (info->bci_rc != 0) {
		D_ERROR("Bulk transfer failed with rc=%d\n", info->bci_rc);
		error_exit();
	}

	DBG_PRINT("Bulk transfer done\n");

	rc = crt_reply_send(info->bci_bulk_desc->bd_rpc);
	if (rc != 0) {
		D_ERROR("Failed to send response\n");
		error_exit();
	}

	crt_bulk_free(info->bci_bulk_desc->bd_local_hdl);

	buff = info->bci_arg;
	D_FREE(buff);

	RPC_PUB_DECREF(info->bci_bulk_desc->bd_rpc);

	return 0;
}

static int
handler_ping(crt_rpc_t *rpc)
{
	struct RPC_PING_in	*input;
	struct RPC_PING_out	*output;
	int			rc = 0;

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

	//DBG_PRINT("Received ping\n");
	if (input->file_size != 0) {
		struct crt_bulk_desc	bulk_desc;
		crt_bulk_t		dst_bulk;
		char			*dst;
		d_sg_list_t		sgl;

		D_ALLOC_ARRAY(dst, input->file_size);

		rc = d_sgl_init(&sgl, 1);
		if (rc != 0)
			error_exit();

		sgl.sg_iovs[0].iov_buf = dst;
		sgl.sg_iovs[0].iov_buf_len = input->file_size;
		sgl.sg_iovs[0].iov_len = input->file_size;

		rc = crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RW, &dst_bulk);
		if (rc != 0)
			error_exit();

		RPC_PUB_ADDREF(rpc);
		bulk_desc.bd_rpc = rpc;
		bulk_desc.bd_bulk_op = CRT_BULK_GET;
		bulk_desc.bd_remote_hdl = input->bulk_hdl;
		bulk_desc.bd_remote_off = 0;
		bulk_desc.bd_local_hdl = dst_bulk;
		bulk_desc.bd_local_off = 0;
		bulk_desc.bd_len = input->file_size;
		rc = crt_bulk_transfer(&bulk_desc, bulk_transfer_done_cb,
				       dst, NULL);
		if (rc != 0) {
			D_ERROR("transfer failed; rc=%d\n", rc);
			error_exit();
		}
	} else {
		output->rc = rc;
		rc = crt_reply_send(rpc);
		if (rc != 0) {
			D_ERROR("reply failed; rc=%d\n", rc);
			error_exit();
		}
	}

	return 0;
}

static int
handler_shutdown(crt_rpc_t *rpc)
{
	crt_reply_send(rpc);
	crtu_progress_stop();
	return 0;
}

int main(void)
{
	crt_context_t		crt_ctx[NUM_SERVER_CTX];
	pthread_t		progress_thread[NUM_SERVER_CTX];
	int			rc;
	int			i;

	crtu_test_init(0, 20, true, true);

	rc = crt_init("cart_server", CRT_FLAG_BIT_SERVER |
		      CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	if (rc != 0) {
		D_ERROR("crt_init() failed; rc=%d\n", rc);
		error_exit();
	}

	rc = crt_rank_self_set(0);
	if (rc != 0) {
		D_ERROR("crt_rank_self_set(0) failed; rc = %d\n", rc);
	}

	for (i = 0; i < NUM_SERVER_CTX; i++) {

		DBG_PRINT("Creating context %d\n", i);
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

	rc = crt_group_config_save(NULL, true);
	if (rc != 0) {
		D_ERROR("crt_group_config_save() failed; rc=%d\n", rc);
		error_exit();
	}

	/* Wait until shutdown is issued and progress threads exit */
	for (i = 0; i < NUM_SERVER_CTX; i++)
		pthread_join(progress_thread[i], NULL);

	rc = crt_finalize();
	if (rc != 0) {
		D_ERROR("crt_finalize() failed with rc=%d\n", rc);
		error_exit();
	}

	return 0;
}

