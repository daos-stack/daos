/*
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Client utilizing crt_launch generated environment for NO-PMIX mode
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
#include "no_pmix_launcher_common.h"

static int
g_do_shutdown;

static void *
progress_function(void *data)
{
	crt_context_t *p_ctx = (crt_context_t *)data;

	while (g_do_shutdown == 0)
		crt_progress(*p_ctx, 1000);

	crt_context_destroy(*p_ctx, 1);

	return NULL;
}

static void
rpc_handle_reply(const struct crt_cb_info *info)
{
	sem_t		*sem;
	uint64_t	rpcid;

	crt_req_rpcid_get(info->cci_rpc, &rpcid);

	DBG_PRINT("[RPCID: 0x%lx] Response handler called with %d\n\n", rpcid, info->cci_rc);


	sem = (sem_t *)info->cci_arg;
	sem_post(sem);
}

int main(int argc, char **argv)
{
	crt_context_t		crt_ctx;
	crt_group_t		*grp;
	char			*grp_cfg_file;
	int			rc;
	sem_t			sem;
	pthread_t		progress_thread;
	crt_rpc_t		*rpc = NULL;
	struct RPC_PING_in	*input;
	crt_endpoint_t		server_ep;
	int			i;
	uint32_t		grp_size;
	d_rank_list_t		*rank_list;
	d_rank_t		rank;
	d_iov_t			iov;
	crt_bulk_t		bulk_hdl;
	d_sg_list_t		sgl;
	int			tag;
	int			repeat;
	uint64_t		rpcid;

#define NUM_RPC_RETRIES 10

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(0, 20, false, true);

	rc = d_log_init();
	assert(rc == 0);

	DBG_PRINT("Client starting up\n");

	rc = sem_init(&sem, 0, 0);
	if (rc != 0) {
		D_ERROR("sem_init() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_init(NULL, 0);
	if (rc != 0) {
		D_ERROR("crt_init() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_proto_register(&my_proto_fmt);
	if (rc != 0) {
		D_ERROR("crt_proto_register() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_group_view_create("server_grp", &grp);
	if (!grp || rc != 0) {
		D_ERROR("Failed to create group view; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_context_create(&crt_ctx);
	if (rc != 0) {
		D_ERROR("crt_context_create() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = pthread_create(&progress_thread, 0, progress_function, &crt_ctx);
	assert(rc == 0);

	grp_cfg_file = getenv("CRT_L_GRP_CFG");
	DBG_PRINT("Client starting with cfg_file=%s\n", grp_cfg_file);

	//TODO: add control to change from false to true
	/* load group info from a config file and delete file upon return */
	rc = crtu_load_group_from_file(grp_cfg_file, crt_ctx, grp, -1, false);
	if (rc != 0) {
		D_ERROR("crtu_load_group_from_file() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_group_size(grp, &grp_size);
	if (rc != 0) {
		D_ERROR("crt_group_size() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_group_ranks_get(grp, &rank_list);
	if (rc != 0) {
		D_ERROR("crt_group_ranks_get() failed; rc=%d\n", rc);
		assert(0);
	}

	if (rank_list->rl_nr != grp_size) {
		D_ERROR("rank_list differs in size. expected %d got %d\n",
			grp_size, rank_list->rl_nr);
		assert(0);
	}

	rc = crt_group_psr_set(grp, rank_list->rl_ranks[0]);
	if (rc != 0) {
		D_ERROR("crt_group_psr_set() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crtu_wait_for_ranks(crt_ctx, grp, rank_list, NUM_SERVER_CTX - 1,
				 NUM_SERVER_CTX, 60, 120);
	if (rc != 0) {
		D_ERROR("wait_for_ranks() failed; rc=%d\n", rc);
		assert(0);
	}


	char *iov_buff[NUM_RPC_RETRIES];

	for (i = 0; i < NUM_RPC_RETRIES; i++) {
		D_ALLOC(iov_buff[i], TEST_IOV_SIZE_IN);
		memset(iov_buff[i], 'a' + i, TEST_IOV_SIZE_IN);
	}
	

	/* Cycle through all ranks and 8 tags and send rpc to each */
	for (i = 0; i < 1 /* rank_list->rl_nr*/ ; i++) {

		rank = rank_list->rl_ranks[i];

		for (tag = 0; tag < 1 /* NUM_SERVER_CTX */; tag++) {

			for (repeat = 0 ; repeat < NUM_RPC_RETRIES; repeat++) {
				D_ALLOC(iov.iov_buf, TEST_IOV_SIZE_IN);
				D_ASSERTF(iov.iov_buf != NULL, "Failed to allocate iov buf\n");

				memset(iov.iov_buf, 'a', TEST_IOV_SIZE_IN);

				tag = 0;
				//tag = repeat % 2;

				iov.iov_buf_len = TEST_IOV_SIZE_IN;
				iov.iov_len = TEST_IOV_SIZE_IN;

				server_ep.ep_rank = rank;
				server_ep.ep_tag = tag;
				server_ep.ep_grp = grp;

				rc = crt_req_create(crt_ctx, &server_ep, RPC_PING, &rpc);
				if (rc != 0) {
					D_ERROR("crt_req_create() failed; rc=%d\n", rc);
					assert(0);
				}

				input = crt_req_get(rpc);
				input->tag = tag;
				input->test_data = iov;

				if (repeat == 0)
					input->delay = 15;
				else
					input->delay = 0;

				rc = crt_req_rpcid_get(rpc, &rpcid);
				if (rc != 0) {
					D_ERROR("crt_req_rpcid_get() failed; rc=%d\n", rc);
					assert(0);
				}

				DBG_PRINT("[RPCID: 0x%lx] Sending ping to %d:%d, using buffer %d\n", rpcid, rank, tag, repeat);

				// new code
				rc = d_sgl_init(&sgl, 1);
				D_ASSERTF(rc == 0, "d_sgl_init() failed; rc: %d\n", rc);

				sgl.sg_iovs[0].iov_buf = iov_buff[repeat]; //iov.iov_buf;
				sgl.sg_iovs[0].iov_len = iov.iov_len;
				sgl.sg_iovs[0].iov_buf_len = iov.iov_buf_len;

				rc = crt_bulk_create(crt_ctx, &sgl, CRT_BULK_RW, &bulk_hdl);
				D_ASSERTF(rc == 0, "crt_bulk_create() failed; rc: %d\n", rc);

				input->bulk_hdl = bulk_hdl;

				rc = crt_req_set_timeout(rpc, 10);
				D_ASSERTF(rc == 0, "crt_req_set_timeout() failed; rc: %d\n", rc);


				rc = crt_req_send(rpc, rpc_handle_reply, &sem);
				D_ASSERTF(rc == 0, "crt_bulk_free() failed; rc: %d\n", rc);


				crtu_sem_timedwait(&sem, 20, __LINE__);

				memset(iov_buff[repeat], 0xc, TEST_IOV_SIZE_IN);
				D_FREE(iov_buff[repeat]);
			}
		}
	}

	D_FREE(iov.iov_buf);

	/* Send shutdown RPC to each server */
	for (i = 0; i < rank_list->rl_nr; i++) {

		rank = rank_list->rl_ranks[i];
		DBG_PRINT("Sending shutdown to rank=%d\n", rank);

		server_ep.ep_rank = rank;
		server_ep.ep_tag = 0;
		server_ep.ep_grp = grp;


		rc = crt_req_create(crt_ctx, &server_ep, RPC_SHUTDOWN, &rpc);
		if (rc != 0) {
			D_ERROR("crt_req_create() failed; rc=%d\n", rc);
			assert(0);
		}

		rc = crt_req_send(rpc, rpc_handle_reply, &sem);
		crtu_sem_timedwait(&sem, 20, __LINE__);
		DBG_PRINT("RPC response received from rank=%d\n", rank);
	}

	D_FREE(rank_list->rl_ranks);
	D_FREE(rank_list);

	rc = crt_group_view_destroy(grp);
	if (rc != 0) {
		D_ERROR("crt_group_view_destroy() failed; rc=%d\n", rc);
		assert(0);
	}

	g_do_shutdown = true;
	pthread_join(progress_thread, NULL);

	sem_destroy(&sem);

	rc = crt_finalize();
	if (rc != 0) {
		D_ERROR("crt_finalize() failed with rc=%d\n", rc);
		assert(0);
	}

	DBG_PRINT("Client successfully finished\n");
	d_log_fini();

	return 0;
}
