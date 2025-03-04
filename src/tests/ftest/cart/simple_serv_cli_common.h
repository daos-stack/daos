/*
 * (C) Copyright 2019-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __SIMPLE_COMMON_H__
#define __SIMPLE_COMMON_H__

#define MY_BASE 0x010000000
#define MY_VER  0

#define DBG_PRINT(x...)                                                                            \
	do {                                                                                       \
		fprintf(stderr, x);                                                                \
		D_INFO(x);                                                                         \
	} while (0)

#define NUM_SERVER_CTX 8

#define RPC_DECLARE(name)                                                                          \
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)                                    \
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)

enum { RPC_PING = CRT_PROTO_OPC(MY_BASE, MY_VER, 0), RPC_SHUTDOWN } rpc_id_t;

#define CRT_ISEQ_RPC_PING /* input fields */                                                       \
	((uint32_t)(seq)CRT_VAR)((uint32_t)(delay_sec)CRT_VAR)((d_iov_t)(test_data)CRT_VAR)

#define CRT_OSEQ_RPC_PING     /* output fields */ ((uint32_t)(seq)CRT_VAR)((uint32_t)(rc)CRT_VAR)

#define CRT_ISEQ_RPC_SHUTDOWN /* input fields */ ((uint64_t)(field)CRT_VAR)

#define CRT_OSEQ_RPC_SHUTDOWN /* output fields */ ((uint64_t)(field)CRT_VAR)

int
handler_ping(crt_rpc_t *rpc);
int
handler_shutdown(crt_rpc_t *rpc);

RPC_DECLARE(RPC_PING);
RPC_DECLARE(RPC_SHUTDOWN);

struct crt_proto_rpc_format my_proto_rpc_fmt[] = {{
						      .prf_flags   = 0,
						      .prf_req_fmt = &CQF_RPC_PING,
						      .prf_hdlr    = (void *)handler_ping,
						      .prf_co_ops  = NULL,
						  },
						  {
						      .prf_flags   = 0,
						      .prf_req_fmt = &CQF_RPC_SHUTDOWN,
						      .prf_hdlr    = (void *)handler_shutdown,
						      .prf_co_ops  = NULL,
						  }};

struct crt_proto_format     my_proto_fmt = {
	.cpf_name  = "my-proto",
	.cpf_ver   = MY_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt),
	.cpf_prf   = &my_proto_rpc_fmt[0],
	.cpf_base  = MY_BASE,
};

#endif
