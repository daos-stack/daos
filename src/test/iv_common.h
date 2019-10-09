/* Copyright (C) 2016-2018 Intel Corporation
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
/**
 * This is a common file for IV client and IV server
 */
#include <unistd.h>
#include <cart/api.h>
#include <cart/iv.h>

#include "tests_common.h"

/* Describes internal structure of the value */
#define MAX_DATA_SIZE 1024

#define IV_GRP_NAME "IV_TEST"

#define TEST_IV_BASE 0x010000000
#define TEST_IV_VER  0


/* Describes internal structure of a key */
struct iv_key_struct {
	d_rank_t	rank;
	uint32_t	key_id;
};

#define CRT_ISEQ_RPC_TEST_FETCH_IV /* input fields */		 \
	((d_iov_t)		(key)			CRT_VAR) \
	((crt_bulk_t)		(bulk_hdl)		CRT_VAR)

#define CRT_OSEQ_RPC_TEST_FETCH_IV /* output fields */		 \
	((d_iov_t)		(key)			CRT_VAR) \
	((uint64_t)		(size)			CRT_VAR) \
	((int64_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_TEST_UPDATE_IV /* input fields */		 \
	((d_iov_t)		(iov_key)		CRT_VAR) \
	((d_iov_t)		(iov_sync)		CRT_VAR) \
	((d_iov_t)		(iov_value)		CRT_VAR)

#define CRT_OSEQ_RPC_TEST_UPDATE_IV /* output fields */		 \
	((int64_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_TEST_INVALIDATE_IV /* input fields */	 \
	((d_iov_t)		(iov_key)		CRT_VAR)

#define CRT_OSEQ_RPC_TEST_INVALIDATE_IV /* output fields */	 \
	((int64_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_SET_IVNS	/* input fields */		 \
	((d_iov_t)		(global_ivns_iov)	CRT_VAR)

#define CRT_OSEQ_RPC_SET_IVNS	/* output fields */		 \
	((uint32_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_SHUTDOWN	/* input fields */		 \
	((uint32_t)		(unused)		CRT_VAR)

#define CRT_OSEQ_RPC_SHUTDOWN	/* output fields */		 \
	((uint32_t)		(rc)			CRT_VAR)

#ifdef _SERVER
#define RPC_REGISTER(name) \
	CRT_RPC_SRV_REGISTER(name, 0, name, DQF_FUNC_##name)
#else
#define RPC_REGISTER(name) \
	CRT_RPC_REGISTER(name, 0, name)
#endif

#define RPC_DECLARE(name, function)					\
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)		\
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)

enum {
	/* Client issues fetch call */
	RPC_TEST_FETCH_IV = CRT_PROTO_OPC(TEST_IV_BASE, TEST_IV_VER, 0),
	 /* Client issues update call */
	RPC_TEST_UPDATE_IV = CRT_PROTO_OPC(TEST_IV_BASE, TEST_IV_VER, 1),
	/* Client issues invalidate call */
	RPC_TEST_INVALIDATE_IV = CRT_PROTO_OPC(TEST_IV_BASE,
							TEST_IV_VER, 2),
	/* send global ivns */
	RPC_SET_IVNS = CRT_PROTO_OPC(TEST_IV_BASE, TEST_IV_VER, 3),
	/* Request server shutdown */
	RPC_SHUTDOWN = CRT_PROTO_OPC(TEST_IV_BASE, TEST_IV_VER, 4),
} rpc_id_t;

int iv_test_fetch_iv(crt_rpc_t *rpc);
int iv_test_update_iv(crt_rpc_t *rpc);
int iv_test_invalidate_iv(crt_rpc_t *rpc);

int iv_set_ivns(crt_rpc_t *rpc);
int iv_shutdown(crt_rpc_t *rpc);

RPC_DECLARE(RPC_TEST_FETCH_IV, iv_test_fetch_iv);
RPC_DECLARE(RPC_TEST_UPDATE_IV, iv_test_update_iv);
RPC_DECLARE(RPC_TEST_INVALIDATE_IV, iv_test_invalidate_iv);
RPC_DECLARE(RPC_SET_IVNS, iv_set_ivns);
RPC_DECLARE(RPC_SHUTDOWN, iv_shutdown);

#ifdef _SERVER
#define PRF_ENTRY(x, y)			\
{					\
	.prf_flags = 0,			\
	.prf_req_fmt = &x,		\
	.prf_hdlr = (void *)y,		\
	.prf_co_ops = NULL,		\
}
#else
#define PRF_ENTRY(x, y)			\
{					\
	.prf_flags = 0,			\
	.prf_req_fmt = &x,		\
	.prf_hdlr = NULL,		\
	.prf_co_ops = NULL,		\
}

#endif

static struct crt_proto_rpc_format my_proto_rpc_fmt_iv[] = {
	PRF_ENTRY(CQF_RPC_TEST_FETCH_IV, iv_test_fetch_iv),
	PRF_ENTRY(CQF_RPC_TEST_UPDATE_IV, iv_test_update_iv),
	PRF_ENTRY(CQF_RPC_TEST_INVALIDATE_IV, iv_test_invalidate_iv),
	PRF_ENTRY(CQF_RPC_SET_IVNS, iv_set_ivns),
	PRF_ENTRY(CQF_RPC_SHUTDOWN, iv_shutdown),
};

static struct crt_proto_format my_proto_fmt_iv = {
	.cpf_name = "my-proto-iv",
	.cpf_ver = TEST_IV_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_iv),
	.cpf_prf = &my_proto_rpc_fmt_iv[0],
	.cpf_base = TEST_IV_BASE,
};
void
rpc_handle_reply(const struct crt_cb_info *info)
{
	int *done;
	crt_rpc_t *rpc_req = NULL;

	rpc_req = info->cci_rpc;
	crt_req_addref(rpc_req);

	done = info->cci_arg;
	*done = 1;
}

int prepare_rpc_request(crt_context_t crt_ctx, int rpc_id,
			crt_endpoint_t *server_ep, void **input,
			crt_rpc_t **rpc_req)
{
	int rc;

	rc = crt_req_create(crt_ctx, server_ep, rpc_id, rpc_req);
	assert(rc == 0);

	*input = crt_req_get(*rpc_req);

	return rc;
}

int send_rpc_request(crt_context_t crt_ctx, crt_rpc_t *rpc_req, void **output)
{
	int rc;
	int done;

	done = 0;
	rc = crt_req_send(rpc_req, rpc_handle_reply, &done);
	assert(rc == 0);

	while (!done)
		sched_yield();

	*output = crt_reply_get(rpc_req);
	return rc;
}

/** Prints a buffer as hex to a file without any newlines/spaces/etc */
static inline void
print_hex(void *buf, size_t len, FILE *log_file)
{
	uint8_t *bytes = (uint8_t *)buf;

	if (bytes == NULL)
		return;

	for (; len > 0; len--) {
		fprintf(log_file, "%02X", *bytes);
		bytes++;
	}
}
