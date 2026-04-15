/*
 * (C) Copyright 2018-2022 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <semaphore.h>
#include "crt_utils.h"

static bool corpc_hdlr_called = false;

static int
corpc_aggregate(crt_rpc_t *src, crt_rpc_t *result, void *priv)
{
	return 0;
}

struct crt_corpc_ops corpc_set_ivns_ops = {
	.co_aggregate = corpc_aggregate,
};

static void
corpc_hdlr(crt_rpc_t *rpc)
{
	int rc;

	DBG_PRINT("corpc handler called\n");
	corpc_hdlr_called = true;

	rc = crt_reply_send(rpc);
	D_ASSERT(rc == 0);
}

static void
shutdown_hdlr(crt_rpc_t *rpc)
{
	int rc;

	DBG_PRINT("shutdown handler called\n");

	rc = crt_reply_send(rpc);
	D_ASSERT(rc == 0);

	crtu_progress_stop();
}

#define TEST_OPC_BASE           0x010000000

#define CRT_ISEQ_BASIC_CORPC	/* input fields */		 \
	((uint32_t)		(unused)		CRT_VAR)

#define CRT_OSEQ_BASIC_CORPC	/* output fields */		 \
	((uint32_t)		(unused)		CRT_VAR)

CRT_RPC_DECLARE(basic_corpc, CRT_ISEQ_BASIC_CORPC, CRT_OSEQ_BASIC_CORPC)
CRT_RPC_DEFINE(basic_corpc, CRT_ISEQ_BASIC_CORPC, CRT_OSEQ_BASIC_CORPC)

#define TEST_OPC_CORPC_PING CRT_PROTO_OPC(TEST_OPC_BASE, 0, 0)
#define TEST_OPC_SHUTDOWN   CRT_PROTO_OPC(TEST_OPC_BASE, 0, 1)

/* Handle CORPC response */
static void
corpc_response_hdlr(const struct crt_cb_info *info)
{
	sem_t *sem;

	D_ASSERTF(info != NULL, "cb_info is null\n");
	D_ASSERTF(info->cci_rc == 0, "CORPC completed with an error\n");

	sem = (sem_t *)info->cci_arg;
	sem_post(sem);
}

/* Handle shutdown response*/
static void
shutdown_resp_hdlr(const struct crt_cb_info *info)
{
	sem_t *sem;

	D_ASSERTF(info != NULL, "cb_info is null\n");
	D_ASSERTF(info->cci_rc == 0, "Shutdown RPC completed with an error\n");

	DBG_PRINT("Shutdown response handler called\n");

	sem = (sem_t *)info->cci_arg;
	sem_post(sem);
}

static struct crt_proto_rpc_format proto_rpc_fmt[] = {{
							  .prf_flags   = 0,
							  .prf_req_fmt = &CQF_basic_corpc,
							  .prf_hdlr    = corpc_hdlr,
							  .prf_co_ops  = &corpc_set_ivns_ops,
						      },
						      {
							  .prf_flags   = 0,
							  .prf_req_fmt = NULL,
							  .prf_hdlr    = shutdown_hdlr,
							  .prf_co_ops  = NULL,
						      }};

static struct crt_proto_format     my_proto = {
	.cpf_name  = "my-proto-basic_corpc",
	.cpf_ver   = 0,
	.cpf_count = ARRAY_SIZE(proto_rpc_fmt),
	.cpf_prf   = &proto_rpc_fmt[0],
	.cpf_base  = TEST_OPC_BASE,
};

int main(void)
{
	int		rc;
	crt_context_t    ctx;
	d_rank_list_t	*rank_list;
	d_rank_list_t	membs;
	d_rank_t	memb_ranks[] = {1, 2, 4};
	crt_rpc_t	*rpc;
	uint32_t	grp_size;
	crt_group_t	*grp;
	char		*env_self_rank;
	char		*grp_cfg_file;
	pthread_t	progress_thread;
	sem_t            sem;
	crt_endpoint_t   server_ep;
	int              i;
	static d_rank_t  my_rank;

	membs.rl_nr = 3;
	membs.rl_ranks = memb_ranks;

	/* get self rank from the env that crt_launch prepares */
	d_agetenv_str(&env_self_rank, "CRT_L_RANK");
	my_rank = atoi(env_self_rank);
	d_freeenv_str(&env_self_rank);

	rc = sem_init(&sem, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	/* rank, num_attach_retries, is_server, D_ASSERT_on_error */
	crtu_test_init(my_rank, 20, true, true);
	crtu_set_shutdown_delay(0);

	rc = d_log_init();
	D_ASSERT(rc == 0);

	rc = crt_init(NULL, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	D_ASSERTF(rc == 0, "crt_init() failed\n");

	rc = crt_proto_register(&my_proto);
	D_ASSERTF(rc == 0, "crt_proto_register() failed\n");

	rc = crt_context_create(&ctx);
	D_ASSERTF(rc == 0, "crt_context_create() failed\n");

	rc = pthread_create(&progress_thread, 0, crtu_progress_fn, &ctx);
	D_ASSERTF(rc == 0, "pthread_create() failed; rc=%d\n", rc);

	d_agetenv_str(&grp_cfg_file, "CRT_L_GRP_CFG");

	rc = crt_rank_self_set(my_rank, 1 /* group_version_min */);
	D_ASSERTF(rc == 0, "crt_rank_self_set(%d) failed\n", my_rank);

	grp = crt_group_lookup(NULL);
	D_ASSERTF(grp != NULL, "Failed to lookup group\n");

	/* load group info from a config file and delete file upon return */
	rc = crtu_load_group_from_file(grp_cfg_file, ctx, grp, my_rank, true);
	d_freeenv_str(&grp_cfg_file);
	D_ASSERTF(rc == 0, "crtu_load_group_from_file() failed; rc=%d\n", rc);

	/* test requires 5 ranks */
	rc = crt_group_size(grp, &grp_size);
	D_ASSERTF(rc == 0, "crt_group_size() failed\n");
	D_ASSERTF(grp_size == 5, "This test requires 5 ranks\n");

	rc = crt_group_ranks_get(grp, &rank_list);
	D_ASSERTF(rc == 0, "crt_group_ranks_get() failed; rc=%d\n", rc);

	/* rank=0 is initiator of the test, the rest of ranks wait for rpcs */
	if (my_rank != 0)
		D_GOTO(wait_for_rpcs, 0);

	/* Wait for all ranks to come up, 5 seconds per ping, 100 seconds max */
	rc = crtu_wait_for_ranks(ctx, grp, rank_list, 0, 1, 5, 100.0);
	D_ASSERTF(rc == 0, "wait_for_ranks() failed; rc=%d\n", rc);

	d_rank_list_free(rank_list);
	rank_list = NULL;

	/* Send test CORPC with rank=3 excluded in membership list */
	DBG_PRINT("Rank 0 sending CORPC call\n");
	rc = crt_corpc_req_create(ctx, NULL, &membs, TEST_OPC_CORPC_PING, NULL, 0,
				  CRT_RPC_FLAG_FILTER_INVERT, crt_tree_topo(CRT_TREE_KNOMIAL, 4),
				  &rpc);
	D_ASSERT(rc == 0);

	rc = crt_req_send(rpc, corpc_response_hdlr, &sem);
	D_ASSERT(rc == 0);

	/* wait for corpc completion */
	crtu_sem_timedwait(&sem, 61, __LINE__);

	/* Send shutdown rpc to ranks 1 through 5 */
	server_ep.ep_grp = NULL;
	for (i = 1; i < grp_size; i++) {
		server_ep.ep_rank = i;
		server_ep.ep_tag  = 0;

		rc = crt_req_create(ctx, &server_ep, TEST_OPC_SHUTDOWN, &rpc);
		D_ASSERTF(rc == 0, "crt_req_create() TEST_OPC_SHUTDOWN failed\n");

		rc = crt_req_send(rpc, shutdown_resp_hdlr, &sem);
		crtu_sem_timedwait(&sem, 61, __LINE__);
	}

	/* rank0 issues a local progress stop */
	crtu_progress_stop();

wait_for_rpcs:
	/* Wait until progress thread exits */
	pthread_join(progress_thread, NULL);

	/* CORPC handler should have been called on ranks 1,2,4 and not any other */
	if (my_rank == 1 || my_rank == 2 || my_rank == 4)
		D_ASSERTF(corpc_hdlr_called == true, "corpc_handler was not called\n");
	else
		D_ASSERTF(corpc_hdlr_called == false, "corpc_handler was called\n");

	DBG_PRINT("All tests done\n");

	rc = sem_destroy(&sem);
	D_ASSERTF(rc == 0, "sem_destroy() failed\n");

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed\n");

	d_log_fini();
	return 0;
}
