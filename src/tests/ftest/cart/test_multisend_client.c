/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <semaphore.h>
#include <sys/types.h>
#include <sys/time.h>

#include "crt_utils.h"
#include "test_multisend_common.h"

static void
rpc_cb_common(const struct crt_cb_info *info)
{
	crt_bulk_t	*p_blk;
	int		rc;

	p_blk = (crt_bulk_t *)info->cci_arg;

	D_ASSERTF(info->cci_rc == 0, "rpc response failed. rc: %d\n", info->cci_rc);

	if (p_blk && *p_blk) {
		rc = crt_bulk_free(*p_blk);
		if (rc)
			D_ERROR("bulk free failed with %d\n", rc);
	}

	sem_post(&test.tg_token_to_proceed);
}

/* stub */
static int
handler_ping(crt_rpc_t *rpc)
{
	return 0;
}

static void
test_run()
{
	void			*dma_buff;
	int			chunk_size;
	int			num_chunks;
	int			num_iterations;
	int			chunk_index;
	unsigned long		time_delta;
	struct RPC_PING_in	*input;
	d_sg_list_t		sgl;
	crt_bulk_t		bulk_hdl[16];
	crt_context_t		ctx;
	crt_group_t		*grp = NULL;
	d_rank_list_t		*rank_list = NULL;
	crt_rpc_t		*rpc_req = NULL;
	crt_endpoint_t		 server_ep = {0};
	struct timeval		 tv_start;
	struct timeval		 tv_end;
	int			 i, ctx_idx;
	int			 rc;

	if (test.tg_save_cfg) {
		rc = crt_group_config_path_set(test.tg_cfg_path);
		D_ASSERTF(rc == 0, "crt_group_config_path_set failed %d\n", rc);
	}

	DBG_PRINT("Client starting with %d contexts\n", test.tg_num_ctx);

	if (test.tg_force_rank != -1)
		DBG_PRINT("Forcing simple RPC to the fixed target %d:0\n",
			  test.tg_force_rank);

	rc = crtu_cli_start_basic(test.tg_local_group_name,
				  test.tg_remote_group_name,
				  &grp, &rank_list, &test.tg_crt_ctx[0],
				  &test.tg_tid[0], 1,
				  test.tg_use_cfg, NULL,
				  test.tg_use_daos_agent_env);
	D_ASSERTF(rc == 0, "crtu_cli_start_basic()\n");

	if (test.tg_num_ctx < 1 || test.tg_num_ctx > MAX_NUM_CLIENT_CTX) {
		DBG_PRINT("Wrong number of ctx specified. Can't exceed %d\n",
			  MAX_NUM_CLIENT_CTX);
		return ;
	}

	for (i = 1; i < test.tg_num_ctx; i++) {
		rc = crt_context_create(&test.tg_crt_ctx[i]);
		D_ASSERTF(rc == 0, "crt_context_create() failed\n");

		rc = pthread_create(&test.tg_tid[i], 0, crtu_progress_fn,
				    &test.tg_crt_ctx[i]);
		D_ASSERTF(rc == 0, "pthread_create() failed\n");
	}

	rc = sem_init(&test.tg_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	rc = crt_group_rank(NULL, &test.tg_my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank() failed. rc: %d\n", rc);

	rc = crt_proto_register(&my_proto_fmt);
	D_ASSERTF(rc == 0, "crt_proto_register() failed. rc: %d\n", rc);

	rc = crt_group_size(grp, &test.tg_remote_group_size);
	D_ASSERTF(rc == 0, "crt_group_size() failed; rc: %d\n", rc);

	test.tg_remote_group = grp;

	/* SETUP dma buffer */
	chunk_size = test.tg_chunk_size_kb * 1024;
	num_chunks = test.tg_remote_group_size;

	num_iterations = test.tg_num_iterations;
	D_ALLOC_ARRAY(dma_buff, chunk_size * num_chunks);
	D_ASSERTF(dma_buff != NULL, "Failed to allocate dma buffer\n");

	ctx = test.tg_crt_ctx[0];

	gettimeofday(&tv_start, NULL);

	ctx_idx = 0;

	for (i = 0; i < num_iterations; i++) {

		/* SEND RPCs */
		for (chunk_index = 0; chunk_index < num_chunks; chunk_index++) {

			d_rank_t rank;

			ctx = test.tg_crt_ctx[ctx_idx];
			ctx_idx = (ctx_idx + 1) % test.tg_num_ctx;


			if (test.tg_force_rank == -1)
				rank = chunk_index % test.tg_remote_group_size;
			else
				rank = test.tg_force_rank;

			/* Pick a new server based on a test mode selected */
			server_ep.ep_grp = grp;
			server_ep.ep_rank = rank;
			server_ep.ep_tag = 0;

			rc = crt_req_create(ctx, &server_ep, RPC_PING, &rpc_req);
			D_ASSERTF(rc == 0 && rpc_req != NULL, "crt_req_create() failed,"
				  " rc: %d rpc_req: %p\n", rc, rpc_req);

			input = crt_req_get(rpc_req);

			/* TODO: for now rdma is disabled when forcing all rpcs to the same rank */
			if (test.tg_force_rank == -1) {
				rc = d_sgl_init(&sgl, 1);
				D_ASSERTF(rc == 0, "d_sgl_init() failed; rc: %d\n", rc);

				sgl.sg_iovs[0].iov_buf = dma_buff + (chunk_size * chunk_index);
				sgl.sg_iovs[0].iov_len = chunk_size;
				sgl.sg_iovs[0].iov_buf_len = chunk_size;

				rc = crt_bulk_create(ctx, &sgl, CRT_BULK_RW,
						     &bulk_hdl[chunk_index]);
				D_ASSERTF(rc == 0, "crt_bulk_create() failed; rc: %d\n", rc);

				input->bulk_hdl = bulk_hdl[chunk_index];
				input->chunk_size = chunk_size;
				input->chunk_index = chunk_index;
				input->do_put = test.tg_do_put;
			} else {
				input->chunk_size = 0;
				input->bulk_hdl = CRT_BULK_NULL;
				input->chunk_index = 0;
				input->do_put = false;
			}

			rc = crt_req_send(rpc_req, rpc_cb_common, &bulk_hdl[chunk_index]);
			D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);

			if (test.tg_test_mode == TEST_MODE_SYNC)
				crtu_sem_timedwait(&test.tg_token_to_proceed, 61, __LINE__);
		}

		if (test.tg_test_mode == TEST_MODE_ASYNC) {
			for (chunk_index = 0; chunk_index < num_chunks; chunk_index++) {
				crtu_sem_timedwait(&test.tg_token_to_proceed, 61, __LINE__);
			}
		}
	}

	gettimeofday(&tv_end, NULL);
	time_delta = (tv_end.tv_sec - tv_start.tv_sec) * 1000000 +
		     (tv_end.tv_usec - tv_start.tv_usec);

	if (test.tg_force_rank == -1 ) {
		DBG_PRINT("%s mode (%s) : Transfer of %d chunks size %dkb each took "
			  "%ld usec (%d repeats)\n",
			  test.tg_test_mode == TEST_MODE_SYNC ? "Synchronous" : "Asynchronous",
			  test.tg_do_put == true ? "PUT" : "GET",
			  num_chunks, test.tg_chunk_size_kb, time_delta/num_iterations,
			  num_iterations);
	} else {
		DBG_PRINT("%s mode, RPCs forced to target %d:0 ; delta %ld usec (%d repeats)\n",
			  test.tg_test_mode == TEST_MODE_SYNC ? "Synchronous" : "Asynchronous",
			  test.tg_force_rank, time_delta/num_iterations, num_iterations);
	}

	/* SHUTDOWN servers */
	if (test.tg_do_shutdown) {
		for (i = 0; i < test.tg_remote_group_size; i++) {

			/* Ranks are sequential from 0 */
			server_ep.ep_rank = i;

			rc = crt_req_create(test.tg_crt_ctx[0], &server_ep, RPC_SHUTDOWN, &rpc_req);
			D_ASSERTF(rc == 0 && rpc_req != NULL,
				  "crt_req_create() failed. rc: %d, rpc_req: %p\n", rc, rpc_req);

			rc = crt_req_send(rpc_req, rpc_cb_common, NULL);
			D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);

			crtu_sem_timedwait(&test.tg_token_to_proceed, 61, __LINE__);
		}
	}

	d_rank_list_free(rank_list);
	rank_list = NULL;

	if (test.tg_save_cfg) {
		rc = crt_group_detach(grp);
		D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	} else {
		rc = crt_group_view_destroy(grp);
		D_ASSERTF(rc == 0, "crt_group_view_destroy() failed; rc=%d\n", rc);
	}

	crtu_progress_stop();

	for (i = 0; i < test.tg_num_ctx; i++) {
		rc = pthread_join(test.tg_tid[i], NULL);
		D_ASSERTF(rc == 0, "pthread_join failed. rc: %d\n", rc);
	}
	D_DEBUG(DB_TRACE, "joined progress threads.\n");

	rc = sem_destroy(&test.tg_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	D_FREE(dma_buff);

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	d_log_fini();
	D_DEBUG(DB_TRACE, "exiting.\n");
}

int
main(int argc, char **argv)
{
	int	rc;

	rc = test_parse_args(argc, argv);
	if (rc != 0) {
		fprintf(stderr, "test_parse_args() failed, rc: %d.\n", rc);
		return rc;
	}

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(0, 40, false, true);

	test_run();

	return rc;
}
