/*
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DUAL_PROVIDER_COMMON_H__
#define __DUAL_PROVIDER_COMMON_H__
static int do_shutdown;
static int g_my_rank;


static void
exit_on_line(int line)
{
	printf("Failed on line %d\n", line);
	exit(0);
}

#define error_exit() exit_on_line(__LINE__)


#define MY_BASE 0x010000000
#define MY_VER  0

#define NUM_PRIMARY_CTX_MAX 8
#define NUM_SECONDARY_CTX_MAX 8

#define SERVER_GROUP_NAME "dual_provider_group"

#define RPC_DECLARE(name)					\
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)	\
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)

enum {
	RPC_PING = CRT_PROTO_OPC(MY_BASE, MY_VER, 0),
	RPC_SHUTDOWN
} rpc_id_t;

#define CRT_ISEQ_RPC_PING	/* input fields */		 \
	((crt_bulk_t)		(bulk_hdl1)		CRT_VAR) \
	((crt_bulk_t)		(bulk_hdl2)		CRT_VAR) \
	((uint32_t)		(size1)			CRT_VAR) \
	((uint32_t)		(size2)			CRT_VAR)

#define CRT_OSEQ_RPC_PING	/* output fields */		 \
	((crt_bulk_t)		(ret_bulk)		CRT_VAR) \
	((int32_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_SHUTDOWN	/* input fields */		 \
	((uint32_t)		(field)			CRT_VAR)

#define CRT_OSEQ_RPC_SHUTDOWN	/* output fields */		 \
	((uint32_t)		(field)			CRT_VAR)


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

	DBG_PRINT("Bulk transfer failed with rc=%d\n", info->bci_rc);
	if (info->bci_rc != 0) {
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
	crt_context_t		*ctx;
	int			rc = 0;
	bool			primary_origin = false;
	int			my_tag;
	uint32_t		hdr_dst_tag;

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

	output->rc = 0;
	DBG_PRINT("Sizes: %d %d\n", input->size1, input->size2);

	ctx = rpc->cr_ctx;

	rc = crt_req_src_provider_is_primary(rpc, &primary_origin);

	if (rc != 0) {
		D_ERROR("crt_req_src_provider_is_primary() failed. rc=%d\n", rc);
		error_exit();
	}

	rc = crt_req_dst_tag_get(rpc, &hdr_dst_tag);
	if (rc != 0) {
		D_ERROR("crt_req_dst_tag_get() failed; rc=%d\n", rc);
		error_exit();
	}

	rc = crt_context_idx(rpc->cr_ctx, &my_tag);
	if (rc != 0) {
		D_ERROR("crt_context_idx() failed; rc=%d\n", rc);
		error_exit();
	}

	DBG_PRINT("RPC arived on a %s context (idx=%d intended_tag=%d); origin was %s\n",
		  crt_context_is_primary(ctx) ? "primary" : "secondary",
		  my_tag, hdr_dst_tag,
		  primary_origin ? "primary" : "secondary");

	/* TODO: Change this to rank == 2 when bulk support is added */
	if (g_my_rank == 100002) {
		struct crt_bulk_desc	bulk_desc;
		crt_bulk_t		dst_bulk;
		char			*dst;
		d_sg_list_t		sgl;

		DBG_PRINT("Initiating transfer\n");

		D_ALLOC_ARRAY(dst, input->size2);

		rc = d_sgl_init(&sgl, 1);
		if (rc != 0)
			error_exit();

		sgl.sg_iovs[0].iov_buf = dst;
		sgl.sg_iovs[0].iov_buf_len = input->size2;
		sgl.sg_iovs[0].iov_len = input->size2;

		rc = crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RW, &dst_bulk);
		if (rc != 0)
			error_exit();

		RPC_PUB_ADDREF(rpc);
		bulk_desc.bd_rpc = rpc;
		bulk_desc.bd_bulk_op = CRT_BULK_GET;
		bulk_desc.bd_remote_hdl = input->bulk_hdl2;
		bulk_desc.bd_remote_off = 0;
		bulk_desc.bd_local_hdl = dst_bulk;
		bulk_desc.bd_local_off = 0;
		bulk_desc.bd_len = input->size2;
		rc = crt_bulk_bind_transfer(&bulk_desc, bulk_transfer_done_cb,
					    dst, NULL);
		if (rc != 0) {
			D_ERROR("transfer failed; rc=%d\n", rc);
			error_exit();
		}
	}

	rc = crt_reply_send(rpc);
	if (rc)
		D_ERROR("Failed with rc=%d\n", rc);

	return 0;
}


static int
handler_shutdown(crt_rpc_t *rpc)
{
	crt_reply_send(rpc);

	do_shutdown = 1;
	return 0;
}

#endif /* __DUAL_PROVIDER_COMMON_H__ */
