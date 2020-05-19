/* Copyright (C) 2018-2020 Intel Corporation
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
 * Basic CORPC test to check CRT_FLAG_RPC_FILTER_INVERT flag. Test assumes 5
 * ranks.
 * CORPC with 'shutdown' is sent to 3 ranks, 1,2 and 4.
 * Ranks0 and 3 are expected to not receive this call.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include "tests_common.h"

static d_rank_t my_rank;

static int
corpc_aggregate(crt_rpc_t *src, crt_rpc_t *result, void *priv)
{
	return 0;
}

struct crt_corpc_ops corpc_set_ivns_ops = {
	.co_aggregate = corpc_aggregate,
};

static void
test_basic_corpc_hdlr(crt_rpc_t *rpc)
{
	int rc;

	DBG_PRINT("Handler called\n");

	rc = crt_reply_send(rpc);
	assert(rc == 0);

	tc_progress_stop();

	/* CORPC is not sent to those ranks */
	if (my_rank == 3 || my_rank == 0) {
		D_ERROR("CORPC was sent to wrong rank=%d\n", my_rank);
		assert(0);
	}

}

#define TEST_BASIC_CORPC 0xC1

#define TEST_CORPC_PREFWD_BASE 0x010000000
#define TEST_CORPC_PREFWD_VER  0

#define CRT_ISEQ_BASIC_CORPC	/* input fields */		 \
	((uint32_t)		(unused)		CRT_VAR)

#define CRT_OSEQ_BASIC_CORPC	/* output fields */		 \
	((uint32_t)		(unused)		CRT_VAR)

CRT_RPC_DECLARE(basic_corpc, CRT_ISEQ_BASIC_CORPC, CRT_OSEQ_BASIC_CORPC)
CRT_RPC_DEFINE(basic_corpc, CRT_ISEQ_BASIC_CORPC, CRT_OSEQ_BASIC_CORPC)

static void
corpc_response_hdlr(const struct crt_cb_info *info)
{
	tc_progress_stop();
}

static struct crt_proto_rpc_format my_proto_rpc_fmt_basic_corpc[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_basic_corpc,
		.prf_hdlr	= test_basic_corpc_hdlr,
		.prf_co_ops	= &corpc_set_ivns_ops,
	}
};

static struct crt_proto_format my_proto_fmt_basic_corpc = {
	.cpf_name = "my-proto-basic_corpc",
	.cpf_ver = TEST_CORPC_PREFWD_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_basic_corpc),
	.cpf_prf = &my_proto_rpc_fmt_basic_corpc[0],
	.cpf_base = TEST_CORPC_PREFWD_BASE,
};

int main(void)
{
	int		rc;
	crt_context_t	g_main_ctx;
	d_rank_list_t	*rank_list;
	d_rank_list_t	membs;
	d_rank_t	memb_ranks[] = {1, 2, 4};
	crt_rpc_t	*rpc;
	uint32_t	grp_size;
	crt_group_t	*grp;
	char		*env_self_rank;
	char		*grp_cfg_file;
	pthread_t	progress_thread;

	membs.rl_nr = 3;
	membs.rl_ranks = memb_ranks;

	env_self_rank = getenv("CRT_L_RANK");
	my_rank = atoi(env_self_rank);

	/* rank, num_attach_retries, is_server, assert_on_error */
	tc_test_init(my_rank, 20, true, true);

	rc = d_log_init();
	assert(rc == 0);

	rc = crt_init(NULL, CRT_FLAG_BIT_SERVER |
			CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	assert(rc == 0);

	rc = crt_proto_register(&my_proto_fmt_basic_corpc);
	assert(rc == 0);

	rc = crt_context_create(&g_main_ctx);
	assert(rc == 0);

	rc = pthread_create(&progress_thread, 0,
			tc_progress_fn, &g_main_ctx);
	if (rc != 0) {
		D_ERROR("pthread_create() failed; rc=%d\n", rc);
		assert(0);
	}

	grp_cfg_file = getenv("CRT_L_GRP_CFG");

	rc = crt_rank_self_set(my_rank);
	if (rc != 0) {
		D_ERROR("crt_rank_self_set(%d) failed; rc=%d\n",
			my_rank, rc);
		assert(0);
	}

	grp = crt_group_lookup(NULL);
	if (!grp) {
		D_ERROR("Failed to lookup group\n");
		assert(0);
	}

	/* load group info from a config file and delete file upon return */
	rc = tc_load_group_from_file(grp_cfg_file, g_main_ctx, grp, my_rank,
					true);
	if (rc != 0) {
		D_ERROR("tc_load_group_from_file() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_group_size(grp, &grp_size);
	assert(rc == 0);

	if (grp_size != 5) {
		D_ERROR("This test assumes 5 ranks\n");
		assert(0);
	}

	rc = crt_group_ranks_get(grp, &rank_list);
	if (rc != 0) {
		D_ERROR("crt_group_ranks_get() failed; rc=%d\n", rc);
		assert(0);
	}

	sleep(2);
	rc = tc_wait_for_ranks(g_main_ctx, grp, rank_list, 0,
			1, 10, 100.0);
	if (rc != 0) {
		D_ERROR("wait_for_ranks() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_swim_init(0);
	if (rc != 0) {
		D_ERROR("crt_swim_init() failed; rc=%d\n", rc);
		assert(0);
	}

	d_rank_list_free(rank_list);
	rank_list = NULL;

	if (my_rank == 0) {
		DBG_PRINT("Rank 0 sending CORPC call\n");
		rc = crt_corpc_req_create(g_main_ctx, NULL, &membs,
			CRT_PROTO_OPC(TEST_CORPC_PREFWD_BASE,
				TEST_CORPC_PREFWD_VER, 0), NULL, 0,
			CRT_RPC_FLAG_FILTER_INVERT,
			crt_tree_topo(CRT_TREE_KNOMIAL, 4), &rpc);
		assert(rc == 0);

		rc = crt_req_send(rpc, corpc_response_hdlr, NULL);
		assert(rc == 0);
	}

	sleep(10);
	/* rank=3 is not sent shutdown sequence */
	if (my_rank == 3)
		tc_progress_stop();

	pthread_join(progress_thread, NULL);
	DBG_PRINT("All tests done\n");

	rc = crt_finalize();
	assert(rc == 0);

	d_log_fini();

	return 0;
}
