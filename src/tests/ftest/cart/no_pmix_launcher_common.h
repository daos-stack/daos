/*
 * (C) Copyright 2019-2021 Intel Corporation.
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

int handler_ping(crt_rpc_t *rpc)
{
	struct RPC_PING_in	*input;
	struct RPC_PING_out	*output;
	d_iov_t			iov;
	int			my_tag;
	d_rank_t		hdr_dst_rank;
	uint32_t		hdr_dst_tag;
	d_rank_t		hdr_src_rank;
	int			rc;

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

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

	output->test_data = iov;
	crt_reply_send(rpc);

	D_FREE(iov.iov_buf);
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

	tc_progress_stop();
	return 0;
}
#endif
