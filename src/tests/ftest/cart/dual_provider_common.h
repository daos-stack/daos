/*
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DUAL_PROVIDER_COMMON_H__
#define __DUAL_PROVIDER_COMMON_H__

#define MY_BASE 0x010000000
#define MY_VER  0

#define NUM_PRIMARY_CTX 8
#define NUM_SECONDARY_CTX 8

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
handler_ping(crt_rpc_t *rpc)
{
	struct RPC_PING_in	*input;
	struct RPC_PING_out	*output;
	int			rc = 0;

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

	output->rc = 0;
	DBG_PRINT("Sizes: %d %d\n", input->size1, input->size2);
	rc = crt_reply_send(rpc);
	if (rc)
		D_ERROR("Failed with rc=%d\n", rc);

	return 0;
}

static int do_shutdown;

static int
handler_shutdown(crt_rpc_t *rpc)
{
	crt_reply_send(rpc);
	
	do_shutdown = 1;
	return 0;
}

#endif /* __DUAL_PROVIDER_COMMON_H__ */
