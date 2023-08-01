/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include "crt_utils.h"

static bool pre_forward_called;
static bool hdlr_called;
static bool post_reply_called;

static int
corpc_aggregate(crt_rpc_t *src, crt_rpc_t *result, void *priv)
{
	return 0;
}

static int
corpc_pre_forward(crt_rpc_t *rpc, void *arg)
{
	DBG_PRINT("Pre-forward called\n");

	if (hdlr_called == true) {
		D_ERROR("Handler called before pre-forward callback\n");
		assert(0);
	}

	pre_forward_called = true;
	return 0;
}

static int
corpc_post_reply(crt_rpc_t *rpc, void *arg)
{
	DBG_PRINT("Post-reply called\n");
	post_reply_called = true;
	return 0;
}

struct crt_corpc_ops corpc_set_ivns_ops = {
	.co_aggregate = corpc_aggregate,
	.co_pre_forward = corpc_pre_forward,
	.co_post_reply = corpc_post_reply,
};

static void
test_basic_corpc_hdlr(crt_rpc_t *rpc)
{
	int rc;

	DBG_PRINT("Handler called\n");
	if (pre_forward_called == false) {
		D_ERROR("Handler called before pre-forward callback\n");
		assert(0);
	}

	rc = crt_reply_send(rpc);
	assert(rc == 0);

	crtu_progress_stop();
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
	crtu_progress_stop();
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
	d_rank_list_t	excluded_membs;
	d_rank_t	excluded_ranks = {0};
	crt_rpc_t	*rpc;
	d_rank_t	my_rank;
	crt_group_t	*grp;
	char		*env_self_rank;
	char		*grp_cfg_file;
	pthread_t	progress_thread;

	excluded_membs.rl_nr = 1;
	excluded_membs.rl_ranks = &excluded_ranks;

	env_self_rank = getenv("CRT_L_RANK");
	my_rank = atoi(env_self_rank);

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(my_rank, 20, true, true);

	rc = d_log_init();
	assert(rc == 0);

	rc = crt_init(NULL, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	assert(rc == 0);

	rc = crt_proto_register(&my_proto_fmt_basic_corpc);
	assert(rc == 0);

	rc = crt_context_create(&g_main_ctx);
	assert(rc == 0);

	rc = pthread_create(&progress_thread, 0,
			    crtu_progress_fn, &g_main_ctx);
	if (rc != 0) {
		D_ERROR("pthread_create() failed; rc=%d\n", rc);
		assert(0);
	}

	grp_cfg_file = getenv("CRT_L_GRP_CFG");

	rc = crt_rank_self_set(my_rank, 1 /* group_version_min */);
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
	rc = crtu_load_group_from_file(grp_cfg_file, g_main_ctx, grp, my_rank,
				       true);
	if (rc != 0) {
		D_ERROR("crtu_load_group_from_file() failed; rc=%d\n", rc);
		assert(0);
	}

	if (my_rank == 0) {
		rc = crt_group_ranks_get(grp, &rank_list);
		if (rc != 0) {
			D_ERROR("crt_group_ranks_get() failed; rc=%d\n", rc);
			assert(0);
		}

		rc = crtu_wait_for_ranks(g_main_ctx, grp, rank_list,
					 0, 1, 50, 100.0);
		if (rc != 0) {
			D_ERROR("wait_for_ranks() failed; rc=%d\n", rc);
			assert(0);
		}

		d_rank_list_free(rank_list);
		rank_list = NULL;

		DBG_PRINT("Rank 0 sending CORPC call\n");
		rc = crt_corpc_req_create(g_main_ctx, NULL, &excluded_membs,
			CRT_PROTO_OPC(TEST_CORPC_PREFWD_BASE,
				TEST_CORPC_PREFWD_VER, 0), NULL, 0, 0,
			crt_tree_topo(CRT_TREE_KNOMIAL, 4), &rpc);
		assert(rc == 0);

		rc = crt_req_send(rpc, corpc_response_hdlr, NULL);
		assert(rc == 0);
	}

	pthread_join(progress_thread, NULL);
	DBG_PRINT("Test finished\n");

	if (my_rank != 0) {
		if (!post_reply_called) {
			D_ERROR("post_reply callback was not called\n");
			assert(0);
		}
	}

	rc = crt_finalize();
	assert(rc == 0);

	d_log_fini();

	return 0;
}
