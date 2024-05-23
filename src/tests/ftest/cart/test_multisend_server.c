/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <semaphore.h>

#include "crt_utils.h"
#include "test_multisend_common.h"


static char *dst;
static bool g_alloc_dma_once = true;

static void
error_exit(void)
{
	assert(0);
}

static int
bulk_transfer_done_cb(const struct crt_bulk_cb_info *info)
{
	void	*buff;
	int	rc;

	if (info->bci_rc != 0) {
		D_ERROR("Bulk transfer failed with rc=%d\n", info->bci_rc);
		error_exit();
	}

	rc = crt_reply_send(info->bci_bulk_desc->bd_rpc);
	if (rc != 0) {
		D_ERROR("Failed to send response\n");
		error_exit();
	}

	crt_bulk_free(info->bci_bulk_desc->bd_local_hdl);

	buff = info->bci_arg;

	if (g_alloc_dma_once == false)
		D_FREE(buff);

	RPC_PUB_DECREF(info->bci_bulk_desc->bd_rpc);

	return 0;
}


static int
handler_ping(crt_rpc_t *rpc)
{
	struct RPC_PING_in	*input;
	struct RPC_PING_out	*output;
	int			rc = 0;

	input = crt_req_get(rpc);
	output = crt_reply_get(rpc);

	if (input->chunk_size != 0) {
		struct crt_bulk_desc	bulk_desc;
		crt_bulk_t		dst_bulk;
		d_sg_list_t		sgl;
		int			chunk_size;

		chunk_size = input->chunk_size;

		if (g_alloc_dma_once == false) {
			D_ALLOC_ARRAY(dst, chunk_size);
		} else {
			if (!dst)
				D_ALLOC_ARRAY(dst, chunk_size);
		}

		rc = d_sgl_init(&sgl, 1);
		if (rc != 0)
			error_exit();

		sgl.sg_iovs[0].iov_buf = dst;
		sgl.sg_iovs[0].iov_buf_len = chunk_size;
		sgl.sg_iovs[0].iov_len = chunk_size;

		rc = crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RW, &dst_bulk);
		if (rc != 0)
			error_exit();

		RPC_PUB_ADDREF(rpc);
		bulk_desc.bd_rpc = rpc;
		bulk_desc.bd_bulk_op = input->do_put ? CRT_BULK_PUT : CRT_BULK_GET;
		bulk_desc.bd_remote_hdl = input->bulk_hdl;
		bulk_desc.bd_remote_off = 0;
		bulk_desc.bd_local_hdl = dst_bulk;
		bulk_desc.bd_local_off = 0;
		bulk_desc.bd_len = chunk_size;
		rc = crt_bulk_transfer(&bulk_desc, bulk_transfer_done_cb,
				       dst, NULL);
		if (rc != 0) {
			D_ERROR("transfer failed; rc=%d\n", rc);
			error_exit();
		}
	} else {
		output->rc = rc;
		rc = crt_reply_send(rpc);
		if (rc != 0) {
			D_ERROR("reply failed; rc=%d\n", rc);
			error_exit();
		}
	}

	return 0;
}

static void
test_run(d_rank_t my_rank)
{
	crt_group_t		*grp = NULL;
	uint32_t		 grp_size;
	int			 rc;

	rc = crtu_srv_start_basic(test.tg_local_group_name, &test.tg_crt_ctx[0],
				  &test.tg_tid[0], &grp, &grp_size, NULL);
	D_ASSERTF(rc == 0, "crtu_srv_start_basic() failed\n");

	rc = sem_init(&test.tg_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	if (my_rank == 0) {
		DBG_PRINT("Saving group (%s) config file\n", test.tg_local_group_name);
		rc = crt_group_config_save(grp, true);
		D_ASSERTF(rc == 0,
			  "crt_group_config_save() failed. rc: %d\n", rc);
	}
	rc = crt_proto_register(&my_proto_fmt);
	D_ASSERT(rc == 0);

	rc = pthread_join(test.tg_tid[0], NULL);
	D_ASSERTF(rc == 0, "pthread_join failed. rc: %d\n", rc);
	D_DEBUG(DB_TRACE, "joined progress thread.\n");

	rc = sem_destroy(&test.tg_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	if (my_rank == 0) {
		rc = crt_group_config_remove(NULL);
		D_ASSERTF(rc == 0,
			  "crt_group_config_remove() failed. rc: %d\n", rc);
	}

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	d_log_fini();
	D_DEBUG(DB_TRACE, "exiting.\n");
}

int
main(int argc, char **argv)
{
	char		*env_self_rank;
	d_rank_t	 my_rank;
	int		 rc;

	rc = test_parse_args(argc, argv);
	if (rc != 0) {
		fprintf(stderr, "test_parse_args() failed, rc: %d.\n", rc);
		return rc;
	}

	d_agetenv_str(&env_self_rank, "CRT_L_RANK");
	my_rank = atoi(env_self_rank);
	d_freeenv_str(&env_self_rank);

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(my_rank, 40, true, true);

	DBG_PRINT("Starting server rank %d\n", my_rank);
	test_run(my_rank);

	return rc;
}
