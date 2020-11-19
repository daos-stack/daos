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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <getopt.h>
#include <semaphore.h>

#include "tests_common.h"
#include "test_group_rpc.h"

#define TEST_CTX_MAX_NUM	72
#define NUM_ATTACH_RETRIES	10
#define TEST_NO_TIMEOUT_BASE    0x010000000
#define TEST_NO_TIMEOUT_VER     0

struct test_t {
	crt_group_t	*t_local_group;
	crt_group_t	*t_remote_group;
	char		*t_local_group_name;
	char		*t_remote_group_name;
	uint32_t	 t_remote_group_size;
	d_rank_t	 t_my_rank;
	int		 t_use_cfg;
	int		 t_save_cfg;
	char		*t_cfg_path;
	unsigned int	 t_srv_ctx_num;
	crt_context_t	 t_crt_ctx[TEST_CTX_MAX_NUM];
	int		 t_thread_id[TEST_CTX_MAX_NUM]; /* logical tid */
	pthread_t	 t_tid[TEST_CTX_MAX_NUM];
	sem_t		 t_token_to_proceed;
	int		 t_roomno;
};

struct test_t test_g = {
	.t_srv_ctx_num = 1,
	.t_roomno = 1082
};

void
client_cb_common(const struct crt_cb_info *cb_info)
{
	crt_rpc_t			*rpc_req;
	struct crt_test_ping_delay_in	*rpc_req_input;
	struct crt_test_ping_delay_out	*rpc_req_output;

	rpc_req = cb_info->cci_rpc;

	if (cb_info->cci_arg != NULL)
		*(int *) cb_info->cci_arg = 1;

	switch (cb_info->cci_rpc->cr_opc) {
	case TEST_OPC_PING_DELAY:
		rpc_req_input = crt_req_get(rpc_req);
		if (rpc_req_input == NULL)
			return;
		rpc_req_output = crt_reply_get(rpc_req);
		if (rpc_req_output == NULL)
			return;
		if (cb_info->cci_rc != 0) {
			D_ERROR("rpc (opc: %#x) failed, rc: %d.\n",
				rpc_req->cr_opc, cb_info->cci_rc);
			D_FREE(rpc_req_input->name);
			break;
		}
		printf("%s ping result - ret: %d, room_no: %d.\n",
		       rpc_req_input->name, rpc_req_output->ret,
		       rpc_req_output->room_no);
		D_FREE(rpc_req_input->name);
		sem_post(&test_g.t_token_to_proceed);
		break;
	case TEST_OPC_SHUTDOWN:
		g_shutdown = 1;
		sem_post(&test_g.t_token_to_proceed);
		break;
	default:
		break;
	}
}

void test_shutdown_handler(crt_rpc_t *rpc_req)
{
	DBG_PRINT("tier1 test_srver received shutdown request, opc: %#x.\n",
		  rpc_req->cr_opc);

	D_ASSERTF(rpc_req->cr_input == NULL, "RPC request has invalid input\n");
	D_ASSERTF(rpc_req->cr_output == NULL, "RPC request output is NULL\n");

	g_shutdown = 1;
	DBG_PRINT("tier1 test_srver set shutdown flag.\n");
}

static struct crt_proto_rpc_format my_proto_rpc_fmt_test_no_timeout[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= NULL,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= CRT_RPC_FEAT_NO_REPLY,
		.prf_req_fmt	= NULL,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= CRT_RPC_FEAT_NO_TIMEOUT,
		.prf_req_fmt	= &CQF_crt_test_ping_delay,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}
};

static struct crt_proto_format my_proto_fmt_test_no_timeout = {
	.cpf_name = "my-proto-test-no_timeout",
	.cpf_ver = TEST_NO_TIMEOUT_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_test_no_timeout),
	.cpf_prf = &my_proto_rpc_fmt_test_no_timeout[0],
	.cpf_base = TEST_NO_TIMEOUT_BASE,
};

static void
ping_delay_reply(crt_group_t *remote_group, int rank, int tag, uint32_t delay)
{
	crt_rpc_t			*rpc_req = NULL;
	struct crt_test_ping_delay_in	*rpc_req_input;
	crt_endpoint_t			 server_ep = {0};
	char				*buffer;
	int				 rc;

	server_ep.ep_grp = remote_group;
	server_ep.ep_rank = rank;
	server_ep.ep_tag = tag;
	rc = crt_req_create(test_g.t_crt_ctx[0], &server_ep,
			    TEST_OPC_PING_DELAY, &rpc_req);
	D_ASSERTF(rc == 0 && rpc_req != NULL, "crt_req_create() failed,"
		  " rc: %d rpc_req: %p\n", rc, rpc_req);

	rpc_req_input = crt_req_get(rpc_req);
	D_ASSERTF(rpc_req_input != NULL, "crt_req_get() failed."
		  " rpc_req_input: %p\n", rpc_req_input);
	D_ALLOC(buffer, 256);
	D_ASSERTF(buffer != NULL, "Cannot allocate memory.\n");
	snprintf(buffer,  256, "Guest %d", test_g.t_my_rank);
	rpc_req_input->name = buffer;
	rpc_req_input->age = 21;
	rpc_req_input->days = 7;
	rpc_req_input->delay = delay;
	D_DEBUG(DB_TEST, "client(rank %d) sending ping rpc with tag "
		"%d, name: %s, age: %d, days: %d, delay: %u.\n",
		test_g.t_my_rank, server_ep.ep_tag, rpc_req_input->name,
		rpc_req_input->age, rpc_req_input->days, rpc_req_input->delay);

	/* send an rpc, print out reply */
	rc = crt_req_send(rpc_req, client_cb_common, NULL);
	D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);
}

void
test_run(void)
{
	crt_group_t		*grp = NULL;
	d_rank_list_t		*rank_list = NULL;
	d_rank_t		 rank;
	int			 tag;
	crt_endpoint_t		 server_ep = {0};
	crt_rpc_t		*rpc_req = NULL;
	uint32_t		 delay = 22;
	int			 i;
	int			 rc = 0;

	fprintf(stderr, "local group: %s remote group: %s\n",
		test_g.t_local_group_name, test_g.t_remote_group_name);

	if (test_g.t_save_cfg) {
		rc = crt_group_config_path_set(test_g.t_cfg_path);
		D_ASSERTF(rc == 0, "crt_group_config_path_set failed %d\n", rc);
	}

	tc_cli_start_basic(test_g.t_local_group_name,
			   test_g.t_remote_group_name,
			   &grp, &rank_list, &test_g.t_crt_ctx[0],
			   &test_g.t_tid[0], test_g.t_srv_ctx_num,
			   test_g.t_use_cfg, NULL);

	rc = sem_init(&test_g.t_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	rc = crt_group_rank(NULL, &test_g.t_my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank() failed. rc: %d\n", rc);

	/* register RPCs */
	rc = crt_proto_register(&my_proto_fmt_test_no_timeout);
	D_ASSERTF(rc == 0, "crt_proto_register() failed. rc: %d\n", rc);

	rc = tc_wait_for_ranks(test_g.t_crt_ctx[0], grp, rank_list,
				test_g.t_srv_ctx_num - 1, test_g.t_srv_ctx_num,
				5, 150);
	D_ASSERTF(rc == 0, "wait_for_ranks() failed; rc=%d\n", rc);

	crt_group_size(test_g.t_remote_group, &test_g.t_remote_group_size);
	fprintf(stderr, "size of %s is %d\n", test_g.t_remote_group_name,
		test_g.t_remote_group_size);

	for (i = 0; i < rank_list->rl_nr; i++) {
		rank = rank_list->rl_ranks[i];

		for (tag = 0; tag < test_g.t_srv_ctx_num; tag++) {
			DBG_PRINT("Sending rpc to %d:%d\n", rank, tag);
			ping_delay_reply(grp, rank, tag, delay);
		}
	}

	for (i = 0; i < rank_list->rl_nr; i++) {
		tc_sem_timedwait(&test_g.t_token_to_proceed, 61,
				 __LINE__);
	}

	if (test_g.t_my_rank == 0) {
		/* client rank 0 tells all servers to shut down */
		for (i = 0; i < rank_list->rl_nr; i++) {
			rank = rank_list->rl_ranks[i];

			server_ep.ep_grp = grp;
			server_ep.ep_rank = rank;

			rc = crt_req_create(test_g.t_crt_ctx[0], &server_ep,
					    TEST_OPC_SHUTDOWN, &rpc_req);
			D_ASSERTF(rc == 0 && rpc_req != NULL,
				  "crt_req_create() failed. "
				  "rc: %d, rpc_req: %p\n", rc, rpc_req);
			rc = crt_req_send(rpc_req, client_cb_common, NULL);
			D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n",
				  rc);

			tc_sem_timedwait(&test_g.t_token_to_proceed, 61,
					   __LINE__);
		}
	}

	d_rank_list_free(rank_list);
	rank_list = NULL;

	if (test_g.t_save_cfg) {
		rc = crt_group_detach(grp);
		D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	} else {
		rc = crt_group_view_destroy(grp);
		D_ASSERTF(rc == 0,
			  "crt_group_view_destroy() failed; rc=%d\n", rc);
	}

	g_shutdown = 1;

	rc = pthread_join(test_g.t_tid[0], NULL);
	if (rc != 0)
		fprintf(stderr, "pthread_join failed. rc: %d\n", rc);
	D_DEBUG(DB_TEST, "joined progress thread.\n");

	rc = sem_destroy(&test_g.t_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	d_log_fini();
	D_DEBUG(DB_TEST, "exiting.\n");
}

int
test_parse_args(int argc, char **argv)
{
	int				option_index = 0;
	int				rc = 0;
	struct option			long_options[] = {
		{"name", required_argument, 0, 'n'},
		{"attach_to", required_argument, 0, 'a'},
		{"srv_ctx_num", required_argument, 0, 'c'},
		{"cfg_path", required_argument, 0, 's'},
		{"use_cfg", required_argument, 0, 'u'},
		{0, 0, 0, 0}
	};

  test_g.t_use_cfg = true;

	while (1) {
		rc = getopt_long(argc, argv, "n:a:c:u:h:", long_options,
				 &option_index);
		if (rc == -1)
			break;
		switch (rc) {
		case 0:
			if (long_options[option_index].flag != 0)
				break;
		case 'n':
			test_g.t_local_group_name = optarg;
			break;
		case 'a':
			test_g.t_remote_group_name = optarg;
			break;
		case 'c': {
			unsigned int	nr;
			char		*end;

			nr = strtoul(optarg, &end, 10);
			if (end == optarg || nr == 0 || nr > TEST_CTX_MAX_NUM) {
				fprintf(stderr, "invalid ctx_num %d exceed "
					"[%d, %d], using 1 for test.\n", nr,
					1, TEST_CTX_MAX_NUM);
			} else {
				test_g.t_srv_ctx_num = nr;
				fprintf(stderr, "will create %d contexts.\n",
					nr);
			}
			break;
		}
		case 's':
			test_g.t_save_cfg = 1;
			test_g.t_cfg_path = optarg;
			break;
		case 'u':
			test_g.t_use_cfg = atoi(optarg);
			break;
		case '?':
			return 1;
		default:
			return 1;
		}
	}
	if (optind < argc) {
		fprintf(stderr, "non-option argv elements encountered");
		return 1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int	rc;

	rc = test_parse_args(argc, argv);
	if (rc != 0) {
		fprintf(stderr, "test_parse_args() failed, rc: %d.\n",
			rc);
		return rc;
	}

	/* rank, num_attach_retries, is_server, assert_on_error */
	tc_test_init(0, 40, false, true);

	test_run();

	return rc;
}
