/* Copyright (C) 2019 Intel Corporation
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
#ifndef __NO_PMIX_LAUNCHER_COMMON_H__
#define __NO_PMIX_LAUNCHER_COMMON_H__

#define MY_BASE 0x010000000
#define MY_VER  0

#define NUM_SERVER_CTX 8
#define TEST_IOV_SIZE_IN 4096

/* TODO: Revert back to 4096 when CART-789 is fixed */
#define TEST_IOV_SIZE_OUT 2096


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

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

	crt_context_idx(rpc->cr_ctx, &my_tag);

	DBG_PRINT("Ping handler called on tag: %d\n", my_tag);
	if (my_tag != input->tag) {
		D_ERROR("Request was sent to wrong tag. Expected %lu got %d\n",
			input->tag, my_tag);
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

static int g_do_shutdown;

int handler_shutdown(crt_rpc_t *rpc)
{
	DBG_PRINT("Shutdown handler called!\n");
	crt_reply_send(rpc);

	g_do_shutdown = true;
	return 0;
}
#endif
