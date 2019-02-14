/* Copyright (C) 2018-2019 Intel Corporation
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
 * Basic CORPC test with pre-forward callbacks. Rank0 sends CORPC request to
 * other ranks. All ranks verify that pre-forward callback is called prior
 * to main callback.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <gurt/common.h>
#include <cart/api.h>

static bool pre_forward_called;
static bool hdlr_called;
static int g_do_shutdown;

static int
corpc_aggregate(crt_rpc_t *src, crt_rpc_t *result, void *priv)
{
	return 0;
}

static int
corpc_pre_forward(crt_rpc_t *rpc, void *arg)
{
	D_DEBUG(DB_TEST, "Pre-forward called\n");

	if (hdlr_called == true) {
		D_ERROR("Handler called before pre-forward callback\n");
		assert(0);
	}

	pre_forward_called = true;
	return 0;
}

struct crt_corpc_ops corpc_set_ivns_ops = {
	.co_aggregate = corpc_aggregate,
	.co_pre_forward = corpc_pre_forward,
};

static void
test_basic_corpc_hdlr(crt_rpc_t *rpc)
{
	int rc;

	D_DEBUG(DB_TEST, "Handler called\n");
	if (pre_forward_called == false) {
		D_ERROR("Handler called before pre-forward callback\n");
		assert(0);
	}

	rc = crt_reply_send(rpc);
	assert(rc == 0);

	g_do_shutdown = 1;
}

#define TEST_BASIC_CORPC 0xC1

#define CRT_ISEQ_BASIC_CORPC	/* input fields */		 \
	((uint32_t)		(unused)		CRT_VAR)

#define CRT_OSEQ_BASIC_CORPC	/* output fields */		 \
	((uint32_t)		(unused)		CRT_VAR)

CRT_RPC_DECLARE(basic_corpc, CRT_ISEQ_BASIC_CORPC, CRT_OSEQ_BASIC_CORPC)
CRT_RPC_DEFINE(basic_corpc, CRT_ISEQ_BASIC_CORPC, CRT_OSEQ_BASIC_CORPC)

static void
corpc_response_hdlr(const struct crt_cb_info *info)
{
	g_do_shutdown = 1;
}

int main(void)
{
	int		rc;
	crt_context_t	g_main_ctx;
	d_rank_list_t	excluded_membs;
	d_rank_t	excluded_ranks = {0};
	crt_rpc_t	*rpc;
	d_rank_t	my_rank;
	int		i;

	excluded_membs.rl_nr = 1;
	excluded_membs.rl_ranks = &excluded_ranks;

	rc = d_log_init();
	assert(rc == 0);

	rc = crt_init(NULL, CRT_FLAG_BIT_SERVER);
	assert(rc == 0);

	rc = crt_group_config_save(NULL, true);
	assert(rc == 0);

	rc = CRT_RPC_CORPC_REGISTER(TEST_BASIC_CORPC, basic_corpc,
				    test_basic_corpc_hdlr,
				    &corpc_set_ivns_ops);
	assert(rc == 0);

	rc = crt_context_create(&g_main_ctx);
	assert(rc == 0);

	rc =  crt_group_rank(NULL, &my_rank);

	if (my_rank == 0) {
		D_DEBUG(DB_TEST, "Rank 0 sending CORPC call\n");
		rc = crt_corpc_req_create(g_main_ctx, NULL, &excluded_membs,
				TEST_BASIC_CORPC, NULL, 0, 0,
				crt_tree_topo(CRT_TREE_KNOMIAL, 4), &rpc);
		assert(rc == 0);

		rc = crt_req_send(rpc, corpc_response_hdlr, NULL);
		assert(rc == 0);
	}

	while (!g_do_shutdown)
		crt_progress(g_main_ctx, 1000, NULL, NULL);

	D_DEBUG(DB_TEST, "Shutting down\n");

	/* Progress for a while to make sure we forward to all children */
	for (i = 0; i < 1000; i++)
		crt_progress(g_main_ctx, 1000, NULL, NULL);

	rc = crt_context_destroy(g_main_ctx, true);
	assert(rc == 0);

	if (my_rank == 0) {
		rc = crt_group_config_remove(NULL);
		assert(rc == 0);
	}

	rc = crt_finalize();
	assert(rc == 0);

	d_log_fini();

	return 0;
}
