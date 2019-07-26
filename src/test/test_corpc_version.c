/* Copyright (C) 2017-2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * This is a test for the CORPC error case in which the group signatures between
 * participant ranks don't match.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <getopt.h>
#include <semaphore.h>

#include <gurt/common.h>
#include <gurt/atomic.h>
#include <cart/api.h>

#define TEST_CORPC_BASE1 0x010000000
#define TEST_CORPC_BASE2 0x020000000
#define TEST_CORPC_VER 0

#define TEST_OPC_SHUTDOWN CRT_PROTO_OPC(TEST_CORPC_BASE2,		\
						TEST_CORPC_VER, 0)
#define TEST_OPC_CORPC_VER_MISMATCH CRT_PROTO_OPC(TEST_CORPC_BASE1,	\
						TEST_CORPC_VER, 0)
#define TEST_OPC_RANK_EVICT CRT_PROTO_OPC(TEST_CORPC_BASE2,		\
						TEST_CORPC_VER, 1)
#define TEST_OPC_SUBGRP_PING CRT_PROTO_OPC(TEST_CORPC_BASE2,		\
						TEST_CORPC_VER, 2)

struct test_t {
	crt_group_t		*t_local_group;
	crt_group_t		*t_target_group;
	crt_group_t		*t_sub_group;
	char			*t_local_group_name;
	char			*t_target_group_name;
	int			 t_is_service;
	uint32_t		 t_is_client,
				 t_hold:1;
	ATOMIC uint32_t		 t_shutdown;
	uint32_t		 t_holdtime;
	uint32_t		 t_my_rank;
	uint32_t		 t_my_group_size;
	uint32_t		 t_target_group_size;
	crt_context_t		 t_crt_ctx;
	pthread_t		 t_tid;
	sem_t			 t_all_done;
};

struct test_t test;


#define CRT_ISEQ_CORPC_VER_MISMATCH /* input fields */		 \
	((uint32_t)		(magic)			CRT_VAR)

#define CRT_OSEQ_CORPC_VER_MISMATCH /* output fields */		 \
	((uint32_t)		(magic)			CRT_VAR) \
	((uint32_t)		(result)		CRT_VAR)

CRT_RPC_DECLARE(corpc_ver_mismatch,
		CRT_ISEQ_CORPC_VER_MISMATCH, CRT_OSEQ_CORPC_VER_MISMATCH)
CRT_RPC_DEFINE(corpc_ver_mismatch,
		CRT_ISEQ_CORPC_VER_MISMATCH, CRT_OSEQ_CORPC_VER_MISMATCH)

#define CRT_ISEQ_RANK_EVICT	/* input fields */		 \
	((uint32_t)		(rank)			CRT_VAR)

#define CRT_OSEQ_RANK_EVICT	/* output fields */		 \
	((uint32_t)		(rc)			CRT_VAR)

CRT_RPC_DECLARE(rank_evict, CRT_ISEQ_RANK_EVICT, CRT_OSEQ_RANK_EVICT)
CRT_RPC_DEFINE(rank_evict, CRT_ISEQ_RANK_EVICT, CRT_OSEQ_RANK_EVICT)

#define CRT_ISEQ_SUBGRP_PING	/* input fields */		 \
	((uint32_t)		(magic)			CRT_VAR)

#define CRT_OSEQ_SUBGRP_PING	/* output fields */		 \
	((uint32_t)		(magic)			CRT_VAR)

CRT_RPC_DECLARE(subgrp_ping, CRT_ISEQ_SUBGRP_PING, CRT_OSEQ_SUBGRP_PING)
CRT_RPC_DEFINE(subgrp_ping, CRT_ISEQ_SUBGRP_PING, CRT_OSEQ_SUBGRP_PING)

static void client_cb(const struct crt_cb_info *cb_info);

int
test_parse_args(int argc, char **argv)
{
	int			 option_index = 0;
	int			 rc = 0;

	struct option		 long_options[] = {
		{"name", required_argument, 0, 'n'},
		{"attach_to", required_argument, 0, 'a'},
		{"holdtime", required_argument, 0, 'h'},
		{"is_service", no_argument, &test.t_is_service, 1},
		{0, 0, 0, 0}
	};

	while (1) {
		rc = getopt_long(argc, argv, "n:a:", long_options,
				 &option_index);
		if (rc == -1)
			break;
		switch (rc) {
		case 0:
			if (long_options[option_index].flag != 0)
				break;
		case 'n':
			test.t_local_group_name = optarg;
			break;
		case 'a':
			test.t_target_group_name = optarg;
			test.t_is_client = 1;
			break;
		case 'h':
			test.t_holdtime = atoi(optarg);
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

static void *progress_thread(void *arg)
{
	crt_context_t	crt_ctx;
	int		rc;

	crt_ctx = (crt_context_t) arg;
	do {
		rc = crt_progress(crt_ctx, 1, NULL, NULL);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("crt_progress failed rc: %d.\n", rc);
			/* continue calling progress on error */
		}

		if (atomic_load_consume(&test.t_shutdown) == 1)
			break;
		sched_yield();
	} while (1);

	D_ASSERTF(rc == 0 || rc == -DER_TIMEDOUT,
		  "Failure exiting progress loop: rc: %d\n", rc);
	fprintf(stderr, "progress_thread: progress thread exit ...\n");

	pthread_exit(NULL);
}

static void
corpc_ver_mismatch_hdlr(crt_rpc_t *rpc_req)
{
	struct corpc_ver_mismatch_in	*rpc_req_input;
	struct corpc_ver_mismatch_out	*rpc_req_output;
	int				 rc = 0;

	rpc_req_input = crt_req_get(rpc_req);
	rpc_req_output = crt_reply_get(rpc_req);
	D_ASSERT(rpc_req_input != NULL && rpc_req_output != NULL);
	fprintf(stderr, "server received request, opc: 0x%x.\n",
		rpc_req->cr_opc);
	rpc_req_output->result = 1;
	rc = crt_reply_send(rpc_req);
	D_ASSERT(rc == 0);
	fprintf(stderr, "received magic number %d, reply %d\n",
		rpc_req_input->magic, rpc_req_output->result);

	/* now everybody evicts rank 2 so group destroy can succeed */
	rc = crt_rank_evict(test.t_local_group, 2);
	if (rc != DER_SUCCESS)
		D_ERROR("crt_rank_evcit(grp=%p, rank=2) failed, rc %d\n",
			test.t_local_group, rc);
}

static void
test_shutdown_hdlr(crt_rpc_t *rpc_req)
{
	fprintf(stderr, "rpc err server received shutdown request, "
		"opc: 0x%x.\n", rpc_req->cr_opc);
	D_ASSERTF(rpc_req->cr_input == NULL, "RPC request has invalid input\n");
	D_ASSERTF(rpc_req->cr_output == NULL, "RPC request output is NULL\n");

	atomic_store_release(&test.t_shutdown, 1);
	fprintf(stderr, "server set shutdown flag.\n");
}

static void
subgrp_ping_hdlr(crt_rpc_t *rpc_req)
{
	struct subgrp_ping_in	*rpc_req_input;
	struct subgrp_ping_out	*rpc_req_output;
	int			 rc = 0;

	rpc_req_input = crt_req_get(rpc_req);
	D_ASSERT(rpc_req_input != NULL);
	rpc_req_output = crt_reply_get(rpc_req);
	D_ASSERT(rpc_req_output != NULL);

	D_DEBUG(DB_TEST, "Recieved magic number %d\n", rpc_req_input->magic);
	rpc_req_output->magic = rpc_req_input->magic + 1;
	rc = crt_reply_send(rpc_req);
	D_ASSERT(rc == 0);
}

static void
test_rank_evict_hdlr(crt_rpc_t *rpc_req)
{
	struct rank_evict_in	*rpc_req_input;
	struct rank_evict_out	*rpc_req_output;
	int			 rc = 0;

	rpc_req_input = crt_req_get(rpc_req);
	rpc_req_output = crt_reply_get(rpc_req);
	D_ASSERT(rpc_req_input != NULL && rpc_req_output != NULL);

	fprintf(stderr, "server received eviction request, opc: 0x%x.\n",
		rpc_req->cr_opc);

	test.t_sub_group = crt_group_lookup("example_grpid");
	D_ASSERT(test.t_sub_group != NULL);
	rc = crt_rank_evict(test.t_local_group, rpc_req_input->rank);
	D_ASSERT(rc == 0);
	D_DEBUG(DB_TEST, "rank %d evicted rank %d.\n", test.t_my_rank,
		rpc_req_input->rank);

	rpc_req_output->rc = rc;
	rc = crt_reply_send(rpc_req);
	D_ASSERT(rc == 0);
}

static int
corpc_ver_mismatch_aggregate(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct corpc_ver_mismatch_out *reply_source;
	struct corpc_ver_mismatch_out *reply_result;

	D_ASSERT(source != NULL && result != NULL);
	reply_source = crt_reply_get(source);
	reply_result = crt_reply_get(result);
	reply_result->result += reply_source->result;

	printf("corpc_ver_mismatch_aggregate, rank %d, result %d, "
	       "aggregate result %d.\n",
	       test.t_my_rank, reply_source->result, reply_result->result);

	return 0;
}

struct crt_corpc_ops corpc_ver_mismatch_ops = {
	.co_aggregate = corpc_ver_mismatch_aggregate,
	.co_pre_forward = NULL,
};

void
target_shutdown_cmd_issue()
{
	crt_endpoint_t			 server_ep;
	crt_rpc_t			*rpc_req = NULL;
	int				 i;
	int				 rc = 0;

	for (i = 0; i < test.t_target_group_size; i++) {
		server_ep.ep_grp = test.t_target_group;
		server_ep.ep_rank = i;
		server_ep.ep_tag = 0;
		rc = crt_req_create(test.t_crt_ctx, &server_ep,
				    TEST_OPC_SHUTDOWN,
				    &rpc_req);
		D_ASSERTF(rc == 0 && rpc_req != NULL,
			  "crt_req_create() failed, rc: %d rpc_req: %p\n",
			  rc, rpc_req);
		rc = crt_req_send(rpc_req, client_cb, NULL);
		D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);

	}
}

void
local_shutdown_cmd_issue()
{
	crt_endpoint_t			 server_ep;
	crt_rpc_t			*rpc_req = NULL;
	int				 i;
	int				 rc = 0;

	for (i = 0; i < test.t_my_group_size; i++) {
		if (i == test.t_my_rank)
			continue;
		server_ep.ep_grp = test.t_local_group;
		server_ep.ep_rank = i;
		server_ep.ep_tag = 0;
		rc = crt_req_create(test.t_crt_ctx, &server_ep,
				    TEST_OPC_SHUTDOWN,
				    &rpc_req);
		D_ASSERTF(rc == 0 && rpc_req != NULL,
			  "crt_req_create() failed, rc: %d rpc_req: %p\n",
			  rc, rpc_req);
		rc = crt_req_send(rpc_req, client_cb, NULL);
		D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);

	}
}

static int
sub_grp_destroy_cb(void *arg, int status)
{
	fprintf(stderr, "in grp_destroy_cb, arg %p, status %d.\n", arg, status);
	local_shutdown_cmd_issue();

	return 0;
}

static int
rank_evict_cb(crt_rpc_t *rpc_req)
{
	d_rank_t			 excluded_ranks[3] = {1, 3, 6};
	d_rank_list_t			 excluded_membs;
	crt_rpc_t			*corpc_req;
	struct corpc_ver_mismatch_in	*corpc_in;
	struct rank_evict_out		*rpc_req_output;
	int				 rc = 0;


	rpc_req_output = crt_reply_get(rpc_req);
	if (rpc_req_output == NULL)
		return -DER_INVAL;

	excluded_membs.rl_nr = 3;
	excluded_membs.rl_ranks = excluded_ranks;
	rc = crt_corpc_req_create(test.t_crt_ctx, test.t_sub_group,
			&excluded_membs, TEST_OPC_CORPC_VER_MISMATCH,
			NULL, NULL, 0,
			crt_tree_topo(CRT_TREE_KNOMIAL, 4),
			&corpc_req);
	fprintf(stderr, "crt_corpc_req_create()  rc: %d, my_rank %d.\n", rc,
		test.t_my_rank);
	D_ASSERT(rc == 0 && corpc_req != NULL);
	corpc_in = crt_req_get(corpc_req);
	D_ASSERT(corpc_in != NULL);
	corpc_in->magic = random()%100;

	rc = crt_req_send(corpc_req, client_cb, NULL);
	D_ASSERT(rc == 0);


	return 0;
}
static int
corpc_ver_mismatch_cb(crt_rpc_t *rpc_req)
{
	struct corpc_ver_mismatch_in		*rpc_req_input;
	struct corpc_ver_mismatch_out		*rpc_req_output;
	int					 rc = 0;

	rpc_req_input = crt_req_get(rpc_req);
	if (rpc_req_input == NULL)
		return -DER_INVAL;
	rpc_req_output = crt_reply_get(rpc_req);
	if (rpc_req_output == NULL)
		return -DER_INVAL;
	fprintf(stderr, "%s, bounced back magic number: %d, %s\n",
		test.t_local_group_name,
		rpc_req_output->magic,
		rpc_req_output->magic == rpc_req_input->magic ?
		"MATCH" : "MISMATCH");
	rc = crt_group_destroy(test.t_sub_group, sub_grp_destroy_cb,
			&test.t_my_rank);
	fprintf(stderr, "crt_group_destroy rc: %d, arg %p.\n",
		rc, &test.t_my_rank);

	return rc;
}

static int
eviction_rpc_issue(void)
{
	crt_rpc_t		*rpc_req;
	crt_endpoint_t		 server_ep;
	struct rank_evict_in	*rpc_req_input;
	int			 rc = 0;

	/* tell rank 4 to evict rank 2 */
	server_ep.ep_grp = test.t_local_group;
	server_ep.ep_rank = 4;
	server_ep.ep_tag = 0;
	rc = crt_req_create(test.t_crt_ctx, &server_ep, TEST_OPC_RANK_EVICT,
			    &rpc_req);
	D_ASSERTF(rc == 0 && rpc_req != NULL,
		  "crt_req_create() failed, rc: %d rpc_req: %p\n",
		  rc, rpc_req);
	rpc_req_input = crt_req_get(rpc_req);
	D_ASSERTF(rpc_req_input != NULL, "crt_req_get() failed. "
		  "rpc_req_input: %p\n", rpc_req_input);
	rpc_req_input->rank = 2;
	rc = crt_req_send(rpc_req, client_cb, NULL);
	D_ASSERTF(rc == 0, "crt_req_send() failed, rc %d\n", rc);

	return rc;
}

static int
subgrp_ping_cb(crt_rpc_t *rpc_req)
{
	struct subgrp_ping_in	*rpc_req_input;
	struct subgrp_ping_out	*rpc_req_output;
	int			 rc = 0;

	rpc_req_input = crt_req_get(rpc_req);
	D_ASSERT(rpc_req_input != NULL);
	rpc_req_output = crt_reply_get(rpc_req);
	D_ASSERT(rpc_req_output != NULL);

	D_DEBUG(DB_TEST, "Received magic number %d\n", rpc_req_output->magic);
	D_ASSERT(rpc_req_output->magic == rpc_req_input->magic + 1);

	eviction_rpc_issue();

	return rc;
}

static void
client_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t		*rpc_req;

	rpc_req = cb_info->cci_rpc;

	switch (cb_info->cci_rpc->cr_opc) {
	case TEST_OPC_SUBGRP_PING:
		D_DEBUG(DB_TEST, "subgrp_ping got reply\n");
		subgrp_ping_cb(rpc_req);
		break;
	case TEST_OPC_CORPC_VER_MISMATCH:
		fprintf(stderr, "RPC failed, return code: %d.\n",
			cb_info->cci_rc);
/*
 * TODO: This needs to be investigated in a test. Depending on which
 * rank is hit first, we might bet back -DER_NONEXIST instead
 * if rank updated membership list but group version hasnt changed yet
 */
		D_ASSERTF((cb_info->cci_rc == -DER_MISMATCH ||
			cb_info->cci_rc == -DER_NONEXIST),
			"cb_info->cci_rc %d\n", cb_info->cci_rc);
		corpc_ver_mismatch_cb(rpc_req);
		break;
	case TEST_OPC_RANK_EVICT:
		rank_evict_cb(rpc_req);
		break;
	case TEST_OPC_SHUTDOWN:
		sem_post(&test.t_all_done);
		break;
	default:
		break;
	}
}

static void
test_rank_conversion(void)
{
	d_rank_t	rank_out;
	int		rc = 0;

	rc = crt_group_rank_p2s(test.t_sub_group, 2, &rank_out);
	D_ASSERT(rc == 0);
	D_ASSERT(rank_out == 1);

	rc = crt_group_rank_s2p(test.t_sub_group, 3, &rank_out);
	D_ASSERT(rc == 0);
	D_ASSERT(rank_out == 4);
}

static int
sub_grp_create_cb(crt_group_t *grp, void *priv, int status)
{
	crt_endpoint_t			 server_ep;
	crt_rpc_t			*rpc_req = NULL;
	struct subgrp_ping_in		*rpc_req_input;
	int				 rc = 0;

	fprintf(stderr, "sub group created, grp %p, myrank %d, status %d.\n",
		grp, *(int *) priv, status);
	D_ASSERT(status == 0);
	test.t_sub_group = grp;

	/* test rank conversion */
	test_rank_conversion();

	/* send an RPC to a subgroup rank */
	server_ep.ep_grp = test.t_sub_group;
	server_ep.ep_rank = 1;
	server_ep.ep_tag = 0;
	rc = crt_req_create(test.t_crt_ctx, &server_ep, TEST_OPC_SUBGRP_PING,
			    &rpc_req);
	D_ASSERTF(rc == 0 && rpc_req != NULL,
		  "crt_req_create() failed, rc: %d rpc_req: %p\n",
		  rc, rpc_req);
	rpc_req_input = crt_req_get(rpc_req);
	D_ASSERTF(rpc_req_input != NULL, "crt_req_get() failed. "
		  "rpc_req_input: %p\n", rpc_req_input);
	rpc_req_input->magic = 1234;
	rc = crt_req_send(rpc_req, client_cb, NULL);
	D_ASSERTF(rc == 0, "crt_req_send() failed, rc %d\n", rc);

	D_DEBUG(DB_TEST, "exiting\n");

	return rc;
}

static void
test_run(void)
{
	crt_group_id_t		sub_grp_id = "example_grpid";
	d_rank_t		sub_grp_ranks[4] = {4, 3, 1, 2};
	d_rank_list_t		sub_grp_membs;
	int			i;
	int			rc = 0;

	if (test.t_my_group_size < 5 || test.t_my_rank != 3)
		D_GOTO(out, 0);

	/* root: rank 3, participants: rank 1, rank 2, rank 4 */
	sub_grp_membs.rl_nr = 4;
	sub_grp_membs.rl_ranks = sub_grp_ranks;


	rc = crt_group_create(sub_grp_id, &sub_grp_membs, 1,
			      sub_grp_create_cb,
			      &test.t_my_rank);
	fprintf(stderr, "crt_group_create rc: %d, my_rank %d.\n",
		rc, test.t_my_rank);
	D_ASSERT(rc == 0);
	for (i = 0; i < test.t_my_group_size - 1; i++)
		sem_wait(&test.t_all_done);
	atomic_store_release(&test.t_shutdown, 1);
out:
	D_ASSERT(rc == 0);
}

struct crt_proto_rpc_format my_proto_rpc_fmt_corpc[] = {
	{
		.prf_flags      = 0,
		.prf_req_fmt    = &CQF_corpc_ver_mismatch,
		.prf_hdlr       = corpc_ver_mismatch_hdlr,
		.prf_co_ops     = &corpc_ver_mismatch_ops,
	}
};

struct crt_proto_format my_proto_fmt_corpc = {
	.cpf_name = "my-proto-corpc",
	.cpf_ver = TEST_CORPC_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_corpc),
	.cpf_prf = &my_proto_rpc_fmt_corpc[0],
	.cpf_base = TEST_CORPC_BASE1,
};

static struct crt_proto_rpc_format my_proto_rpc_fmt_srv[] = {
	{
		.prf_flags      = 0,
		.prf_req_fmt    = NULL,
		.prf_hdlr       = test_shutdown_hdlr,
		.prf_co_ops     = NULL,
	}, {
		.prf_flags      = 0,
		.prf_req_fmt    = &CQF_rank_evict,
		.prf_hdlr       = test_rank_evict_hdlr,
		.prf_co_ops     = NULL,
	}, {
		.prf_flags      = 0,
		.prf_req_fmt    = &CQF_subgrp_ping,
		.prf_hdlr       = subgrp_ping_hdlr,
		.prf_co_ops     = NULL,
	}
};

static struct crt_proto_format my_proto_fmt_srv = {
	.cpf_name = "my-proto-srv",
	.cpf_ver = TEST_CORPC_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_srv),
	.cpf_prf = &my_proto_rpc_fmt_srv[0],
	.cpf_base = TEST_CORPC_BASE2,
};


void
test_init(void)
{
	int		rc = 0;
	uint32_t	flag;

	rc = d_log_init();
	assert(rc == 0);

	D_DEBUG(DB_TEST, "local group: %s, target group: %s\n",
		test.t_local_group_name,
		test.t_target_group_name ? test.t_target_group_name : "NULL\n");

	flag = test.t_is_service ? CRT_FLAG_BIT_SERVER : 0;
	rc = crt_init(test.t_local_group_name, flag);
	D_ASSERTF(rc == 0, "crt_init() failed, rc: %d\n", rc);

	test.t_local_group = crt_group_lookup(test.t_local_group_name);
	D_ASSERTF(test.t_local_group != NULL, "crt_group_lookup() failed. "
		  "local_group = %p\n", test.t_local_group);

	rc = crt_group_rank(NULL, &test.t_my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank() failed, rc: %d\n", rc);
	D_DEBUG(DB_TEST, "local rank is %d\n", test.t_my_rank);

	rc = crt_group_size(NULL, &test.t_my_group_size);
	D_ASSERTF(rc == 0, "crt_group_size() failed. rc: %d\n", rc);
	D_DEBUG(DB_TEST, "local group size is %d\n", test.t_my_group_size);

	rc = crt_context_create(&test.t_crt_ctx);
	D_ASSERTF(rc == 0, "crt_context_create() failed. rc: %d\n", rc);

	rc = crt_proto_register(&my_proto_fmt_corpc);
	D_ASSERTF(rc == 0, "crt_proto_register() for corpc failed, rc: %d\n",
		rc);

	rc = crt_proto_register(&my_proto_fmt_srv);
	D_ASSERTF(rc == 0, "crt_rpc_srv_register() failed, rc: %d\n", rc);

	rc = sem_init(&test.t_all_done, 0, 0);
	D_ASSERTF(rc == 0, "Could not initialize semaphore\n");

	rc = pthread_create(&test.t_tid, NULL, progress_thread,
			    test.t_crt_ctx);
	D_ASSERTF(rc == 0, "pthread_create() failed. rc: %d\n", rc);

	if (test.t_is_client) {
		rc = crt_group_attach(test.t_target_group_name,
				      &test.t_target_group);
		D_ASSERTF(rc == 0, "crt_group_attach() failed, rc: %d\n", rc);
		D_ASSERTF(test.t_target_group != NULL,
			  "attached group is NULL.\n");
		rc = crt_group_size(test.t_target_group,
			       &test.t_target_group_size);
		D_ASSERTF(rc == 0, "crt_group_size() failed. rc: %d\n", rc);
		D_DEBUG(DB_TEST, "sizeof %s is %d\n", test.t_target_group_name,
			test.t_target_group_size);
	}
}

void
test_fini()
{
	int		rc;

	if (test.t_holdtime != 0)
		sleep(test.t_holdtime);

	if (test.t_is_client) {
		if (test.t_my_rank == 0)
			target_shutdown_cmd_issue();

		rc = crt_group_detach(test.t_target_group);
		D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	}

	rc = pthread_join(test.t_tid, NULL);
	D_ASSERTF(rc == 0, "pthread_join() failed, rc: %d\n", rc);

	rc = crt_context_destroy(test.t_crt_ctx, 0);
	D_ASSERTF(rc == 0, "crt_context_destroy() failed. rc: %d\n", rc);
	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	d_log_fini();
}

int main(int argc, char **argv)
{
	int	rc;

	rc = test_parse_args(argc, argv);
	if (rc != 0) {
		fprintf(stderr, "test_parse_args() failed, rc: %d.\n", rc);
		return rc;
	}
	test_init();
	test_run();
	test_fini();

	return rc;
}
