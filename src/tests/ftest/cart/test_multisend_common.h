/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __TEST_MULTISEND_COMMON_H__
#define __TEST_MULTISEND_COMMON_H__

#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <cart/api.h>
#include <cart/types.h>
#include "crt_utils.h"
#include <signal.h>

#define MY_BASE 0x010000000
#define MY_VER  0

#define NUM_SERVER_CTX 8
#define MAX_NUM_CLIENT_CTX 32


#define RPC_DECLARE(name)					\
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)	\
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)

enum {
	RPC_PING = CRT_PROTO_OPC(MY_BASE, MY_VER, 0),
	RPC_SHUTDOWN
} rpc_id_t;

#define CRT_ISEQ_RPC_PING	/* input fields */		 \
	((crt_bulk_t)		(bulk_hdl)		CRT_VAR) \
	((uint64_t)		(chunk_size)		CRT_VAR) \
	((uint64_t)		(chunk_index)		CRT_VAR) \
	((bool)			(do_put)		CRT_VAR)

#define CRT_OSEQ_RPC_PING	/* output fields */		 \
	((int64_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_SHUTDOWN	/* input fields */		 \
	((uint64_t)		(field)			CRT_VAR)

#define CRT_OSEQ_RPC_SHUTDOWN	/* output fields */		 \
	((uint64_t)		(field)			CRT_VAR)

static int handler_ping(crt_rpc_t *rpc);
static int handler_shutdown(crt_rpc_t *rpc);

RPC_DECLARE(RPC_PING);
RPC_DECLARE(RPC_SHUTDOWN);

struct crt_proto_rpc_format my_proto_rpc_fmt[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_RPC_PING,
		.prf_hdlr	= (void *)handler_ping,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_RPC_SHUTDOWN,
		.prf_hdlr	= (void *)handler_shutdown,
		.prf_co_ops	= NULL,
	}
};

struct crt_proto_format my_proto_fmt = {
	.cpf_name = "my-proto",
	.cpf_ver = MY_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt),
	.cpf_prf = &my_proto_rpc_fmt[0],
	.cpf_base = MY_BASE,
};

enum test_sync_mode_t {
	TEST_MODE_SYNC  = 1,
	TEST_MODE_ASYNC = 2,
};

struct test_global_t {
	crt_group_t		*tg_local_group;
	crt_group_t		*tg_remote_group;
	char			*tg_local_group_name;
	char			*tg_remote_group_name;
	uint32_t		 tg_remote_group_size;
	uint32_t		 tg_is_service:1,
				 tg_should_attach:1;
	uint32_t		 tg_my_rank;
	crt_context_t		 tg_crt_ctx[MAX_NUM_CLIENT_CTX];
	pthread_t		 tg_tid[MAX_NUM_CLIENT_CTX];
	int			 tg_thread_id;
	sem_t			 tg_token_to_proceed;
	bool			 tg_use_cfg;
	bool			 tg_save_cfg;
	bool			 tg_do_put;
	bool			 tg_do_shutdown;
	bool			 tg_use_daos_agent_env;
	char			*tg_cfg_path;
	int			 tg_test_mode;
	int			 tg_num_ctx;
	int			 tg_num_iterations;
	int			 tg_chunk_size_kb;
	int			 tg_force_rank;
};

struct test_global_t test = {0};


static int
handler_shutdown(crt_rpc_t *rpc)
{
	DBG_PRINT("received shutdown request\n");
	crt_reply_send(rpc);
	crtu_progress_stop();
	return 0;
}


static void
show_usage(void)
{
	printf("Usage: ./test_multisend_client [-acfspqmex]\n");
	printf("Options:\n");
	printf("-a [--attach-to <group_name>] : server group to attach to\n");
	printf("-s [--cfg-path <path>]: path to attach info file\n");
	printf("-c <kb>: Chunk size in kb\n");
	printf("-e <num>: Number of client contexts to use\n");
	printf("-n <num>: Number of iterations\n");
	printf("-f <rank>: Force all rpcs to go to the specified rank\n");
	printf("-x: When set performs DMA_PUT to client instead of DMA_GET\n");
	printf("-m: Mode. 1 - Synchronous, 2 - Asynchronous\n");
	printf("-q: Shut servers down at the end of the run\n");
}

int
test_parse_args(int argc, char **argv)
{
	int				option_index = 0;
	int				rc = 0;
	struct option			long_options[] = {
		{"name",	required_argument,	0, 'g'},
		{"attach_to",	required_argument,	0, 'a'},
		{"cfg_path",	required_argument,	0, 's'},
		{"num_ctx",	required_argument,	0, 'e'},
		{0, 0, 0, 0}
	};

	test.tg_use_cfg = true;
	test.tg_use_daos_agent_env = false;
	test.tg_num_ctx = 1;
	test.tg_do_put = false;
	test.tg_force_rank = -1;

	while (1) {
		rc = getopt_long(argc, argv, "g:c:n:a:s:p:m:e:f:xq", long_options,
				 &option_index);
		if (rc == -1)
			break;

		switch (rc) {
		case 0:
			if (long_options[option_index].flag != 0)
				break;
		case 'a':
			test.tg_remote_group_name = optarg;
			test.tg_should_attach = 1;
			break;
		case 'c':
			test.tg_chunk_size_kb = atoi(optarg);
			break;
		case 'e':
			test.tg_num_ctx = atoi(optarg);
			break;
		case 'f':
			test.tg_force_rank = atoi(optarg);
			break;
		case 'g':
			test.tg_local_group_name =  optarg;
			break;
		case 'm':
			test.tg_test_mode = atoi(optarg);
			break;
		case 'n':
			test.tg_num_iterations = atoi(optarg);
			break;
		case 'q':
			test.tg_do_shutdown = true;
			break;
		case 's':
			test.tg_save_cfg = true;
			test.tg_cfg_path = optarg;
			break;
		case 'x':
			test.tg_do_put = true;
			break;
		case '?':
		default:
			show_usage();
			return 1;
		}
	}
	if (optind < argc) {
		show_usage();
		return 1;
	}

	return 0;
}

#endif /* __TEST_MULTISEND_COMMON_H__ */
