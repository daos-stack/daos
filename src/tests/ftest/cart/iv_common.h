/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This is a common file for IV client and IV server
 */
#include <unistd.h>
#include <cart/api.h>
#include <cart/iv.h>

#include "tests_common.h"

/* Describes internal structure of the value */
#define MAX_DATA_SIZE 1024

#define IV_GRP_NAME "IV_TEST"

#define TEST_IV_BASE 0x010000000
#define TEST_IV_VER  0

/* Describes internal structure of a key */
struct iv_key_struct {
	d_rank_t	rank;
	uint32_t	key_id;
};

#define CRT_ISEQ_RPC_TEST_FETCH_IV /* input fields */		 \
	((d_iov_t)		(key)			CRT_VAR) \
	((crt_bulk_t)		(bulk_hdl)		CRT_VAR)

#define CRT_OSEQ_RPC_TEST_FETCH_IV /* output fields */		 \
	((d_iov_t)		(key)			CRT_VAR) \
	((uint64_t)		(size)			CRT_VAR) \
	((int64_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_TEST_UPDATE_IV /* input fields */		 \
	((d_iov_t)		(iov_key)		CRT_VAR) \
	((d_iov_t)		(iov_sync)		CRT_VAR) \
	((d_iov_t)		(iov_value)		CRT_VAR)

#define CRT_OSEQ_RPC_TEST_UPDATE_IV /* output fields */		\
	((int64_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_TEST_INVALIDATE_IV /* input fields */	 \
	((d_iov_t)		(iov_key)		CRT_VAR) \
	((d_iov_t)		(iov_sync)		CRT_VAR)

#define CRT_OSEQ_RPC_TEST_INVALIDATE_IV /* output fields */	 \
	((int32_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_SET_IVNS		/* input fields */	 \
	((uint32_t)		(unused)		CRT_VAR)

#define CRT_OSEQ_RPC_SET_IVNS		/* output fields */	 \
	((uint32_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_SHUTDOWN		/* input fields */	 \
	((uint32_t)		(unused)		CRT_VAR)

#define CRT_OSEQ_RPC_SHUTDOWN		/* output fields */	 \
	((uint32_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_SET_GRP_VERSION	/* input fields */	 \
	((uint32_t)		(version)		CRT_VAR) \
	((uint32_t)		(timing)		CRT_VAR)

#define CRT_OSEQ_RPC_SET_GRP_VERSION	/* output fields */	\
	((int32_t)		(rc)			CRT_VAR)

#define CRT_ISEQ_RPC_GET_GRP_VERSION	/* input fields */	 \
	((uint32_t)		(unused)		CRT_VAR)

#define CRT_OSEQ_RPC_GET_GRP_VERSION	/* output fields */	 \
	((uint32_t)		(version)		CRT_VAR) \
	((int32_t)		(rc)			CRT_VAR)

#ifdef _SERVER
#define RPC_REGISTER(name) \
	CRT_RPC_SRV_REGISTER(name, 0, name, DQF_FUNC_##name)
#else
#define RPC_REGISTER(name) \
	CRT_RPC_REGISTER(name, 0, name)
#endif

#define RPC_DECLARE(name, function)					\
	CRT_RPC_DECLARE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)		\
	CRT_RPC_DEFINE(name, CRT_ISEQ_##name, CRT_OSEQ_##name)

enum {
	/* Client issues fetch call */
	RPC_TEST_FETCH_IV = CRT_PROTO_OPC(TEST_IV_BASE, TEST_IV_VER, 0),
	 /* Client issues update call */
	RPC_TEST_UPDATE_IV = CRT_PROTO_OPC(TEST_IV_BASE, TEST_IV_VER, 1),
	/* Client issues invalidate call */
	RPC_TEST_INVALIDATE_IV = CRT_PROTO_OPC(TEST_IV_BASE,
					       TEST_IV_VER, 2),
	/* Send global ivns */
	RPC_SET_IVNS = CRT_PROTO_OPC(TEST_IV_BASE, TEST_IV_VER, 3),
	/* Request server shutdown */
	RPC_SHUTDOWN = CRT_PROTO_OPC(TEST_IV_BASE, TEST_IV_VER, 4),
	/* Change group version */
	RPC_SET_GRP_VERSION = CRT_PROTO_OPC(TEST_IV_BASE, TEST_IV_VER, 5),
	/* Get group version */
	RPC_GET_GRP_VERSION = CRT_PROTO_OPC(TEST_IV_BASE, TEST_IV_VER, 6),

} rpc_id_t;

int iv_test_fetch_iv(crt_rpc_t *rpc);
int iv_test_update_iv(crt_rpc_t *rpc);
int iv_test_invalidate_iv(crt_rpc_t *rpc);

int iv_set_ivns(crt_rpc_t *rpc);
int iv_shutdown(crt_rpc_t *rpc);
int iv_set_grp_version(crt_rpc_t *rpc);
int iv_get_grp_version(crt_rpc_t *rpc);

RPC_DECLARE(RPC_TEST_FETCH_IV, iv_test_fetch_iv);
RPC_DECLARE(RPC_TEST_UPDATE_IV, iv_test_update_iv);
RPC_DECLARE(RPC_TEST_INVALIDATE_IV, iv_test_invalidate_iv);
RPC_DECLARE(RPC_SET_IVNS, iv_set_ivns);
RPC_DECLARE(RPC_SHUTDOWN, iv_shutdown);
RPC_DECLARE(RPC_SET_GRP_VERSION, iv_set_grp_version);
RPC_DECLARE(RPC_GET_GRP_VERSION, iv_get_grp_version);

#ifdef _SERVER
#define PRF_ENTRY(x, y)			\
{					\
	.prf_flags = 0,			\
	.prf_req_fmt = &x,		\
	.prf_hdlr = (void *)y,		\
	.prf_co_ops = NULL,		\
}
#else
#define PRF_ENTRY(x, y)			\
{					\
	.prf_flags = 0,			\
	.prf_req_fmt = &x,		\
	.prf_hdlr = NULL,		\
	.prf_co_ops = NULL,		\
}

#endif

static struct crt_proto_rpc_format my_proto_rpc_fmt_iv[] = {
	PRF_ENTRY(CQF_RPC_TEST_FETCH_IV, iv_test_fetch_iv),
	PRF_ENTRY(CQF_RPC_TEST_UPDATE_IV, iv_test_update_iv),
	PRF_ENTRY(CQF_RPC_TEST_INVALIDATE_IV, iv_test_invalidate_iv),
	PRF_ENTRY(CQF_RPC_SET_IVNS, iv_set_ivns),
	PRF_ENTRY(CQF_RPC_SHUTDOWN, iv_shutdown),
	PRF_ENTRY(CQF_RPC_SET_GRP_VERSION, iv_set_grp_version),
	PRF_ENTRY(CQF_RPC_GET_GRP_VERSION, iv_get_grp_version),
};

static struct crt_proto_format my_proto_fmt_iv = {
	.cpf_name = "my-proto-iv",
	.cpf_ver = TEST_IV_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_iv),
	.cpf_prf = &my_proto_rpc_fmt_iv[0],
	.cpf_base = TEST_IV_BASE,
};

struct rpc_response_t {
	sem_t	sem;
	int	rc;
};

void
rpc_handle_reply(const struct crt_cb_info *info)
{
	struct rpc_response_t	*resp = NULL;
	crt_rpc_t		*rpc_req = NULL;

	resp = (struct rpc_response_t *)info->cci_arg;

	rpc_req = info->cci_rpc;
	crt_req_addref(rpc_req);

	resp->rc = info->cci_rc;
	sem_post(&resp->sem);
}

int prepare_rpc_request(crt_context_t crt_ctx, int rpc_id,
			crt_endpoint_t *server_ep, void **input,
			crt_rpc_t **rpc_req)
{
	int rc;

	rc = crt_req_create(crt_ctx, server_ep, rpc_id, rpc_req);
	assert(rc == 0);

	*input = crt_req_get(*rpc_req);

	return rc;
}

int send_rpc_request(crt_context_t crt_ctx, crt_rpc_t *rpc_req, void **output)
{
	struct rpc_response_t	resp;
	int			rc;

	rc = sem_init(&resp.sem, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed\n");

	rc = crt_req_send(rpc_req, rpc_handle_reply, &resp);
	assert(rc == 0);

	tc_sem_timedwait(&resp.sem, 30, __LINE__);

	D_ASSERTF(resp.rc == 0, "rpc send failed: %d\n", resp.rc);
	*output = crt_reply_get(rpc_req);
	return resp.rc;
}

/** Prints a buffer as hex to a file without any newlines/spaces/etc */
static inline void
print_hex(void *buf, size_t len, FILE *log_file)
{
	uint8_t *bytes = (uint8_t *)buf;

	if (bytes == NULL)
		return;

	for (; len > 0; len--) {
		fprintf(log_file, "%02X", *bytes);
		bytes++;
	}
}
