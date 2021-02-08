/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This is a cart test_group common file, running with no pmix.
 */
#ifndef __TEST_GROUP_NP_COMMON_H__
#define __TEST_GROUP_NP_COMMON_H__

#define TEST_CTX_MAX_NUM	 (72)

#define TEST_GROUP_BASE          0x010000000
#define TEST_GROUP_VER           0


struct test_t {
	char			*t_local_group_name;
	char			*t_remote_group_name;
	int			 t_hold;
	int			 t_shut_only;
	bool			 t_save_cfg;
	bool			 t_use_cfg;
	char			*t_cfg_path;
	uint32_t		 t_hold_time;
	unsigned int		 t_srv_ctx_num;
	crt_context_t		 t_crt_ctx[TEST_CTX_MAX_NUM];
	pthread_t		 t_tid[TEST_CTX_MAX_NUM];
	sem_t			 t_token_to_proceed;
	int			 t_roomno;
	struct d_fault_attr_t	*t_fault_attr_1000;
	struct d_fault_attr_t	*t_fault_attr_5000;
};

struct test_t test_g = { .t_hold_time = 0,
			 .t_srv_ctx_num = 1,
			 .t_roomno = 1082 };

#define CRT_ISEQ_TEST_PING_CHECK /* input fields */		 \
	((uint32_t)		(age)			CRT_VAR) \
	((uint32_t)		(days)			CRT_VAR) \
	((d_string_t)		(name)			CRT_VAR) \
	((bool)			(bool_val)		CRT_VAR)

#define CRT_OSEQ_TEST_PING_CHECK /* output fields */		 \
	((int32_t)		(ret)			CRT_VAR) \
	((uint32_t)		(room_no)		CRT_VAR) \
	((uint32_t)		(bool_val)		CRT_VAR)

CRT_RPC_DECLARE(test_ping_check,
		CRT_ISEQ_TEST_PING_CHECK, CRT_OSEQ_TEST_PING_CHECK)
CRT_RPC_DEFINE(test_ping_check,
		CRT_ISEQ_TEST_PING_CHECK, CRT_OSEQ_TEST_PING_CHECK)

static void
test_checkin_handler(crt_rpc_t *rpc_req)
{
	struct test_ping_check_in	*e_req;
	struct test_ping_check_out	*e_reply;
	int				 rc = 0;

	/* CaRT internally already allocated the input/output buffer */
	e_req = crt_req_get(rpc_req);
	D_ASSERTF(e_req != NULL, "crt_req_get() failed. e_req: %p\n", e_req);

	DBG_PRINT("tier1 test_server recv'd checkin, opc: %#x.\n",
		   rpc_req->cr_opc);
	DBG_PRINT("tier1 checkin input - age: %d, name: %s, days: %d, "
		  "bool_val %d.\n", e_req->age, e_req->name, e_req->days,
		   e_req->bool_val);

	e_reply = crt_reply_get(rpc_req);
	D_ASSERTF(e_reply != NULL, "crt_reply_get() failed. e_reply: %p\n",
		  e_reply);
	e_reply->ret = 0;
	e_reply->room_no = test_g.t_roomno++;
	e_reply->bool_val = e_req->bool_val;
	if (D_SHOULD_FAIL(test_g.t_fault_attr_5000)) {
		e_reply->ret = -DER_MISC;
		e_reply->room_no = -1;
	} else {
		D_DEBUG(DB_ALL, "No fault injected.\n");
	}

	rc = crt_reply_send(rpc_req);
	D_ASSERTF(rc == 0, "crt_reply_send() failed. rc: %d\n", rc);

	DBG_PRINT("tier1 test_srver sent checkin reply, ret: %d, \
		   room_no: %d.\n", e_reply->ret, e_reply->room_no);
}

static void
test_ping_delay_handler(crt_rpc_t *rpc_req)
{
	struct crt_test_ping_delay_in	*p_req;
	struct crt_test_ping_delay_out	*p_reply;
	int				 rc = 0;

	/* CaRT internally already allocated the input/output buffer */
	p_req = crt_req_get(rpc_req);
	D_ASSERTF(p_req != NULL, "crt_req_get() failed. p_req: %p\n", p_req);

	DBG_PRINT("tier1 test_server recv'd checkin, opc: %#x.\n",
		  rpc_req->cr_opc);
	DBG_PRINT("tier1 checkin input - age: %d, name: %s, days: %d, "
		  "delay: %u.\n", p_req->age, p_req->name, p_req->days,
		   p_req->delay);

	p_reply = crt_reply_get(rpc_req);
	D_ASSERTF(p_reply != NULL, "crt_reply_get() failed. p_reply: %p\n",
		  p_reply);
	p_reply->ret = 0;
	p_reply->room_no = test_g.t_roomno++;

	sleep(p_req->delay);

	rc = crt_reply_send(rpc_req);
	D_ASSERTF(rc == 0, "crt_reply_send() failed. rc: %d\n", rc);

	DBG_PRINT("tier1 test_srver sent checkin reply, ret: %d, \
		   room_no: %d.\n", p_reply->ret, p_reply->room_no);
}

static void
client_cb_common(const struct crt_cb_info *cb_info)
{
	crt_rpc_t			*rpc_req;
	struct test_ping_check_in	*rpc_req_input;
	struct test_ping_check_out	*rpc_req_output;

	rpc_req = cb_info->cci_rpc;

	if (cb_info->cci_arg != NULL)
		*(int *) cb_info->cci_arg = 1;

	switch (cb_info->cci_rpc->cr_opc) {
	case TEST_OPC_CHECKIN:
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
		DBG_PRINT("%s checkin result - ret: %d, room_no: %d, "
		       "bool_val %d.\n",
		       rpc_req_input->name, rpc_req_output->ret,
		       rpc_req_output->room_no, rpc_req_output->bool_val);
		D_FREE(rpc_req_input->name);
		sem_post(&test_g.t_token_to_proceed);
		D_ASSERT(rpc_req_output->bool_val == true);
		break;
	case TEST_OPC_SHUTDOWN:
		tc_progress_stop();
		sem_post(&test_g.t_token_to_proceed);
		break;
	default:
		break;
	}
}

static void
test_shutdown_handler(crt_rpc_t *rpc_req)
{
	DBG_PRINT("tier1 test_srver received shutdown request, opc: %#x.\n",
		   rpc_req->cr_opc);

	D_ASSERTF(rpc_req->cr_input == NULL, "RPC request has invalid input\n");
	D_ASSERTF(rpc_req->cr_output == NULL, "RPC request output is NULL\n");

	tc_progress_stop();
	DBG_PRINT("tier1 test_srver set shutdown flag.\n");
}

static struct crt_proto_rpc_format my_proto_rpc_fmt_test_group1[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_test_ping_check,
		.prf_hdlr	= test_checkin_handler,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= CRT_RPC_FEAT_NO_REPLY,
		.prf_req_fmt	= NULL,
		.prf_hdlr	= test_shutdown_handler,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= CRT_RPC_FEAT_NO_TIMEOUT,
		.prf_req_fmt	= &CQF_crt_test_ping_delay,
		.prf_hdlr	= test_ping_delay_handler,
		.prf_co_ops	= NULL,
	}
};

struct crt_proto_format my_proto_fmt_test_group1 = {
	.cpf_name	= "my-proto-test-group1",
	.cpf_ver	= TEST_GROUP_VER,
	.cpf_count	= ARRAY_SIZE(my_proto_rpc_fmt_test_group1),
	.cpf_prf	= &my_proto_rpc_fmt_test_group1[0],
	.cpf_base	= TEST_GROUP_BASE,
};

static struct crt_proto_rpc_format my_proto_rpc_fmt_test_group2[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_test_ping_check,
		.prf_hdlr	= test_checkin_handler,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= CRT_RPC_FEAT_NO_REPLY,
		.prf_req_fmt	= NULL,
		.prf_hdlr	= test_shutdown_handler,
		.prf_co_ops	= NULL,
	}
};

struct crt_proto_format my_proto_fmt_test_group2 = {
	.cpf_name	= "my-proto-test-group2",
	.cpf_ver	= TEST_GROUP_VER,
	.cpf_count	= ARRAY_SIZE(my_proto_rpc_fmt_test_group2),
	.cpf_prf	= &my_proto_rpc_fmt_test_group2[0],
	.cpf_base	= TEST_GROUP_BASE,
};

void
check_in(crt_group_t *remote_group, int rank, int tag)
{
	crt_rpc_t			*rpc_req = NULL;
	struct test_ping_check_in	*rpc_req_input;
	crt_endpoint_t			 server_ep = {0};
	char				*buffer;
	int				 rc;

	server_ep.ep_grp = remote_group;
	server_ep.ep_rank = rank;
	server_ep.ep_tag = tag;
	rc = crt_req_create(test_g.t_crt_ctx[0], &server_ep,
			    TEST_OPC_CHECKIN, &rpc_req);
	D_ASSERTF(rc == 0 && rpc_req != NULL, "crt_req_create() failed,"
		  " rc: %d rpc_req: %p\n", rc, rpc_req);

	rpc_req_input = crt_req_get(rpc_req);
	D_ASSERTF(rpc_req_input != NULL, "crt_req_get() failed."
		  " rpc_req_input: %p\n", rpc_req_input);

	if (D_SHOULD_FAIL(test_g.t_fault_attr_1000)) {
		buffer = NULL;
	} else {
		D_ALLOC(buffer, 256);
		D_INFO("not injecting fault.\n");
	}

	D_ASSERTF(buffer != NULL, "Cannot allocate memory.\n");
	snprintf(buffer,  256, "Guest %d", rank);
	rpc_req_input->name = buffer;
	rpc_req_input->age = 21;
	rpc_req_input->days = 7;
	rpc_req_input->bool_val = true;
	D_DEBUG(DB_TEST, "client(rank %d) sending checkin rpc with tag "
		"%d, name: %s, age: %d, days: %d, bool_val %d.\n",
		rank, server_ep.ep_tag, rpc_req_input->name,
		rpc_req_input->age, rpc_req_input->days,
		rpc_req_input->bool_val);

	rc = crt_req_send(rpc_req, client_cb_common, NULL);
	D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);
}

int
test_parse_args(int argc, char **argv)
{
	int				option_index = 0;
	int				rc = 0;
	struct option			long_options[] = {
		{"name", required_argument, 0, 'n'},
		{"attach_to", required_argument, 0, 'a'},
		{"holdtime", required_argument, 0, 'h'},
		{"hold", no_argument, &test_g.t_hold, 1},
		{"srv_ctx_num", required_argument, 0, 'c'},
		{"shut_only", no_argument, &test_g.t_shut_only, 1},
		{"cfg_path", required_argument, 0, 's'},
		{"use_cfg", required_argument, 0, 'u'},
		{0, 0, 0, 0}
	};

	test_g.t_use_cfg = true;

	while (1) {
		rc = getopt_long(argc, argv, "n:a:c:h:u:", long_options,
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
		case 'h':
			test_g.t_hold = 1;
			test_g.t_hold_time = atoi(optarg);
			break;
		case 's':
			test_g.t_save_cfg = true;
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
#endif
