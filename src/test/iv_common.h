/* Copyright (C) 2016-2017 Intel Corporation
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
#include <gurt/common.h>
#include <cart/api.h>
#include <cart/iv.h>

/* Describes internal structure of the value */
#define MAX_DATA_SIZE 1024

/* Describes internal structure of a key */
struct iv_key_struct {
	d_rank_t	rank;
	uint32_t	key_id;
};

/* RPC_TEST_FETCH_IV RPC */
struct rpc_test_fetch_iv_in {
	d_iov_t iov_key;
};

struct rpc_test_fetch_iv_out {
	int64_t rc;
};

struct crt_msg_field *arg_test_fetch_iv_in[] = {
	&CMF_IOVEC,
};

struct crt_msg_field *arg_test_fetch_iv_out[] = {
	&CMF_UINT64,
};

/* RCP_TEST_INVALIDATE_IV RPC */
struct rpc_test_invalidate_iv_in {
	d_iov_t iov_key;
};

struct rpc_test_invalidate_iv_out {
	int64_t rc;
};

struct crt_msg_field *arg_test_invalidate_iv_in[] = {
	&CMF_IOVEC,
};

struct crt_msg_field *arg_test_invalidate_iv_out[] = {
	&CMF_UINT64,
};

/* RPC_TEST_UPDATE_IV RPC */
struct rpc_test_update_iv_in {
	d_iov_t	 iov_key;
	d_iov_t	 iov_sync;
	char	*str_value;
};

struct rpc_test_update_iv_out {
	int64_t rc;
};

struct crt_msg_field *arg_test_update_iv_in[] = {
	&CMF_IOVEC,
	&CMF_IOVEC,
	&CMF_STRING,
};

struct crt_msg_field *arg_test_update_iv_out[] = {
	&CMF_UINT64,
};

/* RPC_SET_IVNS */
struct rpc_set_ivns_in {
	d_iov_t global_ivns_iov;
};

struct rpc_set_ivns_out {
	uint32_t rc;
};

struct crt_msg_field *arg_set_ivns_in[] = {
	&CMF_IOVEC,
};

struct crt_msg_field *arg_set_ivns_out[] = {
	&CMF_UINT32,
};

/* RPC_SHUTDOWN */
struct rpc_shutdown_in {
	uint32_t unused;
};

struct rpc_shutdown_out {
	uint32_t rc;
};

struct crt_msg_field *arg_shutdown_in[] = {
	&CMF_UINT32,
};

struct crt_msg_field *arg_shutdown_out[] = {
	&CMF_UINT32,
};

#ifdef _SERVER
#define RPC_REGISTER(name) \
	crt_rpc_srv_register(name, 0, &DQF_##name, DQF_FUNC_##name)
#else
#define RPC_REGISTER(name) \
	crt_rpc_register(name, 0, &DQF_##name)
#endif

#ifdef _SERVER
#define RPC_DECLARE(name, input, output, function)			\
	struct crt_req_format DQF_##name = DEFINE_CRT_REQ_FMT(#name,	\
							      input,	\
							      output);	\
	static void *DQF_FUNC_##name = (void *)function
#else
#define RPC_DECLARE(name, input, output, function)			\
	struct crt_req_format DQF_##name = DEFINE_CRT_REQ_FMT(#name,	\
							      input,	\
							      output)
#endif


enum {
	RPC_TEST_FETCH_IV = 0xB1, /* Client issues fetch call */
	RPC_TEST_UPDATE_IV = 0xB2, /* Client issues update call */
	RPC_TEST_INVALIDATE_IV = 0xB3, /* Client issues invalidate call */
	RPC_SET_IVNS, /* send global ivns */
	RPC_SHUTDOWN, /* Request server shutdown */
} rpc_id_t;

int iv_test_fetch_iv(crt_rpc_t *rpc);
int iv_test_update_iv(crt_rpc_t *rpc);
int iv_test_invalidate_iv(crt_rpc_t *rpc);

int iv_set_ivns(crt_rpc_t *rpc);
int iv_shutdown(crt_rpc_t *rpc);

RPC_DECLARE(RPC_TEST_FETCH_IV,
	    arg_test_fetch_iv_in, arg_test_fetch_iv_out, iv_test_fetch_iv);

RPC_DECLARE(RPC_TEST_UPDATE_IV,
	    arg_test_update_iv_in, arg_test_update_iv_out, iv_test_update_iv);

RPC_DECLARE(RPC_TEST_INVALIDATE_IV,
	    arg_test_invalidate_iv_in, arg_test_invalidate_iv_out,
	    iv_test_invalidate_iv);

RPC_DECLARE(RPC_SET_IVNS,
	    arg_set_ivns_in, arg_set_ivns_out, iv_set_ivns);

RPC_DECLARE(RPC_SHUTDOWN,
	    arg_shutdown_in, arg_shutdown_out, iv_shutdown);

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
		crt_progress(crt_ctx, 10000, NULL, NULL);

	*output = crt_reply_get(rpc_req);

	return rc;
}

static inline void
init_hostname(char *hname, int max_size)
{
	char	*ptr;

	gethostname(hname, max_size);
	ptr = strchr(hname, '.');

	if (ptr)
		*ptr = 0;
}
