/*
 * (C) Copyright 2018-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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

#include "tests_common.h"

static int g_ret_code;

#define MY_BASE 0x010000000
#define MY_VER  0

#define NUM_SERVER_CTX 8

#define RPC_DECLARE(name)						\
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)		\
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)

enum {
	RPC_SET_ERR_CODE = CRT_PROTO_OPC(MY_BASE, MY_VER, 0),
	CORPC_TEST,
	RPC_SHUTDOWN
} rpc_id_t;


#define CRT_ISEQ_RPC_SET_ERR_CODE/* input fields */		\
	((int)			(err_code)	CRT_VAR)	\
	((uint32_t)		(pad1)		CRT_VAR)

#define CRT_OSEQ_RPC_SET_ERR_CODE /* output fields */		\
	((uint64_t)		(field)			CRT_VAR)

#define CRT_ISEQ_RPC_SHUTDOWN	/* input fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_OSEQ_RPC_SHUTDOWN	/* output fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_ISEQ_CORPC_TEST \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_OSEQ_CORPC_TEST \
	((uint64_t)		(field)			CRT_VAR)


RPC_DECLARE(RPC_SET_ERR_CODE);
RPC_DECLARE(RPC_SHUTDOWN);
RPC_DECLARE(CORPC_TEST);

static int
handler_corpc_test(crt_rpc_t *rpc)
{
	DBG_PRINT("CORPC_HANDLER called\n");
	crt_reply_send(rpc);
	return 0;
}

static int
handler_set_err_code(crt_rpc_t *rpc)
{
	struct RPC_SET_ERR_CODE_in	*input;

	input = crt_req_get(rpc);
	g_ret_code = input->err_code;

	crt_reply_send(rpc);
	return 0;
}

static int
handler_shutdown(crt_rpc_t *rpc)
{
	DBG_PRINT("Shutdown handler called!\n");
	crt_reply_send(rpc);

	tc_progress_stop();
	return 0;
}
static int
corpc_aggregate(crt_rpc_t *src, crt_rpc_t *result, void *priv)
{
	struct CORPC_TEST_out	*output_src;
	struct CORPC_TEST_out	*output_result;


	output_src = crt_reply_get(src);
	output_result = crt_reply_get(result);

	output_result->field = output_src->field;
	return 0;
}

struct crt_corpc_ops corpc_test_ops = {
	.co_aggregate = corpc_aggregate,
	.co_pre_forward = NULL,
};

struct crt_proto_rpc_format my_proto_rpc_fmt[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_RPC_SET_ERR_CODE,
		.prf_hdlr	= (void *)handler_set_err_code,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_CORPC_TEST,
		.prf_hdlr	= (void *)handler_corpc_test,
		.prf_co_ops	= &corpc_test_ops,
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


struct corpc_wait_info {
	sem_t	sem;
	int	rc;
};

static void
rpc_handle_reply(const struct crt_cb_info *info)
{
	struct corpc_wait_info *wait_info;

	wait_info = (struct corpc_wait_info *)info->cci_arg;

	wait_info->rc = info->cci_rc;
	sem_post(&wait_info->sem);
}

static void
verify_corpc(crt_context_t ctx, crt_group_t *grp, int exp_rc)
{
	struct corpc_wait_info	wait_info;
	crt_rpc_t		*rpc;
	int			rc;

	DBG_PRINT(">>> Sending test to %s, expected_rc=%d\n",
		grp->cg_grpid, exp_rc);

	rc = sem_init(&wait_info.sem, 0, 0);
	if (rc != 0) {
		D_ERROR("sem_init() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_corpc_req_create(ctx, grp, NULL, CORPC_TEST,
			NULL, 0, 0, crt_tree_topo(CRT_TREE_KNOMIAL, 2),
			&rpc);
	if (rc != 0) {
		D_ERROR("crt_copc_req_create() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_req_send(rpc, rpc_handle_reply, &wait_info);
	if (rc != 0) {
		D_ERROR("crt_req_send() failed; rc=%d\n", rc);
		assert(0);
	}

	tc_sem_timedwait(&wait_info.sem, 10, __LINE__);

	if (wait_info.rc != exp_rc) {
		D_ERROR("Expected %d got %d\n", exp_rc, wait_info.rc);
		assert(0);
	}
	DBG_PRINT("<<< Test finished successfully\n");
}

static int
rpc_callback(crt_context_t *ctx, void *hdlr_arg,
	void (*rpc_hdlr)(void *), void *arg)
{
	crt_rpc_t *rpc = hdlr_arg;

	if (rpc->cr_opc != CORPC_TEST) {
		rpc_hdlr(rpc);
		return 0;
	}

	if (g_ret_code == 0) {
		rpc_hdlr(rpc);
		return 0;
	}

	return g_ret_code;
}

static void
set_error(crt_context_t ctx, crt_group_t *grp,
		int rank, int error_code)
{
	struct RPC_SET_ERR_CODE_in	*input;
	crt_rpc_t			*rpc;
	crt_endpoint_t			server_ep;
	struct corpc_wait_info		wait_info;
	int				rc;

	rc = sem_init(&wait_info.sem, 0, 0);
	if (rc != 0) {
		D_ERROR("sem_init() failed; rc=%d\n", rc);
		assert(0);
	}

	server_ep.ep_grp = grp;
	server_ep.ep_rank = rank;
	server_ep.ep_tag = 0;

	rc = crt_req_create(ctx, &server_ep, RPC_SET_ERR_CODE, &rpc);
	if (rc != 0) {
		D_ERROR("SET_ERR_CODE rpc failed; rc=%d\n", rc);
		assert(0);
	}

	input = crt_req_get(rpc);
	input->err_code = error_code;

	rc = crt_req_send(rpc, rpc_handle_reply, &wait_info);
	if (rc != 0) {
		D_ERROR("crt_req_send() failed; rc=%d\n", rc);
		assert(0);
	}

	tc_sem_timedwait(&wait_info.sem, 10, __LINE__);
}


int main(int argc, char **argv)
{
	crt_group_t		*grp;
	crt_context_t		crt_ctx[NUM_SERVER_CTX];
	pthread_t		progress_thread[NUM_SERVER_CTX];
	int			i, k;
	char			*my_uri;
	char			*env_self_rank;
	d_rank_t		my_rank;
	char			*grp_cfg_file;
	uint32_t		grp_size;
	crt_group_t		*sec_grp1;
	d_rank_list_t		*rank_list;
	d_rank_list_t		*p_list, *s_list;
	crt_endpoint_t		server_ep;
	d_rank_t		rank;
	crt_rpc_t		*rpc;
	sem_t			sem;
	int			rc;

	env_self_rank = getenv("CRT_L_RANK");
	my_rank = atoi(env_self_rank);

	/* rank, num_attach_retries, is_server, assert_on_error */
	tc_test_init(my_rank, 20, true, true);

	rc = d_log_init();
	assert(rc == 0);

	DBG_PRINT("Server starting up\n");
	rc = crt_init(NULL, CRT_FLAG_BIT_SERVER |
			CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
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
				tc_progress_fn, &crt_ctx[i]);
		assert(rc == 0);

		rc = crt_context_register_rpc_task(crt_ctx[i],
				rpc_callback, NULL, NULL);
		if (rc != 0) {
			D_ERROR("register_rpc_task failed; rc=%d\n", rc);
			assert(0);
		}
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
	}

	if (my_rank != 0)
		D_GOTO(join, 0);

	/* Wait for all servers to load up */
	/* TODO: This will be replaced by proper sync when CART-715 is done */
	sleep(2);

	rc = crt_group_ranks_get(grp, &rank_list);
	if (rc != 0) {
		D_ERROR("crt_group_ranks_get() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = tc_wait_for_ranks(crt_ctx[0], grp, rank_list, 0,
			NUM_SERVER_CTX, 10, 100.0);
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

	rc = sem_init(&sem, 0, 0);
	if (rc != 0) {
		D_ERROR("sem_init() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_group_ranks_get(grp, &p_list);
	if (rc != 0) {
		D_ERROR("crt_group_ranks_get() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_group_ranks_get(sec_grp1, &s_list);
	if (rc != 0) {
		D_ERROR("crt_group_ranks_get() failed; rc=%d\n", rc);
		assert(0);
	}

	for (k = 1; k < p_list->rl_nr; k++) {
		DBG_PRINT("TEST_%d: Setting node %d to return error\n", k,
			p_list->rl_ranks[k]);
		for (i = 0; i < p_list->rl_nr; i++)
			set_error(crt_ctx[1], grp, p_list->rl_ranks[i],
				DER_SUCCESS);

		set_error(crt_ctx[1], grp, p_list->rl_ranks[k], -2022);
		verify_corpc(crt_ctx[1], grp, -2022);
	}

	/* Send shutdown RPC to all nodes except for self */
	DBG_PRINT("Senidng shutdown to all nodes\n");

	/* Note rank at i=0 corresponds to 'self' */
	for (i = 0; i < s_list->rl_nr; i++) {
		rank = s_list->rl_ranks[i];

		if (i == 0)
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
		tc_sem_timedwait(&sem, 10, __LINE__);
	}

	d_rank_list_free(s_list);
	d_rank_list_free(p_list);

	tc_progress_stop();
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

