/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <semaphore.h>

#include "tests_common.h"
#include "test_proto_common.h"

static void
rpc_cb_common(const struct crt_cb_info *cb_info)
{
	crt_rpc_t		*rpc_req;
	struct ping_in		*rpc_req_input;
	struct ping_out		*rpc_req_output;

	rpc_req = cb_info->cci_rpc;
	D_ASSERT(rpc_req != NULL);

	switch (rpc_req->cr_opc) {
	case CRT_PROTO_OPC(OPC_MY_PROTO, 1, 1):
		rpc_req_input = crt_req_get(rpc_req);
		rpc_req_output = crt_reply_get(rpc_req);
		DBG_PRINT("bounced back magic number %u\n",
			rpc_req_output->po_magic);
		D_ASSERT(rpc_req_output->po_magic
			 == rpc_req_input->pi_magic + 1);
		sem_post(&test.tg_token_to_proceed);
		break;
	case OPC_SHUTDOWN:
		tc_progress_stop();
		sem_post(&test.tg_token_to_proceed);
		break;
	default:
		sem_post(&test.tg_token_to_proceed);
		break;
	}
}

static void
query_cb(struct crt_proto_query_cb_info *cb_info)
{
	uint32_t	*high_ver;

	high_ver = cb_info->pq_arg;

	D_ASSERT(cb_info->pq_arg != NULL);
	D_ERROR("query_cb() failed, cb_info->pq_rc: %d.\n", cb_info->pq_rc);
	D_ASSERT(cb_info->pq_rc == DER_SUCCESS);

	*high_ver = cb_info->pq_ver;
}

static void
test_run()
{
	crt_group_t		*grp = NULL;
	d_rank_list_t		*rank_list = NULL;
	crt_rpc_t		*rpc_req = NULL;
	struct ping_in		*rpc_req_input;
	crt_endpoint_t		 server_ep = {0};
	crt_opcode_t		 my_opc;
	uint32_t		 my_ver_array[] = {0, 2, 5, 1, 4, 3, 7};
	uint32_t		 s_high_ver = 0xFFFFFFFF;
	uint32_t		 c_high_ver = test.tg_num_proto - 1;
	int			 rc;

	fprintf(stderr, "local group: %s remote group: %s\n",
		test.tg_local_group_name, test.tg_remote_group_name);

	if (test.tg_save_cfg) {
		rc = crt_group_config_path_set(test.tg_cfg_path);
		D_ASSERTF(rc == 0, "crt_group_config_path_set failed %d\n", rc);
	}

	tc_cli_start_basic(test.tg_local_group_name,
			   test.tg_remote_group_name,
			   &grp, &rank_list, &test.tg_crt_ctx,
			   &test.tg_tid, 1,
			   test.tg_use_cfg, NULL);

	rc = sem_init(&test.tg_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	rc = crt_group_rank(NULL, &test.tg_my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank() failed. rc: %d\n", rc);

	switch (test.tg_num_proto) {
	case 4:
		rc = crt_proto_register(&my_proto_fmt_3);
		D_ASSERT(rc == 0);
	case 3:
		rc = crt_proto_register(&my_proto_fmt_2);
		D_ASSERT(rc == 0);
	case 2:
		rc = crt_proto_register(&my_proto_fmt_1);
		D_ASSERT(rc == 0);
	case 1:
		rc = crt_proto_register(&my_proto_fmt_0);
		D_ASSERT(rc == 0);
	default:
		break;
	}

	/* Attempt to re-register duplicate proto */
	rc = crt_proto_register(&my_proto_fmt_0_duplicate);
	D_ASSERTF(rc == -DER_EXIST,
		"re-registration returned unexpected rc: %d\n", rc);

	rc = crt_group_size(grp, &test.tg_remote_group_size);
	D_ASSERTF(rc == 0, "crt_group_size() failed; rc=%d\n", rc);

	test.tg_remote_group = grp;

	server_ep.ep_grp = grp;
	server_ep.ep_rank = 0;

	DBG_PRINT("proto query\n");
	rc = crt_proto_query(&server_ep, OPC_MY_PROTO, my_ver_array, 7,
			     query_cb, &s_high_ver);
	D_ASSERT(rc == 0);

	while (s_high_ver == 0xFFFFFFFF)
		sched_yield();

	DBG_PRINT("s_high_ver %u.\n", s_high_ver);
	DBG_PRINT("c_high_ver %u.\n", c_high_ver);

	if (c_high_ver > s_high_ver)
		my_opc = CRT_PROTO_OPC(OPC_MY_PROTO, s_high_ver, s_high_ver);
	else
		my_opc = CRT_PROTO_OPC(OPC_MY_PROTO, c_high_ver, c_high_ver);

	rc = crt_req_create(test.tg_crt_ctx, &server_ep, my_opc, &rpc_req);

	D_ASSERTF(rc == 0 && rpc_req != NULL, "crt_req_create() failed,"
		  " rc: %d rpc_req: %p\n", rc, rpc_req);

	rpc_req_input = crt_req_get(rpc_req);
	D_ASSERTF(rpc_req_input != NULL, "crt_req_get() failed."
		  " rpc_req_input: %p\n", rpc_req_input);

	rpc_req_input->pi_magic = 2345;

	rc = crt_req_send(rpc_req, rpc_cb_common, NULL);
	D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);

	tc_sem_timedwait(&test.tg_token_to_proceed, 61, __LINE__);

	if (test.tg_my_rank == 0) {
		rc = crt_req_create(test.tg_crt_ctx, &server_ep,
				    OPC_SHUTDOWN, &rpc_req);
		D_ASSERTF(rc == 0 && rpc_req != NULL,
			  "crt_req_create() failed. "
			  "rc: %d, rpc_req: %p\n", rc, rpc_req);
		rc = crt_req_send(rpc_req, rpc_cb_common, NULL);
		D_ASSERTF(rc == 0,
			  "crt_req_send() failed. rc: %d\n", rc);
		tc_sem_timedwait(&test.tg_token_to_proceed, 61, __LINE__);
	}

	d_rank_list_free(rank_list);
	rank_list = NULL;

	if (test.tg_save_cfg) {
		rc = crt_group_detach(grp);
		D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	}

	tc_progress_stop();

	rc = pthread_join(test.tg_tid, NULL);
	D_ASSERTF(rc == 0, "pthread_join failed. rc: %d\n", rc);
	D_DEBUG(DB_TRACE, "joined progress thread.\n");

	rc = sem_destroy(&test.tg_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

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
	tc_test_init(0, 40, false, true);

	test_run();

	return rc;
}
