/*
 * (C) Copyright 2026 Gluesys Inc.
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * Test that an RPC can safely outlive the detach of its group.
 *
 * The collective root path in crt_corpc_req_create() stores the group
 * in the RPC without taking a reference, and crt_rpc_priv_free()
 * releases none, so the RPC relies on the caller's attach reference.
 * This test keeps a collective RPC in flight, drops the last external
 * reference on its group (secondary group destroy), and only then lets
 * the RPC complete and be freed. Without the fix this is a
 * use-after-free on crp_grp_priv.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <semaphore.h>
#include <pthread.h>

#include "crt_utils.h"
#include <cart/api.h>
#include <gurt/common.h>

#define MY_BASE               0x010000000
#define MY_VER                0

#define NUM_SERVER_CTX        2

#define CRT_ISEQ_RPC_SHUTDOWN ((uint64_t)(field)CRT_VAR)
#define CRT_OSEQ_RPC_SHUTDOWN /* output fields */ ((uint64_t)(field)CRT_VAR)
#define CRT_ISEQ_RPC_GRP_TEST /* input fields */ ((uint64_t)(field)CRT_VAR)
#define CRT_OSEQ_RPC_GRP_TEST /* output fields */ ((uint64_t)(field)CRT_VAR)

enum {
	RPC_GRP_TEST = CRT_PROTO_OPC(MY_BASE, MY_VER, 0),
	RPC_SHUTDOWN,
} rpc_id_t;

#define RPC_DECLARE(name)                                                                          \
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)                                    \
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)

RPC_DECLARE(RPC_SHUTDOWN);
RPC_DECLARE(RPC_GRP_TEST);

static int
handler_grp_test(crt_rpc_t *rpc)
{
	/* Delay the reply so the client can destroy the secondary group
	 * while this RPC is still in flight.
	 */
	sleep(2);
	crt_reply_send(rpc);
	return 0;
}

static int
handler_shutdown(crt_rpc_t *rpc)
{
	crt_reply_send(rpc);
	crtu_progress_stop();
	return 0;
}

struct crt_proto_rpc_format my_proto_rpc_fmt[] = {{
						      .prf_flags   = 0,
						      .prf_req_fmt = &CQF_RPC_GRP_TEST,
						      .prf_hdlr    = (void *)handler_grp_test,
						      .prf_co_ops  = NULL,
						  },
						  {
						      .prf_flags   = 0,
						      .prf_req_fmt = &CQF_RPC_SHUTDOWN,
						      .prf_hdlr    = (void *)handler_shutdown,
						      .prf_co_ops  = NULL,
						  }};

struct crt_proto_format     my_proto_fmt = {
	.cpf_name  = "grp-lifetime-proto",
	.cpf_ver   = MY_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt),
	.cpf_prf   = &my_proto_rpc_fmt[0],
	.cpf_base  = MY_BASE,
};

static sem_t g_reply_sem;

static void
grp_test_reply(const struct crt_cb_info *info)
{
	sem_post(&g_reply_sem);
}

int
main(int argc, char **argv)
{
	crt_group_t   *grp;
	crt_group_t   *sec_grp;
	crt_context_t  crt_ctx[NUM_SERVER_CTX];
	pthread_t      progress_thread[NUM_SERVER_CTX];
	d_rank_t       my_rank;
	char          *env_self_rank;
	char          *grp_cfg_file;
	char          *my_uri;
	crt_endpoint_t server_ep;
	crt_rpc_t     *rpc;
	uint32_t       grp_size;
	int            i;
	int            rc;

	crtu_test_init(0, 20, true, true);
	if (D_ON_VALGRIND)
		crtu_set_shutdown_delay(10);
	else
		crtu_set_shutdown_delay(2);

	rc = d_log_init();
	assert(rc == 0);

	d_agetenv_str(&env_self_rank, "CRT_L_RANK");
	my_rank = atoi(env_self_rank);
	d_freeenv_str(&env_self_rank);

	DBG_PRINT("Server starting up, rank %d\n", my_rank);
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

	for (i = 0; i < NUM_SERVER_CTX; i++) {
		rc = crt_context_create(&crt_ctx[i]);
		if (rc != 0) {
			D_ERROR("crt_context_create() failed; rc=%d\n", rc);
			assert(0);
		}

		rc = pthread_create(&progress_thread[i], 0, crtu_progress_fn, &crt_ctx[i]);
		assert(rc == 0);
	}

	d_agetenv_str(&grp_cfg_file, "CRT_L_GRP_CFG");

	rc = crt_rank_self_set(my_rank, 1 /* group_version_min */);
	if (rc != 0) {
		D_ERROR("crt_rank_self_set(%d) failed; rc=%d\n", my_rank, rc);
		assert(0);
	}

	rc = crt_rank_uri_get(grp, my_rank, 0, &my_uri);
	if (rc != 0) {
		D_ERROR("crt_rank_uri_get() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crtu_load_group_from_file(grp_cfg_file, crt_ctx[0], grp, my_rank, true);
	if (rc != 0) {
		D_ERROR("crtu_load_group_from_file() failed; rc=%d\n", rc);
		assert(0);
	}

	DBG_PRINT("self_rank=%d uri=%s grp_cfg_file=%s\n", my_rank, my_uri, grp_cfg_file);
	d_freeenv_str(&grp_cfg_file);
	D_FREE(my_uri);

	rc = crt_group_size(NULL, &grp_size);
	if (rc != 0) {
		D_ERROR("crt_group_size() failed; rc=%d\n", rc);
		assert(0);
	}

	if (grp_size != 2) {
		D_ERROR("This test expects 2 instances; got=%d\n", grp_size);
		assert(0);
	}

	/* let rank 1 finish its contexts and load the group view */
	sleep(2);

	/*
	 * Rank 0 drives the scenario: a collective RPC on a secondary
	 * group, dropped before the reply arrives, with the secondary
	 * group destroyed while the RPC is still in flight.
	 */
	if (my_rank != 0)
		goto wait_threads;

	rc = crt_group_secondary_create("sec_grp_lifetime", grp, NULL, &sec_grp);
	if (rc != 0) {
		D_ERROR("crt_group_secondary_create() failed; rc=%d\n", rc);
		assert(0);
	}

	/* Map the primary ranks into the secondary group; the mapping is
	 * local to this rank, matching the corpc_errors pattern.
	 */
	rc = crt_group_secondary_rank_add(sec_grp, 1, 1);
	if (rc != 0) {
		D_ERROR("crt_group_secondary_rank_add() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_group_size(sec_grp, &grp_size);
	if (rc != 0) {
		D_ERROR("crt_group_size() failed; rc=%d\n", rc);
		assert(0);
	}

	if (grp_size != 1) {
		D_ERROR("Expected secondary group size=1 got=%d\n", grp_size);
		assert(0);
	}

	rc = sem_init(&g_reply_sem, 0, 0);
	if (rc != 0) {
		D_ERROR("sem_init() failed; rc=%d\n", rc);
		assert(0);
	}

	server_ep.ep_grp  = sec_grp;
	server_ep.ep_rank = 1;
	server_ep.ep_tag  = 0;

	rc = crt_req_create(crt_ctx[1], &server_ep, RPC_GRP_TEST, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed; rc=%d\n", rc);
		assert(0);
	}

	/*
	 * The RPC now references sec_grp. Destroying the secondary group
	 * drops the last external reference, so without the fix the group
	 * is freed here while the RPC still points at it; the upcoming
	 * send resolves the target through the group, and the completion
	 * and crt_rpc_priv_free() come after that. With the fix the RPC
	 * holds its own reference until it is freed.
	 */
	rc = crt_group_secondary_destroy(sec_grp);
	if (rc != 0) {
		D_ERROR("crt_group_secondary_destroy() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_req_send(rpc, grp_test_reply, NULL);
	if (rc != 0) {
		D_ERROR("crt_req_send() failed; rc=%d\n", rc);
		assert(0);
	}

	if (sem_trywait(&g_reply_sem) == 0) {
		DBG_PRINT("reply completed\n");
	} else {
		DBG_PRINT("no reply yet; continuing to shutdown\n");
	}
	sem_destroy(&g_reply_sem);

	/* shutdown rank 1 */
	server_ep.ep_grp  = grp;
	server_ep.ep_rank = 1;
	server_ep.ep_tag  = 0;

	rc = crt_req_create(crt_ctx[1], &server_ep, RPC_SHUTDOWN, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_req_send(rpc, grp_test_reply, NULL);
	if (rc != 0) {
		D_ERROR("crt_req_send() failed; rc=%d\n", rc);
		assert(0);
	}

	crtu_progress_stop();
	sem_destroy(&g_reply_sem);

wait_threads:
	for (i = 0; i < NUM_SERVER_CTX; i++)
		pthread_join(progress_thread[i], NULL);

	rc = crt_finalize();
	if (rc != 0) {
		D_ERROR("crt_finalize() failed; rc=%d\n", rc);
		assert(0);
	}

	DBG_PRINT("Finalized\n");
	d_log_fini();

	return 0;
}
