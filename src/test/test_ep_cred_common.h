/* Copyright (C) 2018-2019 Intel Corporation
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
#ifndef __TEST_EP_CRED_COMMON_H__
#define __TEST_EP_CRED_COMMON_H__

#include <getopt.h>
#include <semaphore.h>

#define OPC_MY_PROTO    (0x01000000)
#define OPC_PING	(0x01000000)
#define OPC_PING_FRONT	(0x01000001)
#define OPC_SHUTDOWN    (0x01000002)

struct test_global_t {
	crt_group_t		*tg_local_group;
	crt_group_t		*tg_remote_group;
	char			*tg_local_group_name;
	char			*tg_remote_group_name;
	uint32_t		 tg_remote_group_size;
	uint32_t		 tg_is_service:1,
				 tg_should_attach:1,
				 /* notify the progress thread to exit */
				 tg_shutdown:1,
				 tg_hold:1;
	uint32_t		 tg_my_rank;
	crt_context_t		 tg_crt_ctx;
	pthread_t		 tg_tid;
	int			 tg_thread_id;
	sem_t			 tg_token_to_proceed;
	sem_t			 tg_queue_front_token;
	int			 tg_credits;
	int			 tg_burst_count;
	int			 tg_send_shutdown;
	int			 tg_send_queue_front;
	bool			 tg_save_cfg;
	char			*tg_cfg_path;
};

struct test_global_t test = {0};

#define CRT_ISEQ_PING		/* input fields */		 \
	((uint32_t)		(pi_delay)		CRT_VAR)

#define CRT_OSEQ_PING		/* output fields */		 \
	((uint32_t)		(po_magic)		CRT_VAR)

CRT_RPC_DECLARE(ping, CRT_ISEQ_PING, CRT_OSEQ_PING)
CRT_RPC_DEFINE(ping, CRT_ISEQ_PING, CRT_OSEQ_PING)

static void
ping_hdlr_0(crt_rpc_t *rpc_req)
{
	int		rc;
	struct ping_in	*input;

	D_DEBUG(DB_TRACE, "entered %s().\n", __func__);

	input = crt_req_get(rpc_req);
	if (input->pi_delay != 0) {
		D_DEBUG(DB_TRACE, "sleep for %d\n", input->pi_delay);
		sleep(input->pi_delay);
	}

	rc = crt_reply_send(rpc_req);
	D_ASSERTF(rc == 0, "crt_reply_send() failed. rc: %d\n", rc);
}

static void
ping_hdlr_1(crt_rpc_t *rpc_req)
{
	int rc;

	D_DEBUG(DB_TRACE, "entered %s().\n", __func__);
	rc = crt_reply_send(rpc_req);
	D_ASSERTF(rc == 0, "crt_reply_send() failed. rc: %d\n", rc);
}


static void
shutdown_handler(crt_rpc_t *rpc_req)
{
	DBG_PRINT("received shutdown request, opc: %#x.\n", rpc_req->cr_opc);

	D_ASSERTF(rpc_req->cr_input == NULL, "RPC request has invalid input\n");
	D_ASSERTF(rpc_req->cr_output == NULL, "RPC request output is NULL\n");

	g_shutdown = 1;
	DBG_PRINT("server set shutdown flag.\n");
}

struct crt_proto_rpc_format my_proto_rpc_fmt_0[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_ping,
		.prf_hdlr	= ping_hdlr_0,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= CRT_RPC_FEAT_QUEUE_FRONT,
		.prf_req_fmt	= &CQF_ping,
		.prf_hdlr	= ping_hdlr_1,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= CRT_RPC_FEAT_NO_REPLY,
		.prf_req_fmt	= NULL,
		.prf_hdlr	= shutdown_handler,
		.prf_co_ops	= NULL,
	}
};


struct crt_proto_format my_proto_fmt_0 = {
	.cpf_name = "my-proto",
	.cpf_ver = 0,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_0),
	.cpf_prf = &my_proto_rpc_fmt_0[0],
	.cpf_base = OPC_MY_PROTO,
};


int
test_parse_args(int argc, char **argv)
{
	int				option_index = 0;
	int				rc = 0;
	struct option			long_options[] = {
		{"name",	required_argument,	0, 'n'},
		{"attach_to",	required_argument,	0, 'a'},
		{"hold",	no_argument,		0, 'h'},
		{"is_service",	no_argument,		0, 's'},
		{"credits",	required_argument,	0, 'c'},
		{"burst",	required_argument,	0, 'b'},
		{"queue_front",	no_argument,		0, 'f'},
		{"shutdown",	no_argument,		0, 'q'},
		{"cfg_path",	required_argument,	0, 'p'},
		{0, 0, 0, 0}
	};

	while (1) {
		rc = getopt_long(argc, argv, "n:a:b:c:p:fhsq", long_options,
				 &option_index);
		if (rc == -1)
			break;
		switch (rc) {
		case 0:
			if (long_options[option_index].flag != 0)
				break;
		case 'n':
			test.tg_local_group_name = optarg;
			break;
		case 'a':
			test.tg_remote_group_name = optarg;
			test.tg_should_attach = 1;
			break;
		case 'h':
			test.tg_hold = 1;
			break;
		case 's':
			test.tg_is_service = 1;
			break;
		case 'b':
			test.tg_burst_count = atoi(optarg);
			break;
		case 'c':
			test.tg_credits = atoi(optarg);
			break;
		case 'q':
			test.tg_send_shutdown = 1;
			break;
		case 'f':
			test.tg_send_queue_front = 1;
			break;
		case 'p':
			test.tg_save_cfg = true;
			test.tg_cfg_path = optarg;
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

#endif /* __TEST_EP_CRED_COMMON_H__ */
