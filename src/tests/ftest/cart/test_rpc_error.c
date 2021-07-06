/*
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This is a test for the RPC error case in which the RPC handler doesn't call
 * crt_reply_send().
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <getopt.h>
#include <semaphore.h>

#include <gurt/common.h>
#include <cart/api.h>

#define TEST_RPC_ERROR_BASE             0x010000000
#define TEST_RPC_ERROR_VER              0

#define RPC_ERR_OPC_NOREPLY	CRT_PROTO_OPC(TEST_RPC_ERROR_BASE, \
					TEST_RPC_ERROR_VER, 0)
#define RPC_ERR_OPC_NORPC	CRT_PROTO_OPC(TEST_RPC_ERROR_BASE, \
					TEST_RPC_ERROR_VER, 1)
#define RPC_ERR_OPC_SHUTDOWN	CRT_PROTO_OPC(TEST_RPC_ERROR_BASE, \
					TEST_RPC_ERROR_VER, 2)

struct rpc_err_t {
	crt_group_t		*re_local_group;
	crt_group_t		*re_target_group;
	char			*re_local_group_name;
	char			*re_target_group_name;
	int			 re_is_service;
	uint32_t		 re_is_client,
				 re_hold:1,
				 re_shutdown:1;
	uint32_t		 re_holdtime;
	uint32_t		 re_my_rank;
	uint32_t		 re_target_group_size;
	crt_context_t		 re_crt_ctx;
	pthread_t		 re_tid;
	sem_t			 re_all_done;
};

struct rpc_err_t rpc_err;

#define CRT_ISEQ_RPC_ERR_NOREPLY /* input fields */		 \
	((uint32_t)		(magic)			CRT_VAR)

#define CRT_OSEQ_RPC_ERR_NOREPLY /* output fields */		 \
	((uint32_t)		(magic)			CRT_VAR)

CRT_RPC_DECLARE(rpc_err_noreply,
		CRT_ISEQ_RPC_ERR_NOREPLY, CRT_OSEQ_RPC_ERR_NOREPLY)
CRT_RPC_DEFINE(rpc_err_noreply,
		CRT_ISEQ_RPC_ERR_NOREPLY, CRT_OSEQ_RPC_ERR_NOREPLY)

int
rpc_err_parse_args(int argc, char **argv)
{
	int			 option_index = 0;
	int			 rc = 0;

	struct option		 long_options[] = {
		{"name", required_argument, 0, 'n'},
		{"attach_to", required_argument, 0, 'a'},
		{"holdtime", required_argument, 0, 'h'},
		{"is_service", no_argument, &rpc_err.re_is_service, 1},
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
			rpc_err.re_local_group_name = optarg;
			break;
		case 'a':
			rpc_err.re_target_group_name = optarg;
			rpc_err.re_is_client = 1;
			break;
		case 'h':
			rpc_err.re_holdtime = atoi(optarg);
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
	int			rc;
	crt_context_t		crt_ctx;

	crt_ctx = (crt_context_t) arg;
	do {
		rc = crt_progress(crt_ctx, 1);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("crt_progress failed rc: %d.\n", rc);
			break;
		}

		if (rpc_err.re_shutdown == 1)
			break;
	} while (1);

	printf("progress_thread: rc: %d, do_shutdown: %d.\n",
	       rc, rpc_err.re_shutdown);
	printf("progress_thread: progress thread exit ...\n");

	pthread_exit(NULL);
}

static void
rpc_err_noreply_hdlr(crt_rpc_t *rpc_req)
{
	struct rpc_err_noreply_in		*rpc_req_input;

	rpc_req_input = crt_req_get(rpc_req);
	D_ASSERTF(rpc_req_input != NULL,
		  "crt_req_get() failed, rpc_req_input %p\n", rpc_req_input);
	fprintf(stderr, "rpc error server received request, opc: %#x.\n",
		rpc_req->cr_opc);
	fprintf(stderr, "received magic number %d\n", rpc_req_input->magic);
}

static void
rpc_err_shutdown_hdlr(crt_rpc_t *rpc_req)
{
	int		rc = 0;

	fprintf(stderr, "rpc err server received shutdown request, "
		"opc: %#x.\n", rpc_req->cr_opc);
	D_ASSERTF(rpc_req->cr_input == NULL, "RPC request has invalid input\n");
	D_ASSERTF(rpc_req->cr_output == NULL, "RPC request output is NULL\n");

	rc = crt_reply_send(rpc_req);
	D_ASSERT(rc == 0);
	printf("rpc err server sent shutdown response.\n");
	rpc_err.re_shutdown = 1;
	fprintf(stderr, "rpc err server set shutdown flag.\n");
}

static struct crt_proto_rpc_format my_proto_rpc_fmt_rpc_error[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_rpc_err_noreply,
		.prf_hdlr	= rpc_err_noreply_hdlr,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= NULL,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= NULL,
		.prf_hdlr	= rpc_err_shutdown_hdlr,
		.prf_co_ops	= NULL,
	}
};

static struct crt_proto_format my_proto_fmt_rpc_error = {
	.cpf_name = "my-proto-rpc_error",
	.cpf_ver = TEST_RPC_ERROR_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_rpc_error),
	.cpf_prf = &my_proto_rpc_fmt_rpc_error[0],
	.cpf_base = TEST_RPC_ERROR_BASE,
};


void
rpc_err_init(void)
{
	int		rc = 0;
	uint32_t	flag;

	D_DEBUG(DB_TEST, "local group: %s, target group: %s\n",
		rpc_err.re_local_group_name,
		rpc_err.re_target_group_name);

	rc = d_log_init();
	assert(rc == 0);

	flag = rpc_err.re_is_service ? CRT_FLAG_BIT_SERVER : 0;
	rc = crt_init(rpc_err.re_local_group_name, flag);
	D_ASSERTF(rc == 0, "crt_init() failed, rc: %d\n", rc);

	rc = crt_group_rank(NULL, &rpc_err.re_my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank() failed, rc: %d\n", rc);

	rc = crt_context_create(&rpc_err.re_crt_ctx);
	D_ASSERTF(rc == 0, "crt_context_create() failed. rc: %d\n", rc);

	rc = crt_proto_register(&my_proto_fmt_rpc_error);

	D_ASSERTF(rc == 0, "crt_proto_register() failed, rc: %d\n", rc);

	rc = sem_init(&rpc_err.re_all_done, 0, 0);
	D_ASSERTF(rc == 0, "Could not initialize semaphore\n");

	rc = pthread_create(&rpc_err.re_tid, NULL, progress_thread,
			    rpc_err.re_crt_ctx);
	D_ASSERTF(rc == 0, "pthread_create() failed. rc: %d\n", rc);
}

void
rpc_err_fini()
{
	int		rc;

	rc = pthread_join(rpc_err.re_tid, NULL);
	D_ASSERTF(rc == 0, "pthread_join() failed, rc: %d\n", rc);

	rc = crt_context_destroy(rpc_err.re_crt_ctx, 0);
	D_ASSERTF(rc == 0, "crt_context_destroy() failed. rc: %d\n", rc);
	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	d_log_fini();
}

static void
client_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t			*rpc_req;
	struct rpc_err_noreply_in	*rpc_req_input;
	struct rpc_err_noreply_out	*rpc_req_output;
	struct rpc_err_t		*re = cb_info->cci_arg;

	rpc_req = cb_info->cci_rpc;

	switch (cb_info->cci_rpc->cr_opc) {
	case RPC_ERR_OPC_NOREPLY:
		fprintf(stderr, "RPC failed, return code: %d.\n",
			cb_info->cci_rc);
		D_ASSERT(cb_info->cci_rc == -DER_NOREPLY);
		rpc_req_input = crt_req_get(rpc_req);
		D_ASSERT(rpc_req_input != NULL);
		rpc_req_output = crt_reply_get(rpc_req);
		D_ASSERT(rpc_req_output != NULL);
		fprintf(stderr, "%s, bounced back magic number: %d, %s\n",
			rpc_err.re_local_group_name,
			rpc_req_output->magic,
			rpc_req_output->magic == rpc_req_input->magic ?
			"MATCH" : "MISMATCH");
		sem_post(&re->re_all_done);
		break;
	case RPC_ERR_OPC_NORPC:
		fprintf(stderr, "RPC failed, return code: %d.\n",
			cb_info->cci_rc);
		D_ASSERT(cb_info->cci_rc == -DER_UNREG);
		sem_post(&re->re_all_done);
		break;
	case RPC_ERR_OPC_SHUTDOWN:
		sem_post(&re->re_all_done);
		break;
	default:
		D_ASSERTF(false, "The default case should never occur.\n");
		break;
	}
}

static void
rpc_err_rpc_issue()
{
	crt_endpoint_t			 server_ep;
	crt_rpc_t			*rpc_req = NULL;
	struct rpc_err_noreply_in	*rpc_req_input;
	int				 i;
	int				 rc = 0;

	for (i = 0; i < rpc_err.re_target_group_size; i++) {
		server_ep.ep_grp = rpc_err.re_target_group;
		server_ep.ep_rank = i;
		server_ep.ep_tag = 0;
		rc = crt_req_create(rpc_err.re_crt_ctx, &server_ep,
				    RPC_ERR_OPC_NOREPLY,
				    &rpc_req);
		D_ASSERTF(rc == 0 && rpc_req != NULL,
			  "crt_req_create() failed, rc: %d rpc_req: %p\n",
			  rc, rpc_req);

		rpc_req_input = crt_req_get(rpc_req);
		D_ASSERTF(rpc_req_input != NULL, "crt_req_get() failed. "
			  "rpc_req_input: %p\n", rpc_req_input);
		rpc_req_input->magic = random()%100;
		D_DEBUG(DB_TEST, "client rank %d sending magic number %d to "
			"rank %d, tag %d.\n",
			rpc_err.re_my_rank, rpc_req_input->magic,
			server_ep.ep_rank, server_ep.ep_tag);

		rc = crt_req_send(rpc_req, client_cb, &rpc_err);
		D_ASSERTF(rc == 0, "crt_req_send() failed, rc %d\n", rc);

		rpc_req = NULL;

		rc = crt_req_create(rpc_err.re_crt_ctx, NULL,
				    RPC_ERR_OPC_NORPC,
				    &rpc_req);
		D_ASSERTF(rc == 0 && rpc_req != NULL,
			  "crt_req_create() failed, rc: %d rpc_req: %p\n",
			  rc, rpc_req);

		rc = crt_req_set_endpoint(rpc_req, &server_ep);
		D_ASSERTF(rc == 0, "crt_set_endpoint() failed, rc %d\n", rc);

		rc = crt_req_send(rpc_req, client_cb, &rpc_err);
		D_ASSERTF(rc == 0, "crt_req_send() failed, rc %d\n", rc);

	}
	for (i = 0; i < rpc_err.re_target_group_size; i++) {
		/* Wait twice for each target here as two RPCs are sent for
		 * each target in the loop above.
		 */
		D_DEBUG(DB_TEST, "Waiting on reply %d\n", i * 2);
		sem_wait(&rpc_err.re_all_done);
		D_DEBUG(DB_TEST, "Waiting on reply %d\n", (i * 2) + 1);
		sem_wait(&rpc_err.re_all_done);
	}
}

void
shutdown_cmd_issue()
{
	crt_endpoint_t			 server_ep;
	crt_rpc_t			*rpc_req = NULL;
	int				 i;
	int				 rc = 0;

	for (i = 0; i < rpc_err.re_target_group_size; i++) {
		server_ep.ep_grp = rpc_err.re_target_group;
		server_ep.ep_rank = i;
		server_ep.ep_tag = 0;
		rc = crt_req_create(rpc_err.re_crt_ctx, &server_ep,
				    RPC_ERR_OPC_SHUTDOWN,
				    &rpc_req);
		D_ASSERTF(rc == 0 && rpc_req != NULL,
			  "crt_req_create() failed, rc: %d rpc_req: %p\n",
			  rc, rpc_req);
		rc = crt_req_send(rpc_req, client_cb, &rpc_err);
		D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);
	}

	for (i = 0; i < rpc_err.re_target_group_size; i++)
		sem_wait(&rpc_err.re_all_done);
	rpc_err.re_shutdown = 1;
}

static void
rpc_err_test_run(void)
{
	int		rc;

	if (rpc_err.re_is_client) {
		/* try until success */
		do {
			rc = crt_group_attach(rpc_err.re_target_group_name,
					      &rpc_err.re_target_group);
		} while (rc != 0);
		D_ASSERTF(rc == 0, "crt_group_attach() failed, rc: %d\n", rc);
		D_ASSERTF(rpc_err.re_target_group != NULL,
			  "attached group is NULL.\n");
		rc = crt_group_size(rpc_err.re_target_group,
			       &rpc_err.re_target_group_size);
		D_ASSERTF(rc == 0, "crt_group_size() failed. rc: %d\n", rc);
		D_DEBUG(DB_TEST, "sizeof %s is %d\n",
			rpc_err.re_target_group_name,
			rpc_err.re_target_group_size);
	}

	if (rpc_err.re_is_client)
		rpc_err_rpc_issue();

	if (rpc_err.re_holdtime != 0)
		sleep(rpc_err.re_holdtime);

	if (rpc_err.re_is_client) {
		if (rpc_err.re_my_rank == 0)
			shutdown_cmd_issue();

		rc = crt_group_detach(rpc_err.re_target_group);
		D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	}
}

int main(int argc, char **argv)
{
	int	rc;

	rc = rpc_err_parse_args(argc, argv);
	if (rc != 0) {
		fprintf(stderr, "rpc_err_parse_args() failed, rc: %d.\n", rc);
		return rc;
	}
	rpc_err_init();
	rpc_err_test_run();
	rpc_err_fini();

	return rc;
}
