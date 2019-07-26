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
 * Dynamic group testing for primary and secondary groups
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <gurt/common.h>
#include <cart/api.h>

#include "tests_common.h"

struct test_options {
	int		self_rank;
	int		mypid;
};

static struct test_options opts;
static int g_do_shutdown;

#define DBG_PRINT(x...)							\
	do {								\
		fprintf(stderr, "SRV [rank=%d pid=%d]\t",		\
			opts.self_rank,					\
			opts.mypid);					\
		fprintf(stderr, x);					\
	} while (0)

#define MY_BASE 0x010000000
#define MY_VER  0

#define NUM_SERVER_CTX 8

#define RPC_DECLARE(name)						\
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)		\
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)

enum {
	RPC_PING = CRT_PROTO_OPC(MY_BASE, MY_VER, 0),
	CORPC_PING,
	RPC_SHUTDOWN
} rpc_id_t;


#define CRT_ISEQ_RPC_PING	/* input fields */		 \
	((uint64_t)		(tag)			CRT_VAR)

#define CRT_OSEQ_RPC_PING	/* output fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_ISEQ_RPC_SHUTDOWN	/* input fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_OSEQ_RPC_SHUTDOWN	/* output fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_ISEQ_CORPC_PING \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_OSEQ_CORPC_PING \
	((uint64_t)		(field)			CRT_VAR)


RPC_DECLARE(RPC_PING);
RPC_DECLARE(RPC_SHUTDOWN);
RPC_DECLARE(CORPC_PING);

static int
handler_corpc_ping(crt_rpc_t *rpc)
{
	DBG_PRINT("CORPC_HANDLER called\n");
	crt_reply_send(rpc);

	return 0;
}

static int
handler_ping(crt_rpc_t *rpc)
{
	struct RPC_PING_in	*input;
	int			my_tag;

	input = crt_req_get(rpc);

	crt_context_idx(rpc->cr_ctx, &my_tag);

	DBG_PRINT("Ping handler called on tag: %d\n", my_tag);
	if (my_tag != input->tag) {
		D_ERROR("Request was sent to wrong tag. Expected %lu got %d\n",
			input->tag, my_tag);
		assert(0);
	}

	crt_reply_send(rpc);
	return 0;
}

static int
handler_shutdown(crt_rpc_t *rpc)
{
	DBG_PRINT("Shutdown handler called!\n");
	crt_reply_send(rpc);

	g_do_shutdown = true;
	return 0;
}
static int
corpc_aggregate(crt_rpc_t *src, crt_rpc_t *result, void *priv)
{
	struct CORPC_PING_out	*output_src;
	struct CORPC_PING_out	*output_result;


	output_src = crt_reply_get(src);
	output_result = crt_reply_get(result);

	output_result->field = output_src->field;
	return 0;
}

struct crt_corpc_ops corpc_ping_ops = {
	.co_aggregate = corpc_aggregate,
	.co_pre_forward = NULL,
};

struct crt_proto_rpc_format my_proto_rpc_fmt[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_RPC_PING,
		.prf_hdlr	= (void *)handler_ping,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_CORPC_PING,
		.prf_hdlr	= (void *)handler_corpc_ping,
		.prf_co_ops	= &corpc_ping_ops,
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


static void *
progress_function(void *data)
{
	int i;
	crt_context_t *p_ctx = (crt_context_t *)data;

	while (g_do_shutdown == 0)
		crt_progress(*p_ctx, 1000, NULL, NULL);

	/* Progress contexts for a while after shutdown to send response */
	for (i = 0; i < 1000; i++)
		crt_progress(*p_ctx, 1000, NULL, NULL);

	crt_context_destroy(*p_ctx, 1);

	return NULL;
}

static void
__dump_ranks(crt_group_t *grp) {
	d_rank_list_t	*rank_list;
	int		rc;
	int		i;

	rc = crt_group_ranks_get(grp, &rank_list);
	if (rc != 0) {
		D_ERROR("crt_group_ranks_get() failed; rc=%d\n", rc);
		assert(0);
	}

	DBG_PRINT("group '%s' size=%d\n", grp->cg_grpid,
		rank_list->rl_nr);

	DBG_PRINT("Ranks:\n");
	for (i = 0; i < rank_list->rl_nr; i++)
		DBG_PRINT("rank[%d] = %d\n", i, rank_list->rl_ranks[i]);

	D_FREE(rank_list->rl_ranks);
	D_FREE(rank_list);
}

static void
__verify_ranks(crt_group_t *grp, d_rank_t *exp_ranks, int size)
{
	uint32_t	grp_size;
	d_rank_list_t	*rank_list;
	int		i;
	int		rc;


	rc = crt_group_size(grp, &grp_size);
	if (rc != 0) {
		D_ERROR("crt_group_size() failed; rc=%d\n", rc);
		assert(0);
	}

	if (grp_size != size) {
		D_ERROR("group_size expected=%d got=%d\n",
			size, grp_size);
		assert(0);
	}

	rc = crt_group_ranks_get(grp, &rank_list);
	if (rc != 0) {
		D_ERROR("crt_group_ranks_get() failed; rc=%d\n", rc);
		assert(0);
	}

	if (rank_list->rl_nr != size) {
		D_ERROR("rank_list size expected=%d got=%d\n",
			size, rank_list->rl_nr);
		assert(0);
	}

	for (i = 0; i < size; i++) {
		if (rank_list->rl_ranks[i] != exp_ranks[i]) {
			D_ERROR("rank_list[%d] expected=%d got=%d\n",
				i, rank_list->rl_ranks[i],
				exp_ranks[i]);
			assert(0);
		}
	}

	D_FREE(rank_list->rl_ranks);
	D_FREE(rank_list);
}

#define VERIFY_RANKS(grp, list...)			\
	do {						\
		d_rank_t __exp[] = {list};		\
		__verify_ranks(grp, __exp,		\
				ARRAY_SIZE(__exp));	\
	} while (0)


static void
rpc_handle_reply(const struct crt_cb_info *info)
{
	sem_t	*sem;

	D_ASSERTF(info->cci_rc == 0, "rpc response failed. rc: %d\n",
		info->cci_rc);

	sem = (sem_t *)info->cci_arg;
	sem_post(sem);
}

int main(int argc, char **argv)
{
	crt_group_t		*grp;
	crt_context_t		crt_ctx[NUM_SERVER_CTX];
	pthread_t		progress_thread[NUM_SERVER_CTX];
	int			i;
	char			*my_uri;
	char			*env_self_rank;
	d_rank_t		my_rank;
	char			*grp_cfg_file;
	uint32_t		grp_size;
	d_rank_t		tmp_rank;
	crt_group_t		*sec_grp1;
	d_rank_list_t		*rank_list;
	crt_endpoint_t		server_ep;
	d_rank_t		rank;
	crt_rpc_t		*rpc;
	struct RPC_PING_in	*input;
	sem_t			sem;
	int			tag;
	int			rc;

	env_self_rank = getenv("CRT_L_RANK");
	my_rank = atoi(env_self_rank);

	/* Set up for DBG_PRINT */
	opts.self_rank = my_rank;
	opts.mypid = getpid();

	rc = d_log_init();
	assert(rc == 0);

	DBG_PRINT("Server starting up\n");
	rc = crt_init(NULL, CRT_FLAG_BIT_SERVER |
		CRT_FLAG_BIT_PMIX_DISABLE | CRT_FLAG_BIT_LM_DISABLE);
	if (rc != 0) {
		D_ERROR("crt_init() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_proto_register(&my_proto_fmt);
	if (rc != 0) {
		D_ERROR("crt_proto_register() failed; rc=%d\n", rc);
		assert(0);
	}

	grp = crt_group_lookup(NULL);
	if (!grp) {
		D_ERROR("Failed to lookup group\n");
		assert(0);
	}

	for (i = 0; i < NUM_SERVER_CTX; i++) {
		rc = crt_context_create(&crt_ctx[i]);
		if (rc != 0) {
			D_ERROR("crt_context_create() failed; rc=%d\n", rc);
			assert(0);
		}

		rc = pthread_create(&progress_thread[i], 0,
				progress_function, &crt_ctx[i]);
		assert(rc == 0);
	}

	grp_cfg_file = getenv("CRT_L_GRP_CFG");

	rc = crt_rank_self_set(my_rank);
	if (rc != 0) {
		D_ERROR("crt_rank_self_set(%d) failed; rc=%d\n",
			my_rank, rc);
		assert(0);
	}

	rc = crt_rank_uri_get(grp, my_rank, 0, &my_uri);
	if (rc != 0) {
		D_ERROR("crt_rank_uri_get() failed; rc=%d\n", rc);
		assert(0);
	}

	/* load group info from a config file and delete file upon return */
	rc = tc_load_group_from_file(grp_cfg_file, crt_ctx[0], grp, my_rank,
					true);
	if (rc != 0) {
		D_ERROR("tc_load_group_from_file() failed; rc=%d\n", rc);
		assert(0);
	}

	DBG_PRINT("self_rank=%d uri=%s grp_cfg_file=%s\n", my_rank,
			my_uri, grp_cfg_file);
	D_FREE(my_uri);

	rc = crt_group_size(NULL, &grp_size);
	if (rc != 0) {
		D_ERROR("crt_group_size() failed; rc=%d\n", rc);
		assert(0);
	}

	if (grp_size != 8) {
		D_ERROR("This test expects 8 instances of servers; got=%d\n",
			grp_size);
		assert(0);
	}

	DBG_PRINT("--------------------------------------------------------\n");
	rc = crt_group_secondary_create("sec_group1", grp, NULL,
					&sec_grp1);
	if (rc != 0) {
		D_ERROR("crt_group_secondary_create() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_group_size(sec_grp1, &grp_size);
	if (rc != 0) {
		D_ERROR("crt_group_size() failed; rc=%d\n", rc);
		assert(0);
	}

	if (grp_size != 0) {
		D_ERROR("Expected group_size=0 got=%d\n", grp_size);
		assert(0);
	}

	d_rank_t real_ranks[] = {0, 1, 2, 3, 4, 5,  6,  7};
	d_rank_t sec_ranks[] = {10, 9, 8, 7, 6, 41, 42, 43};

	/* Populate secondary group with 1 rank at a time */
	for (i = 0 ; i < 8; i++) {
		rc = crt_group_secondary_rank_add(sec_grp1,
					sec_ranks[i], real_ranks[i]);
		if (rc != 0) {
			D_ERROR("Rank addition failed; rc=%d\n", rc);
			assert(0);
		}

		__verify_ranks(sec_grp1, sec_ranks, i + 1);
	}

	/* Verify primary to secondary and secondary to primary conversion */
	for (i = 0; i < 8; i++) {
		rc = crt_group_rank_s2p(sec_grp1, sec_ranks[i], &tmp_rank);
		if (rc != 0) {
			D_ERROR("crt_group_rank_s2p() failed; rc=%d\n", rc);
			assert(0);
		}

		if (real_ranks[i] != tmp_rank) {
			D_ERROR("Expected rank=%d got=%d\n",
				real_ranks[i], tmp_rank);
			assert(0);
		}

		rc = crt_group_rank_p2s(sec_grp1, real_ranks[i], &tmp_rank);
		if (rc != 0) {
			D_ERROR("crt_group_rank_p2s() failed; rc=%d\n", rc);
			assert(0);
		}

		if (sec_ranks[i] != tmp_rank) {
			D_ERROR("Expected rank=%d got %d\n",
				sec_ranks[i], tmp_rank);
			assert(0);
		}
	}

	/* Test removal of the rank from the middle of the list */
	rc = crt_group_rank_remove(sec_grp1, 8);
	if (rc != 0) {
		D_ERROR("crt_group_rank_remove() failed; rc=%d\n", rc);
		assert(0);
	}

	VERIFY_RANKS(sec_grp1, 10, 9, 7, 6, 41, 42, 43);

	/* Add new sec_rank=50 after the removal of previous one */
	rc = crt_group_secondary_rank_add(sec_grp1, 50, 2);
	if (rc != 0) {
		D_ERROR("Rank addition failed; rc=%d\n", rc);
		assert(0);
	}

	VERIFY_RANKS(sec_grp1, 10, 9, 50, 7, 6, 41, 42, 43);

	/* Verify new ranks secondary to primary conversion */
	rc = crt_group_rank_s2p(sec_grp1, 50, &tmp_rank);
	if (rc != 0) {
		D_ERROR("crt_group_rank_s2p() failed; rc=%d\n", rc);
		assert(0);
	}

	if (tmp_rank != 2) {
		D_ERROR("Expected real rank=2 got=%d\n", tmp_rank);
		assert(0);
	}

	/* Add non-existant primary rank - Negative test */
	rc = crt_group_secondary_rank_add(sec_grp1, 50, 15);
	if (rc != -DER_OOG) {
		D_ERROR("Expected -DER_OOG got %d\n", rc);
		assert(0);
	}

	/* Add already existing primary rank - Negative test */
	rc = crt_group_secondary_rank_add(sec_grp1, 50, 2);
	if (rc != -DER_EXIST) {
		D_ERROR("Expected -DER_EXIST got %d\n", rc);
		assert(0);
	}

	/* Remove non existant rank - Negative test */
	rc = crt_group_rank_remove(sec_grp1, 105);
	if (rc != -DER_OOG) {
		D_ERROR("Expected -DER_OOG got %d\n", rc);
		assert(0);
	}


	/* All ranks except for 0 wait for RPCs. rank=0 initiates test */
	if (my_rank != 0)
		D_GOTO(join, 0);

	/* Wait for all servers to load up */
	/* TODO: This will be replaced by proper sync when CART-715 is done */
	sleep(10);

	/* This section only executes for my_rank == 0 */

	DBG_PRINT("------------------------------------\n");
	rc = crt_group_ranks_get(sec_grp1, &rank_list);
	if (rc != 0) {
		D_ERROR("crt_group_ranks_get() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = sem_init(&sem, 0, 0);
	if (rc != 0) {
		D_ERROR("sem_init() failed; rc=%d\n", rc);
		assert(0);
	}

	__dump_ranks(sec_grp1);
	/* Send RPCs to all secondary ranks to all tags in reverse order */
	for (i = 0; i < rank_list->rl_nr; i++) {
		rank = rank_list->rl_ranks[i];

		for (tag = NUM_SERVER_CTX - 1; tag > 0; tag--) {

			server_ep.ep_rank = rank;
			server_ep.ep_grp = sec_grp1;
			server_ep.ep_tag = tag;

			DBG_PRINT("Sending rpc to secondary rank=%d tag=%d\n",
				rank, tag);
			rc = crt_req_create(crt_ctx[1], &server_ep,
					RPC_PING, &rpc);
			if (rc != 0) {
				D_ERROR("crt_req_create() failed; rc=%d\n",
					rc);
				assert(0);
			}

			input = crt_req_get(rpc);
			input->tag = tag;

			rc = crt_req_send(rpc, rpc_handle_reply, &sem);
			tc_sem_timedwait(&sem, 10, __LINE__);
			DBG_PRINT("RPC to rank=%d finished\n", rank);
		}
	}


	DBG_PRINT("All RPCs to secondary ranks are done\n");

	/* Send CORPC over the secondary group */
	DBG_PRINT("Sending CORPC to secondary group\n");
	rc = crt_corpc_req_create(crt_ctx[1], sec_grp1, NULL,
				CORPC_PING, NULL, 0, 0,
				crt_tree_topo(CRT_TREE_KNOMIAL, 4),
				&rpc);
	if (rc != 0) {
		D_ERROR("crt_corpc_req_create() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_req_send(rpc, rpc_handle_reply, &sem);
	if (rc != 0) {
		D_ERROR("crt_req_send() failed; rc=%d\n", rc);
		assert(0);
	}
	tc_sem_timedwait(&sem, 10, __LINE__);
	DBG_PRINT("CORRPC to secondary group finished\n");

	/* Send shutdown RPC to all nodes except for self */
	DBG_PRINT("Senidng shutdown to all nodes\n");
	/* Note rank at i=0 corresponds to 'self' */
	for (i = 1; i < rank_list->rl_nr; i++) {
		rank = rank_list->rl_ranks[i];

		server_ep.ep_rank = rank;
		server_ep.ep_tag = 0;
		server_ep.ep_grp = sec_grp1;

		rc = crt_req_create(crt_ctx[1],
				&server_ep, RPC_SHUTDOWN,
				&rpc);
		if (rc != 0) {
			D_ERROR("crt_req_create() failed; rc=%d\n", rc);
			assert(0);
		}

		rc = crt_req_send(rpc, rpc_handle_reply, &sem);
		if (rc != 0) {
			D_ERROR("crt_req_send() failed; rc=%d\n", rc);
			assert(0);
		}
		tc_sem_timedwait(&sem, 10, __LINE__);
	}
	D_FREE(rank_list->rl_ranks);
	D_FREE(rank_list);

	/*
	 * Test removal of primary rank from primary group. This should
	 * cause secondary group to automatically shrink
	 */
	DBG_PRINT("Testing removal of primary rank\n");
	rc = crt_group_rank_remove(grp, 2);
	if (rc != 0) {
		D_ERROR("Failed to remove rank 50; rc=%d\n", rc);
		assert(0);
	}

	VERIFY_RANKS(sec_grp1, 10, 9, 7, 6, 41, 42, 43);

	/* Shutdown self (prim_rank=0) */
	g_do_shutdown = 1;
	sem_destroy(&sem);

	DBG_PRINT("All tesst succeeded\n");
join:

	/* Wait until shutdown is issued and progress threads exit */
	for (i = 0; i < NUM_SERVER_CTX; i++)
		pthread_join(progress_thread[i], NULL);

	DBG_PRINT("Finished waiting for contexts\n");

	rc = crt_group_secondary_destroy(sec_grp1);
	if (rc != 0) {
		D_ERROR("Failed to destroy a secondary group\n");
		assert(0);
	}

	DBG_PRINT("Destroyed secondary group\n");

	rc = crt_finalize();
	if (rc != 0) {
		D_ERROR("crt_finalize() failed with rc=%d\n", rc);
		assert(0);
	}

	DBG_PRINT("Finalized\n");
	d_log_fini();

	return 0;
}

