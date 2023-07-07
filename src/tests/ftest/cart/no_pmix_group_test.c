/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <cart/api.h>

#include "crt_utils.h"

/*
 * By default expect RPCs to finish in 10 seconds; increase timeout for
 * when running under the valgrind
 */
static int g_exp_rpc_timeout = 10;

#define MY_BASE 0x010000000
#define MY_VER  0

#define NUM_SERVER_CTX 8

#define RPC_DECLARE(name)					\
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)	\
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)	\

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
	d_rank_t		hdr_src_rank;
	int			rc;

	rc = crt_req_src_rank_get(rpc, &hdr_src_rank);
	D_ASSERTF(rc == 0, "crt_req_src_rank_get() failed; rc=%d\n", rc);

	DBG_PRINT("CORPC_HANDLER called (src_rank=%d)\n", hdr_src_rank);

	crt_reply_send(rpc);

	return 0;
}

static int
handler_ping(crt_rpc_t *rpc)
{
	struct RPC_PING_in	*input;
	int			my_tag;
	d_rank_t		hdr_dst_rank;
	uint32_t		hdr_dst_tag;
	d_rank_t		hdr_src_rank;
	int			rc;

	input = crt_req_get(rpc);

	crt_context_idx(rpc->cr_ctx, &my_tag);

	if (my_tag != input->tag) {
		D_ERROR("Request was sent to wrong tag. Expected %lu got %d\n",
			input->tag, my_tag);
		assert(0);
	}

	rc = crt_req_src_rank_get(rpc, &hdr_src_rank);
	D_ASSERTF(rc == 0, "crt_req_src_rank_get() failed; rc=%d\n", rc);

	rc = crt_req_dst_rank_get(rpc, &hdr_dst_rank);
	D_ASSERTF(rc == 0, "crt_req_dst_rank_get() failed; rc=%d\n", rc);

	rc = crt_req_dst_tag_get(rpc, &hdr_dst_tag);
	D_ASSERTF(rc == 0, "crt_req_dst_tag_get() failed; rc=%d\n", rc);

	DBG_PRINT("Ping handler called on %d:%d (src=%d)\n",
		hdr_dst_rank, hdr_dst_tag, hdr_src_rank);

	crt_reply_send(rpc);
	return 0;
}

static int
handler_shutdown(crt_rpc_t *rpc)
{
	DBG_PRINT("Shutdown handler called!\n");
	crt_reply_send(rpc);

	crtu_progress_stop();
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
__dump_ranklist(const char *msg, d_rank_list_t *rl)
{
	int i;

	DBG_PRINT("%s", msg);
	for (i = 0; i < rl->rl_nr; i++)
		DBG_PRINT("rank[%d] = %d\n", i, rl->rl_ranks[i]);

}

static void
__verify_ranks(crt_group_t *grp, d_rank_t *exp_ranks, int size, int line)
{
	uint32_t	grp_size;
	d_rank_list_t	*rank_list;
	int		i;
	int		rc;
	d_rank_list_t	*sorted_list;
	d_rank_list_t	exp_list;
	d_rank_list_t	*exp_sorted;

	exp_list.rl_nr = size;
	exp_list.rl_ranks = exp_ranks;

	rc = d_rank_list_dup_sort_uniq(&exp_sorted, &exp_list);
	if (rc != 0) {
		D_ERROR("d_rank_list_dup_sort_uniq() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_group_size(grp, &grp_size);
	if (rc != 0) {
		D_ERROR("Line:%d crt_group_size() failed; rc=%d\n", line, rc);
		assert(0);
	}

	if (grp_size != size) {
		D_ERROR("Line:%d group_size expected=%d got=%d\n",
			line, size, grp_size);
		assert(0);
	}

	rc = crt_group_ranks_get(grp, &rank_list);
	if (rc != 0) {
		D_ERROR("Line:%d crt_group_ranks_get() failed; rc=%d\n",
			line, rc);
		assert(0);
	}

	if (rank_list->rl_nr != size) {
		D_ERROR("Line:%d rank_list size expected=%d got=%d\n",
			line, size, rank_list->rl_nr);
		assert(0);
	}

	rc = d_rank_list_dup_sort_uniq(&sorted_list, rank_list);
	if (rc != 0) {
		D_ERROR("Line:%d d_rank_list_dup_sort_uniq() failed; rc=%d\n",
			line, rc);
		assert(0);
	}

	for (i = 0; i < size; i++) {
		if (sorted_list->rl_ranks[i] != exp_sorted->rl_ranks[i]) {
			D_ERROR("Line:%d rank_list[%d] expected=%d got=%d\n",
				line, i, sorted_list->rl_ranks[i],
				exp_sorted->rl_ranks[i]);
			__dump_ranklist("Expected\n", exp_sorted);
			__dump_ranklist("Actual\n", sorted_list);
			assert(0);
		}
	}

	d_rank_list_free(rank_list);
	d_rank_list_free(sorted_list);
	d_rank_list_free(exp_sorted);
}

#define VERIFY_RANKS(grp, list...)			\
	do {						\
		d_rank_t __exp[] = {list};		\
		__verify_ranks(grp, __exp,		\
				ARRAY_SIZE(__exp),	\
				__LINE__);	\
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
	struct test_options	*opts = crtu_get_opts();
	d_rank_list_t		*mod_ranks;
	uint64_t		*incarnations;
	char			*uris[10];
	d_rank_list_t		*mod_prim_ranks;
	d_rank_list_t		*mod_sec_ranks;
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
	int			num_attach_retries = 20;
	uint32_t		primary_grp_version = 1;

	env_self_rank = getenv("CRT_L_RANK");
	my_rank = atoi(env_self_rank);

	/* When under valgrind bump expected timeouts to 60 seconds */
	if (D_ON_VALGRIND) {
		DBG_PRINT("Valgrind env detected. bumping timeouts\n");
		g_exp_rpc_timeout = 60;
		num_attach_retries = 60;
	}

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(my_rank, num_attach_retries, true, true);

	if (D_ON_VALGRIND)
		crtu_set_shutdown_delay(5);

	rc = d_log_init();
	assert(rc == 0);

	DBG_PRINT("Server starting up\n");
	rc = crt_init(NULL, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
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

	rc = crt_group_auto_rank_remove(grp, true);
	if (rc != 0) {
		D_ERROR("crt_group_auto_rank_remove() failed; rc=%d\n", rc);
		assert(0);
	}

	for (i = 0; i < NUM_SERVER_CTX; i++) {
		rc = crt_context_create(&crt_ctx[i]);
		if (rc != 0) {
			D_ERROR("crt_context_create() failed; rc=%d\n", rc);
			assert(0);
		}

		rc = pthread_create(&progress_thread[i], 0,
				    crtu_progress_fn, &crt_ctx[i]);
		assert(rc == 0);
	}

	if (opts->is_swim_enabled) {
		rc = crt_swim_init(0);
		if (rc != 0) {
			D_ERROR("crt_swim_init() failed; rc=%d\n", rc);
			assert(0);
		}
	}

	grp_cfg_file = getenv("CRT_L_GRP_CFG");

	rc = crt_rank_self_set(my_rank, primary_grp_version);
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
	rc = crtu_load_group_from_file(grp_cfg_file, crt_ctx[0], grp, my_rank,
				       true);
	if (rc != 0) {
		D_ERROR("crtu_load_group_from_file() failed; rc=%d\n", rc);
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

		__verify_ranks(sec_grp1, sec_ranks, i + 1, __LINE__);
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

	VERIFY_RANKS(sec_grp1, 6, 7, 9, 10, 41, 42, 43);

	/* Add new sec_rank=50 after the removal of previous one */
	rc = crt_group_secondary_rank_add(sec_grp1, 50, 2);
	if (rc != 0) {
		D_ERROR("Rank addition failed; rc=%d\n", rc);
		assert(0);
	}

	VERIFY_RANKS(sec_grp1, 6, 7, 9, 10, 41, 42, 43, 50);

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

	/* Add existing secondary rank with bogus primary one */
	rc = crt_group_secondary_rank_add(sec_grp1, 50, 15);
	if (rc != -DER_EXIST) {
		D_ERROR("Expected -DER_EXIST got %d\n", rc);
		assert(0);
	}

	/* Add already existing primary rank - Negative test */
	rc = crt_group_secondary_rank_add(sec_grp1, 50, 2);
	if (rc != -DER_EXIST) {
		D_ERROR("Expected -DER_EXIST got %d\n", rc);
		assert(0);
	}

	/* Remove non existent rank - Negative test */
	rc = crt_group_rank_remove(sec_grp1, 105);
	if (rc != -DER_OOG) {
		D_ERROR("Expected -DER_OOG got %d\n", rc);
		assert(0);
	}

	/* All ranks except for 0 wait for RPCs. rank=0 initiates test */
	if (my_rank != 1)
		D_GOTO(join, 0);

	rc = crt_group_ranks_get(grp, &rank_list);
	if (rc != 0) {
		D_ERROR("crt_group_ranks_get() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crtu_wait_for_ranks(crt_ctx[0], grp, rank_list, 0,
				 NUM_SERVER_CTX, 50, 100.0);
	if (rc != 0) {
		D_ERROR("wait_for_ranks() failed; rc=%d\n", rc);
		assert(0);
	}

	D_FREE(rank_list->rl_ranks);
	D_FREE(rank_list);
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
			if (rc != 0) {
				D_ERROR("crt_req_send() failed; rc=%d\n", rc);
				assert(0);
			}
			crtu_sem_timedwait(&sem, g_exp_rpc_timeout, __LINE__);
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
	crtu_sem_timedwait(&sem, g_exp_rpc_timeout, __LINE__);
	DBG_PRINT("CORRPC to secondary group finished\n");

	/* Send shutdown RPC to all nodes except for self */
	DBG_PRINT("Sending shutdown to all nodes\n");

	/* Note rank at i=1 corresponds to 'self' */
	for (i = 0; i < rank_list->rl_nr; i++) {
		rank = rank_list->rl_ranks[i];

		if (i == 1)
			continue;

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
		crtu_sem_timedwait(&sem, g_exp_rpc_timeout, __LINE__);
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

	VERIFY_RANKS(sec_grp1, 6, 7, 9, 10, 41, 42, 43);
	VERIFY_RANKS(grp, 0, 1, 3, 4, 5, 6, 7);

	DBG_PRINT("----------------------------\n");
	DBG_PRINT("Testing crt_group_primary_modify()\n");

	mod_ranks = d_rank_list_alloc(10);
	if (!mod_ranks) {
		D_ERROR("rank list allocation failed\n");
		assert(0);
	}

	D_ALLOC_ARRAY(incarnations, mod_ranks->rl_nr);
	if (!incarnations) {
		D_ERROR("incarnation list allocation failed\n");
		assert(0);
	}

	for (i = 0; i < 10; i++) {
		rc = asprintf(&uris[i], "ofi+tcp;ofi_rxm://127.0.0.1:%d",
				10000 + i);
		if (rc == -1) {
			D_ERROR("asprintf() failed\n");
			assert(0);
		}
		mod_ranks->rl_ranks[i] = i + 1;
		incarnations[i] = i + 1;
	}

	DBG_PRINT("primary modify: Add\n");
	primary_grp_version++;
	rc = crt_group_primary_modify(grp, &crt_ctx[1], 1, mod_ranks, incarnations, uris,
				      CRT_GROUP_MOD_OP_ADD, primary_grp_version);
	if (rc != 0) {
		D_ERROR("crt_group_primary_modify() failed; rc = %d\n", rc);
		assert(0);
	}

	VERIFY_RANKS(grp, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);

	mod_ranks->rl_ranks[0] = 0;
	mod_ranks->rl_ranks[1] = 5;
	mod_ranks->rl_ranks[2] = 11;
	mod_ranks->rl_ranks[3] = 15;
	mod_ranks->rl_ranks[4] = 18;
	mod_ranks->rl_nr = 5;

	DBG_PRINT("primary modify: Replace\n");
	primary_grp_version++;
	rc = crt_group_primary_modify(grp, &crt_ctx[1], 1, mod_ranks, incarnations, uris,
				      CRT_GROUP_MOD_OP_REPLACE, primary_grp_version);
	if (rc != 0) {
		D_ERROR("crt_group_primary_modify() failed; rc=%d\n", rc);
		assert(0);
	}

	VERIFY_RANKS(grp, 0, 5, 11, 15, 18);
	VERIFY_RANKS(sec_grp1, 10, 41);

	mod_ranks->rl_ranks[0] = 5;
	mod_ranks->rl_ranks[1] = 15;
	mod_ranks->rl_nr = 2;

	DBG_PRINT("primary modify: Remove\n");
	primary_grp_version++;
	rc = crt_group_primary_modify(grp, &crt_ctx[1], 1, mod_ranks, incarnations, NULL,
				      CRT_GROUP_MOD_OP_REMOVE, primary_grp_version);
	if (rc != 0) {
		D_ERROR("crt_group_primary_modify() failed; rc=%d\n", rc);
		assert(0);
	}

	VERIFY_RANKS(grp, 0, 11, 18);
	VERIFY_RANKS(sec_grp1, 10);

	mod_ranks->rl_ranks[0] = 1;
	mod_ranks->rl_ranks[1] = 2;
	mod_ranks->rl_ranks[2] = 12;
	mod_ranks->rl_nr = 3;

	primary_grp_version++;
	rc = crt_group_primary_modify(grp, &crt_ctx[1], 1, mod_ranks, incarnations, uris,
				      CRT_GROUP_MOD_OP_ADD, primary_grp_version);
	if (rc != 0) {
		D_ERROR("crt_group_primary_modify() failed; rc=%d\n", rc);
		assert(0);
	}

	VERIFY_RANKS(grp, 0, 1, 2, 11, 12, 18);

	D_FREE(incarnations);
	d_rank_list_free(mod_ranks);

	/* Allocated above with asprintf */
	for (i = 0; i < 10; i++)
		free(uris[i]);

	mod_prim_ranks = d_rank_list_alloc(10);
	mod_sec_ranks = d_rank_list_alloc(10);

	if (!mod_prim_ranks || !mod_sec_ranks) {
		D_ERROR("Failed to allocate lists\n");
		assert(0);
	}

	mod_prim_ranks->rl_ranks[0] = 1;
	mod_prim_ranks->rl_ranks[1] = 2;
	mod_prim_ranks->rl_ranks[2] = 18;

	mod_sec_ranks->rl_ranks[0] = 55;
	mod_sec_ranks->rl_ranks[1] = 102;
	mod_sec_ranks->rl_ranks[2] = 48;

	mod_sec_ranks->rl_nr = 3;
	mod_prim_ranks->rl_nr = 3;

	DBG_PRINT("secondary group: Add\n");
	rc = crt_group_secondary_modify(sec_grp1, mod_sec_ranks,
					mod_prim_ranks, CRT_GROUP_MOD_OP_ADD,
					0X0);
	if (rc != 0) {
		D_ERROR("crt_group_secondary_modify() failed; rc=%d\n", rc);
		assert(0);
	}

	VERIFY_RANKS(sec_grp1, 10, 48, 55, 102);

	mod_prim_ranks->rl_ranks[0] = 0;
	mod_sec_ranks->rl_ranks[0] = 10;

	mod_prim_ranks->rl_ranks[1] = 18;
	mod_sec_ranks->rl_ranks[1] = 55;

	mod_prim_ranks->rl_ranks[2] = 12;
	mod_sec_ranks->rl_ranks[2] = 114;

	mod_prim_ranks->rl_nr = 3;
	mod_sec_ranks->rl_nr = 3;

	DBG_PRINT("secondary group: Replace\n");
	rc = crt_group_secondary_modify(sec_grp1, mod_sec_ranks, mod_prim_ranks,
					CRT_GROUP_MOD_OP_REPLACE, 0x0);
	if (rc != 0) {
		D_ERROR("crt_group_secondary_modify() failed; rc=%d\n", rc);
		assert(0);
	}

	VERIFY_RANKS(sec_grp1, 10, 55, 114);

	mod_sec_ranks->rl_ranks[0] = 55;
	mod_sec_ranks->rl_nr = 1;

	DBG_PRINT("secondary group: Remove\n");
	rc = crt_group_secondary_modify(sec_grp1, mod_sec_ranks, NULL,
					CRT_GROUP_MOD_OP_REMOVE, 0x0);
	if (rc != 0) {
		D_ERROR("crt_group_secondary_modify() failed; rc=%d\n", rc);
		assert(0);
	}

	VERIFY_RANKS(sec_grp1, 10, 114);

	d_rank_list_free(mod_prim_ranks);
	d_rank_list_free(mod_sec_ranks);

	crtu_progress_stop();
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

