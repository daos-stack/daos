/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __TEST_RPC_RECONNECT_COMMON_H__
#define __TEST_RPC_RECONNECT_COMMON_H__

#include <cart/api.h>

#include "crt_utils.h"

/*
 * Transport-neutral protocol: echo a per-worker sequence to validate replies,
 * then shut down the recovered server only after the senders have drained.
 */
#define RPC_RECONNECT_BASE          0x010000000
#define RPC_RECONNECT_VER           0

#define CRT_ISEQ_RPC_RECONNECT_PING /* input fields */ ((uint64_t)(sequence)CRT_VAR)

#define CRT_OSEQ_RPC_RECONNECT_PING /* output fields */ ((uint64_t)(sequence)CRT_VAR)

#define CRT_ISEQ_RPC_RECONNECT_SHUTDOWN /* input fields */ ((uint64_t)(field)CRT_VAR)

#define CRT_OSEQ_RPC_RECONNECT_SHUTDOWN /* output fields */ ((uint64_t)(field)CRT_VAR)

CRT_RPC_DECLARE(RPC_RECONNECT_PING, CRT_ISEQ_RPC_RECONNECT_PING, CRT_OSEQ_RPC_RECONNECT_PING)
CRT_RPC_DEFINE(RPC_RECONNECT_PING, CRT_ISEQ_RPC_RECONNECT_PING, CRT_OSEQ_RPC_RECONNECT_PING)
CRT_RPC_DECLARE(RPC_RECONNECT_SHUTDOWN, CRT_ISEQ_RPC_RECONNECT_SHUTDOWN,
		CRT_OSEQ_RPC_RECONNECT_SHUTDOWN)
CRT_RPC_DEFINE(RPC_RECONNECT_SHUTDOWN, CRT_ISEQ_RPC_RECONNECT_SHUTDOWN,
	       CRT_OSEQ_RPC_RECONNECT_SHUTDOWN)

enum {
	RPC_RECONNECT_PING_OPCODE = CRT_PROTO_OPC(RPC_RECONNECT_BASE, RPC_RECONNECT_VER, 0),
	RPC_RECONNECT_SHUTDOWN_OPCODE,
};

static int
rpc_reconnect_ping_handler(crt_rpc_t *rpc)
{
	struct RPC_RECONNECT_PING_in  *input;
	struct RPC_RECONNECT_PING_out *output;
	int                            rc;

	input            = crt_req_get(rpc);
	output           = crt_reply_get(rpc);
	output->sequence = input->sequence;

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("crt_reply_send() failed; rc=%d\n", rc);

	return rc;
}

static int
rpc_reconnect_shutdown_handler(crt_rpc_t *rpc)
{
	int rc;

	rc = crt_reply_send(rpc);
	if (rc != 0) {
		D_ERROR("crt_reply_send() for shutdown failed; rc=%d\n", rc);
		return rc;
	}

	/* Let the utility progress loop drain pending work before destroying its context. */
	crtu_progress_stop();
	return 0;
}

static struct crt_proto_rpc_format rpc_reconnect_rpc_fmt[] = {
    {
	.prf_flags   = 0,
	.prf_req_fmt = &CQF_RPC_RECONNECT_PING,
	.prf_hdlr    = (void *)rpc_reconnect_ping_handler,
	.prf_co_ops  = NULL,
    },
    {
	.prf_flags   = 0,
	.prf_req_fmt = &CQF_RPC_RECONNECT_SHUTDOWN,
	.prf_hdlr    = (void *)rpc_reconnect_shutdown_handler,
	.prf_co_ops  = NULL,
    },
};

static struct crt_proto_format rpc_reconnect_proto_fmt = {
    .cpf_name  = "rpc-reconnect-test",
    .cpf_ver   = RPC_RECONNECT_VER,
    .cpf_count = ARRAY_SIZE(rpc_reconnect_rpc_fmt),
    .cpf_prf   = rpc_reconnect_rpc_fmt,
    .cpf_base  = RPC_RECONNECT_BASE,
};

#endif /* __TEST_RPC_RECONNECT_COMMON_H__ */
