/*
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __NO_PMIX_LAUNCHER_COMMON_H__
#define __NO_PMIX_LAUNCHER_COMMON_H__

#define MY_BASE 0x010000000
#define MY_VER  0

#define NUM_SERVER_CTX 8
#define TEST_IOV_SIZE_IN 4096
#define TEST_IOV_SIZE_OUT 4096

#define RPC_DECLARE(name)						\
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)		\
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)

enum {
	RPC_PING = CRT_PROTO_OPC(MY_BASE, MY_VER, 0),
	RPC_SET_GRP_INFO,
	RPC_SHUTDOWN
} rpc_id_t;


#define CRT_ISEQ_RPC_PING	/* input fields */		 \
	((uint64_t)		(tag)			CRT_VAR) \
	((crt_bulk_t)		(bulk_hdl)		CRT_VAR) \
	((uint64_t)		(delay)			CRT_VAR) \
	((d_iov_t)		(test_data)		CRT_VAR)

#define CRT_OSEQ_RPC_PING	/* output fields */		 \
	((uint64_t)		(field)			CRT_VAR) \
	((d_iov_t)		(test_data)		CRT_VAR)

#define CRT_ISEQ_RPC_SET_GRP_INFO /* input fields */		 \
	((d_iov_t)		(grp_info)		CRT_VAR)

#define CRT_OSEQ_RPC_SET_GRP_INFO /* output fields */		 \
	((uint64_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_SHUTDOWN	/* input fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_OSEQ_RPC_SHUTDOWN	/* output fields */		 \
	((uint64_t)		(field)			CRT_VAR)

int handler_ping(crt_rpc_t *rpc);
int handler_set_group_info(crt_rpc_t *rpc);
int handler_shutdown(crt_rpc_t *rpc);

RPC_DECLARE(RPC_PING);
RPC_DECLARE(RPC_SET_GRP_INFO);
RPC_DECLARE(RPC_SHUTDOWN);

struct crt_proto_rpc_format my_proto_rpc_fmt[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_RPC_PING,
		.prf_hdlr	= (void *)handler_ping,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_RPC_SET_GRP_INFO,
		.prf_hdlr	= (void *)handler_set_group_info,
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


char g_iov[TEST_IOV_SIZE_IN];

static int
bulk_transfer_done_cb(const struct crt_bulk_cb_info *info)
{
	crt_rpc_t		*rpc;
	uint64_t		rpcid;
	int			rc;

	rpc = info->bci_bulk_desc->bd_rpc;
	crt_req_rpcid_get(rpc, &rpcid);



	if (info->bci_rc != 0) {
		DBG_PRINT("[RPCID: 0x%lx] Bulk transfer failed with rc=%d (%s)\n",
			  rpcid, info->bci_rc, d_errstr(info->bci_rc));
	} else {
		DBG_PRINT("[RPCID: 0x%lx] Bulk transfer passed\n", rpcid);
	}

	crt_bulk_free(info->bci_bulk_desc->bd_local_hdl);

	rc = crt_reply_send(rpc);
	DBG_PRINT("[RPCID: 0x%lx] Responded to rpc, rc=%d\n", rpcid, rc);


	int 			i;

	fprintf(stderr, "dma bufer contents:\n");
	for (i = 0; i < TEST_IOV_SIZE_IN/10; i++) {
		fprintf(stderr, "%x", g_iov[i]);
	}
	fprintf(stderr, "\n\n");

	/* addref in handler_ping() */
	RPC_PUB_DECREF(rpc);
	return 0;
}


int handler_ping(crt_rpc_t *rpc)
{
	struct RPC_PING_in	*input;
	struct RPC_PING_out	*output;
	d_iov_t			iov;
	int			my_tag;
	d_rank_t		hdr_dst_rank;
	uint32_t		hdr_dst_tag;
	d_rank_t		hdr_src_rank;
	uint64_t		rpcid;
	d_sg_list_t		sgl;
	crt_bulk_t		local_bulk;
	struct crt_bulk_desc	bulk_desc;
	int			rc;

	input = crt_req_get(rpc);

	rc = crt_req_rpcid_get(rpc, &rpcid);
	D_ASSERTF(rc == 0, "crt_req_rpcid_get() failed; rc=%d\n", rc);

	DBG_PRINT("[RPCID: 0x%lx] ping handler called with delay: %ld seconds\n", rpcid, input->delay);

	rc = crt_req_src_rank_get(rpc, &hdr_src_rank);
	D_ASSERTF(rc == 0, "crt_req_src_rank_get() failed; rc=%d\n", rc);

	rc = crt_req_dst_rank_get(rpc, &hdr_dst_rank);
	D_ASSERTF(rc == 0, "crt_req_dst_rank_get() failed; rc=%d\n", rc);

	rc = crt_req_dst_tag_get(rpc, &hdr_dst_tag);
	D_ASSERTF(rc == 0, "crt_req_dst_tag_get() failed; rc=%d\n", rc);

	crt_context_idx(rpc->cr_ctx, &my_tag);

	if (my_tag != input->tag || my_tag != hdr_dst_tag) {
		D_ERROR("Incorrect tag Expected %lu got %d (hdr=%d)\n",
			input->tag, my_tag, hdr_dst_tag);
		assert(0);
	}

	if (hdr_src_rank != CRT_NO_RANK) {
		D_ERROR("Expected %d got %d\n", CRT_NO_RANK, hdr_src_rank);
		assert(0);
	}

	D_ALLOC(iov.iov_buf, TEST_IOV_SIZE_OUT);
	memset(iov.iov_buf, 'b', TEST_IOV_SIZE_OUT);
	D_ASSERTF(iov.iov_buf != NULL, "Failed to allocate iov buf\n");

	iov.iov_buf_len = TEST_IOV_SIZE_OUT;
	iov.iov_len = TEST_IOV_SIZE_OUT;

	output = crt_reply_get(rpc);
	output->test_data = iov;

	rc = d_sgl_init(&sgl, 1);
	D_ASSERTF(rc == 0, "d_sgl_init() failed; rc=%d\n", rc);

	sgl.sg_iovs[0].iov_buf = g_iov;
	sgl.sg_iovs[0].iov_buf_len = TEST_IOV_SIZE_IN;
	sgl.sg_iovs[0].iov_len = TEST_IOV_SIZE_IN;

	rc = crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RW, &local_bulk);
	D_ASSERTF(rc == 0, "crt_bulk_create() failed; rc=%d\n", rc);


	RPC_PUB_ADDREF(rpc);

	bulk_desc.bd_rpc = rpc;
	bulk_desc.bd_bulk_op = CRT_BULK_GET;
	bulk_desc.bd_remote_hdl = input->bulk_hdl;
	bulk_desc.bd_remote_off = 0;
	bulk_desc.bd_local_hdl = local_bulk;
	bulk_desc.bd_local_off = 0;
	bulk_desc.bd_len = TEST_IOV_SIZE_IN;

	if (input->delay) {
		DBG_PRINT("[RPCID: 0x%lx] Delaying bulk transfer by %ld seconds\n", rpcid, input->delay);
		sleep(input->delay);
	}


	rc = crt_bulk_transfer(&bulk_desc, bulk_transfer_done_cb, iov.iov_buf, NULL);
	D_ASSERTF(rc == 0, "crt_bulk_transfer() failed; rc=%d\n", rc);


	return 0;
}

int handler_set_group_info(crt_rpc_t *rpc)
{
	return 0;
}

int handler_shutdown(crt_rpc_t *rpc)
{
	DBG_PRINT("Shutdown handler called!\n");
	crt_reply_send(rpc);

	crtu_progress_stop();
	return 0;
}
#endif
