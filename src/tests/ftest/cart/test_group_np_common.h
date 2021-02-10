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

#define TEST_GROUP_BASE					0x010000000
#define TEST_GROUP_VER					 0

#define MAX_NUM_RANKS		1024
#define MAX_SWIM_STATUSES	1024
#define CRT_CTL_MAX_ARG_STR_LEN (1 << 16)

#include <regex.h>
#include <ctype.h>

struct t_swim_status {
	int rank;
	int swim_status;
};

struct test_t {
	char			*t_local_group_name;
	char			*t_remote_group_name;
	int			 t_hold;
	int			 t_shut_only;
	int			 t_init_only;
	int			 t_skip_init;
	int			 t_skip_shutdown;
	int			 t_skip_check_in;
	bool			 t_save_cfg;
	bool			 t_use_cfg;
	bool			 t_register_swim_callback;
	int			 t_get_swim_status;
	int			 t_shutdown_delay;
	d_rank_t		 cg_ranks[MAX_NUM_RANKS];
	int			 cg_num_ranks;
	struct			 t_swim_status t_verify_swim_status;
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

/* input fields */
#define CRT_ISEQ_TEST_PING_CHECK				 \
	((uint32_t)		(age)			CRT_VAR) \
	((uint32_t)		(days)			CRT_VAR) \
	((d_string_t)		(name)			CRT_VAR) \
	((bool)			(bool_val)		CRT_VAR)

/* output fields */
#define CRT_OSEQ_TEST_PING_CHECK				 \
	((int32_t)		(ret)			CRT_VAR) \
	((uint32_t)		(room_no)		CRT_VAR) \
	((uint32_t)		(bool_val)		CRT_VAR)

CRT_RPC_DECLARE(test_ping_check,
		CRT_ISEQ_TEST_PING_CHECK, CRT_OSEQ_TEST_PING_CHECK)
CRT_RPC_DEFINE(test_ping_check,
	       CRT_ISEQ_TEST_PING_CHECK, CRT_OSEQ_TEST_PING_CHECK)

/* input fields */
#define CRT_ISEQ_TEST_SWIM_STATUS				 \
	((uint32_t)		(rank)			CRT_VAR) \
	((uint32_t)		(exp_status)		CRT_VAR)

/* output fields */
#define CRT_OSEQ_TEST_SWIM_STATUS				 \
	((uint32_t)		(bool_val)		CRT_VAR)

CRT_RPC_DECLARE(test_swim_status,
		CRT_ISEQ_TEST_SWIM_STATUS, CRT_OSEQ_TEST_SWIM_STATUS)
CRT_RPC_DEFINE(test_swim_status,
	       CRT_ISEQ_TEST_SWIM_STATUS, CRT_OSEQ_TEST_SWIM_STATUS)

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

/* Track number of dead-alive swim status changes */
struct rank_status {
  int num_alive;
  int num_dead;
};

/* Keep a table of whether each rank is alive (0) or dead (1) */
static char *swim_status_by_rank[MAX_NUM_RANKS];

static void
test_swim_status_handler(crt_rpc_t *rpc_req)
{
	struct test_swim_status_in	*e_req;
	struct test_swim_status_out	*e_reply;
	int				 rc = 0;
	regex_t				 regex_alive;
	regex_t				 regex_dead;
	static const char		*dead_regex = ".?0*1";
	static const char		*alive_regex = ".?0*";
	char				*swim_seq = malloc(MAX_SWIM_STATUSES);

	/* CaRT internally already allocated the input/output buffer */
	e_req = crt_req_get(rpc_req);

	if (swim_status_by_rank[e_req->rank] != NULL)
		strcpy(swim_seq, swim_status_by_rank[e_req->rank]);
	else
		swim_seq = "";

	/* compile and run regex's */
	regcomp(&regex_dead, dead_regex, REG_EXTENDED);
	int rc_dead = regexec(&regex_dead,
			      swim_seq,
			      0, NULL, 0);
	regcomp(&regex_alive, alive_regex, REG_EXTENDED);
	int rc_alive = regexec(&regex_alive,
			       swim_seq,
			       0, NULL, 0);

	D_ASSERTF(e_req != NULL, "crt_req_get() failed. e_req: %p\n", e_req);

	DBG_PRINT("tier1 test_server recv'd swim_status, opc: %#x.\n",
		  rpc_req->cr_opc);
	DBG_PRINT("tier1 swim_status input - rank: %d, exp_status: %d.\n",
		  e_req->rank, e_req->exp_status);

	if (e_req->exp_status == CRT_EVT_ALIVE)
		D_ASSERTF(rc_alive == 0,
			  "Swim status sequence (%s) does not match '%s'.\n",
			  swim_seq, alive_regex);
	else if (e_req->exp_status == CRT_EVT_DEAD)
		D_ASSERTF(rc_dead == 0,
			  "Swim status sequence (%s) does not match '%s'.\n",
			  swim_seq, dead_regex);

	DBG_PRINT("Rank [%d] SWIM state sequence (%s) for "
		  "status [%d] is as expected.\n",
		  e_req->rank, swim_seq,
		  e_req->exp_status);

	e_reply = crt_reply_get(rpc_req);

	/* If we got past the previous assert, then we've succeeded */
	e_reply->bool_val = true;
	D_ASSERTF(e_reply != NULL, "crt_reply_get() failed. e_reply: %p\n",
		  e_reply);

	rc = crt_reply_send(rpc_req);
	D_ASSERTF(rc == 0, "crt_reply_send() failed. rc: %d\n", rc);

	DBG_PRINT("tier1 test_srver sent swim_status reply,"
		  "e_reply->bool_val: %d.\n",
		  e_reply->bool_val);
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

	DBG_PRINT("tier1 test_srver sent checkin reply, ret: %d, "
		   "room_no: %d.\n", p_reply->ret, p_reply->room_no);
}

static void
client_cb_common(const struct crt_cb_info *cb_info)
{
	crt_rpc_t			*rpc_req;
	struct test_ping_check_in	*test_ping_rpc_req_input;
	struct test_ping_check_out	*test_ping_rpc_req_output;

	struct test_swim_status_in	*swim_status_rpc_req_input;
	struct test_swim_status_out	*swim_status_rpc_req_output;

	rpc_req = cb_info->cci_rpc;

	if (cb_info->cci_arg != NULL) {
		*(int *) cb_info->cci_arg = 1;
	}

	switch (cb_info->cci_rpc->cr_opc) {
	case TEST_OPC_CHECKIN:

		test_ping_rpc_req_input = crt_req_get(rpc_req);
		D_ASSERT(test_ping_rpc_req_input != NULL);
		test_ping_rpc_req_output = crt_reply_get(rpc_req);
		D_ASSERT(test_ping_rpc_req_output != NULL);

		if (cb_info->cci_rc != 0) {
			D_ERROR("rpc (opc: %#x) failed, rc: %d.\n",
				rpc_req->cr_opc, cb_info->cci_rc);
			break;
		}
		DBG_PRINT("%s checkin result - ret: %d, room_no: %d, "
			  "bool_val %d.\n",
			  test_ping_rpc_req_input->name,
			  test_ping_rpc_req_output->ret,
			  test_ping_rpc_req_output->room_no,
			  test_ping_rpc_req_output->bool_val);
		sem_post(&test_g.t_token_to_proceed);
		D_ASSERT(test_ping_rpc_req_output->bool_val == true);
		break;
	case TEST_OPC_SWIM_STATUS:

		swim_status_rpc_req_input = crt_req_get(rpc_req);
		D_ASSERT(swim_status_rpc_req_input != NULL);
		swim_status_rpc_req_output = crt_reply_get(rpc_req);
		D_ASSERT(swim_status_rpc_req_output != NULL);

		if (cb_info->cci_rc != 0) {
			D_ERROR("rpc (opc: %#x) failed, rc: %d.\n",
				rpc_req->cr_opc, cb_info->cci_rc);
			break;
		}
		DBG_PRINT("swim_status result - rank: %d, exp_status: %d, "
			  "result: %d.\n",
			  swim_status_rpc_req_input->rank,
			  swim_status_rpc_req_input->exp_status,
			  swim_status_rpc_req_output->bool_val);
		sem_post(&test_g.t_token_to_proceed);
		D_ASSERT(swim_status_rpc_req_output->bool_val == true);
		break;
	case TEST_OPC_SHUTDOWN:
		DBG_PRINT("Received TEST_OPC_SHUTDOWN.\n");
		tc_progress_stop();
		sem_post(&test_g.t_token_to_proceed);
		break;
	default:
		DBG_PRINT("Received unregistered opcode.\n");
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
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_test_swim_status,
		.prf_hdlr	= test_swim_status_handler,
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
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_test_swim_status,
		.prf_hdlr	= test_swim_status_handler,
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
	snprintf(buffer, 256, "Guest %d", rank);
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

static struct t_swim_status
parse_verify_swim_status_arg(char *source)
{
	char *regexString = "([0-9]+)[ ]*=[ ]*(a|d|alive|dead)";

	struct t_swim_status ss = {-1, '\0'};

	size_t maxMatches = 2;
	size_t maxGroups  = 3;

	unsigned int	 m;
	regex_t		 regexCompiled;
	regmatch_t	 groupArray[maxGroups];
	char		*cursor;

	if (regcomp(&regexCompiled, regexString, REG_EXTENDED | REG_ICASE)) {
		printf("Could not compile regular expression.\n");
		return ss;
	};

	m = 0;
	cursor = source;

	for (m = 0; m < maxMatches; m++) {

		if (regexec(&regexCompiled, cursor, maxGroups, groupArray, 0)) {
			break;	/* No more matches */
		}

		unsigned int g = 0;
		unsigned int offset = 0;

		for (g = 0; g < maxGroups; g++) {

			if (groupArray[g].rm_so == (size_t)-1) {
				break;	/* No more groups */
			}

			if (g == 0) {
				offset = groupArray[g].rm_eo;
			}

			char cursorCopy[strlen(cursor) + 1];

			strcpy(cursorCopy, cursor);
			cursorCopy[groupArray[g].rm_eo] = 0;
			D_DEBUG(DB_TEST,
				"parse_verify_swim_status_arg, match %u, "
					 "group %u: [%2u-%2u]: %s\n",
					 m,
					 g,
					 groupArray[g].rm_so,
					 groupArray[g].rm_eo,
					 cursorCopy + groupArray[g].rm_so);

			if (g == 1) {
				ss.rank = atoi(cursorCopy +
					       groupArray[g].rm_so);
			}
			if (g == 2) {

				char exp_status[8];

				strcpy(exp_status, cursorCopy +
				       groupArray[g].rm_so);

				/* "d(ead)?"=1, a(live)?=0 as
				 * specified in crt_event_type:
				 *
				 * src/include/cart/api.h
				 * enum crt_event_type {
				 *		CRT_EVT_ALIVE,
				 *		CRT_EVT_DEAD,
				 * };
				 */
				ss.swim_status = 0;
				if (tolower(exp_status[0]) == 'd') {
					ss.swim_status = 1;
				}

			}
		}
		cursor += offset;
	}

	regfree(&regexCompiled);

	return ss;
}

/* Source: src/utils/ctl/cart_ctl.c */
static void
parse_rank_string(char *arg_str, d_rank_t *ranks, int *num_ranks)
{
	char		*token;
	char		*saveptr;
	char		*ptr;
	uint32_t	 num_ranks_l = 0;
	uint32_t	 index = 0;
	int		 rstart;
	int		 rend;
	int		 i;

	D_ASSERT(ranks != NULL);
	D_ASSERT(num_ranks != NULL);
	D_ASSERT(arg_str != NULL);
	if (strnlen(arg_str, CRT_CTL_MAX_ARG_STR_LEN) >=
				CRT_CTL_MAX_ARG_STR_LEN) {
		D_ERROR("arg string too long.\n");
		return;
	}

	if (strcmp(arg_str, "all") == 0) {
		*num_ranks = -1;
		return;
	}

	D_DEBUG(DB_TRACE, "arg_str %s\n", arg_str);
	token = strtok_r(arg_str, ",", &saveptr);
	while (token != NULL) {
		ptr = strchr(token, '-');
		if (ptr == NULL) {
			num_ranks_l++;
			if (num_ranks_l > MAX_NUM_RANKS) {
				D_ERROR("Too many target ranks.\n");
				return;
			}
			ranks[index] = atoi(token);
			index++;
			token = strtok_r(NULL, ",", &saveptr);
			continue;
		}
		if (ptr == token || ptr == token + strlen(token)) {
			D_ERROR("Invalid rank range.\n");
			return;
		}
		rstart = atoi(token);
		rend = atoi(ptr + 1);
		num_ranks_l += (rend - rstart + 1);
		if (num_ranks_l > MAX_NUM_RANKS) {
			D_ERROR("Too many target ranks.\n");
			return;
		}
		for (i = rstart; i < rend + 1; i++) {
			ranks[index] = i;
			index++;
		}
		token = strtok_r(NULL, ",", &saveptr);
	}
	*num_ranks = num_ranks_l;
}

int
test_parse_args(int argc, char **argv)
{
	int				option_index = 0;
	int				rc = 0;
	int				ss;
	struct option			long_options[] = {
		{"name", required_argument, 0, 'n'},
		{"attach_to", required_argument, 0, 'a'},
		{"holdtime", required_argument, 0, 'h'},
		{"hold", no_argument, &test_g.t_hold, 1},
		{"srv_ctx_num", required_argument, 0, 'c'},
		{"shut_only", no_argument, &test_g.t_shut_only, 1},
		{"init_only", no_argument, &test_g.t_init_only, 1},
		{"skip_init", no_argument, &test_g.t_skip_init, 1},
		{"skip_shutdown", no_argument, &test_g.t_skip_shutdown, 1},
		{"skip_check_in", no_argument, &test_g.t_skip_check_in, 1},
		{"rank", required_argument, 0, 'r'},
		{"cfg_path", required_argument, 0, 's'},
		{"use_cfg", required_argument, 0, 'u'},
		{"register_swim_callback", required_argument, 0, 'w'},
		{"verify_swim_status", required_argument, 0, 'v'},
		{"get_swim_status", no_argument, 0, 'g'},
		{"shutdown_delay", required_argument, 0, 'd'},
		{0, 0, 0, 0}
	};

	test_g.cg_num_ranks = 0;
	test_g.t_use_cfg = true;
	test_g.t_shutdown_delay = 0;

	/* SWIM testing options */
	test_g.t_get_swim_status = false;
	test_g.t_register_swim_callback = false;

	/* Default value: non-existent rank with status "alive" */
	test_g.t_verify_swim_status = (struct t_swim_status){ -1, 0 };

	struct t_swim_status vss;

	while (1) {
		rc = getopt_long(argc, argv, "n:a:c:h:u:r:", long_options,
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
		case 'w':
			test_g.t_register_swim_callback = atoi(optarg);
			break;
		case 's':
			test_g.t_save_cfg = true;
			test_g.t_cfg_path = optarg;
			break;
		case 'u':
			test_g.t_use_cfg = atoi(optarg);
			break;
		case 'v':
			vss = parse_verify_swim_status_arg(optarg);
			test_g.t_verify_swim_status.rank	= vss.rank;

			/* use short name to stay under 80-char width */
			ss = vss.swim_status;
			test_g.t_verify_swim_status.swim_status = ss;
			break;
		case 'g':
			test_g.t_get_swim_status = true;
			break;
		case 'd':
			test_g.t_shutdown_delay = atoi(optarg);
			break;
		case 'r':
			parse_rank_string(optarg, test_g.cg_ranks,
					  &test_g.cg_num_ranks);
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
