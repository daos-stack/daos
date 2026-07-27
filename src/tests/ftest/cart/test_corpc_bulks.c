/*
 * (C) Copyright 2018-2022 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * CORPC test with bulk transfer. Assumes 5 instances are running
 *
 * Rank 0 creates a source bulk handle backed by a static buffer and
 * sends a CORPC to all ranks, including self. By default bulk is sent
 * as an input param to the CORPC. Each rank then performs BULK_GET
 * transfer and verifies resultant contents.
 *
 * If -i (inline) option is passed, bulk is passed inline during CORPC
 * creation, and is accessed via crt_bulk_access() by all ranks, bypassing
 * manual bulk transfer.
 *
 */

#include <assert.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <cart/api.h>
#include "crt_utils.h"
#include "test_corpc_bulks.h"

#define TEST_CORPC_BULKS_BASE   0x010000000
#define TEST_CORPC_BULKS_VER    0
#define TEST_CORPC_BULK_SIZE    (20 * 1024)
#define TEST_CORPC_BULK_PATTERN 0xae

static bool       g_corpc_hdlr_called;
static bool       g_inline_bulk;
static d_rank_t   g_my_rank;
static crt_bulk_t g_source_bulk = CRT_BULK_NULL;
static uint8_t    g_source_buf[TEST_CORPC_BULK_SIZE];

enum {
	TEST_OPC_BULK_TEST = CRT_PROTO_OPC(TEST_CORPC_BULKS_BASE, TEST_CORPC_BULKS_VER, 0),
	TEST_OPC_SHUTDOWN
} test_corpc_bulks_opc_t;

/* clang-format off */
#define CRT_ISEQ_BULK_TEST		/* input fields */ \
	((crt_bulk_t)		(bulk_hdl)	CRT_VAR) \
	((uint64_t)		(bulk_size)	CRT_VAR)

#define CRT_OSEQ_BULK_TEST		/* output fields */ \
	((uint32_t)		(unused)	CRT_VAR)

#define CRT_ISEQ_SHUTDOWN		/* input fields */ \
	((uint32_t)		(unused)	CRT_VAR)

#define CRT_OSEQ_SHUTDOWN		/* output fields */ \
	((uint32_t)		(unused)	CRT_VAR)
/* clang-format on */

#define TEST_CORPC_BULKS_RPC(name, in_seq, out_seq)                                                \
	CRT_RPC_DECLARE(name, in_seq, out_seq)                                                     \
	CRT_RPC_DEFINE(name, in_seq, out_seq)

TEST_CORPC_BULKS_RPC(bulk_test, CRT_ISEQ_BULK_TEST, CRT_OSEQ_BULK_TEST);
TEST_CORPC_BULKS_RPC(shutdown, CRT_ISEQ_SHUTDOWN, CRT_OSEQ_SHUTDOWN);

static int
corpc_aggregate(crt_rpc_t *src, crt_rpc_t *result, void *priv)
{
	return 0;
}

struct crt_corpc_ops corpc_bulk_ops = {
    .co_aggregate = corpc_aggregate,
};

static void
__error_exit(int line, const char *fn)
{
	D_ERROR("Failed in %s on line %d\n", fn, line);
	assert(0);
}

#define error_exit() __error_exit(__LINE__, __func__);

static void
verify_bulk_pattern(const uint8_t *buf, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (buf[i] != TEST_CORPC_BULK_PATTERN) {
			D_ERROR("bulk data mismatch at %zu: expected %#x got %#x\n", i,
				TEST_CORPC_BULK_PATTERN, buf[i]);
			error_exit();
		}
	}
}

static void
verify_implicit_bulk(crt_bulk_t bulk_hdl, uint64_t expected_size)
{
	d_sg_list_t sgl  = {0};
	d_iov_t    *iovs = NULL;
	uint32_t    seg_num;
	size_t      total_size = 0;
	uint32_t    i;
	int         rc;

	rc = crt_bulk_access(bulk_hdl, &sgl);
	if (rc != -DER_TRUNC) {
		DL_ERROR(rc, "crt_bulk_access() probe failed\n");
		error_exit();
	}

	seg_num = sgl.sg_nr_out;
	if (seg_num == 0) {
		D_ERROR("implicit bulk has no segments\n");
		error_exit();
	}

	D_ALLOC_ARRAY(iovs, seg_num);
	if (iovs == NULL)
		error_exit();

	sgl.sg_nr   = seg_num;
	sgl.sg_iovs = iovs;
	rc          = crt_bulk_access(bulk_hdl, &sgl);
	if (rc != 0) {
		DL_ERROR(rc, "crt_bulk_access() read failed\n");
		D_FREE(iovs);
		error_exit();
	}

	for (i = 0; i < seg_num; i++) {
		D_ASSERTF(iovs[i].iov_buf != NULL, "implicit bulk segment %u is NULL\n", i);
		verify_bulk_pattern(iovs[i].iov_buf, iovs[i].iov_len);
		total_size += iovs[i].iov_len;
	}

	D_FREE(iovs);
	D_ASSERTF(total_size == expected_size,
		  "implicit bulk size mismatch: expected=" DF_U64 " got=%zu\n", expected_size,
		  total_size);
}

static int
bulk_transfer_done_cb(const struct crt_bulk_cb_info *info)
{
	uint8_t *dst_buf;
	int      rc;

	if (info == NULL || info->bci_bulk_desc == NULL) {
		D_ERROR("bulk completion info is invalid\n");
		error_exit();
	}

	if (info->bci_rc != 0) {
		DL_ERROR(info->bci_rc, "Bulk transfer failed\n");
		error_exit();
	}

	dst_buf = info->bci_arg;
	verify_bulk_pattern(dst_buf, info->bci_bulk_desc->bd_len);

	rc = crt_reply_send(info->bci_bulk_desc->bd_rpc);
	if (rc != 0) {
		DL_ERROR(rc, "Failed to send bulk reply\n");
		error_exit();
	}

	crt_bulk_free(info->bci_bulk_desc->bd_local_hdl);
	D_FREE(dst_buf);
	RPC_PUB_DECREF(info->bci_bulk_desc->bd_rpc);

	return 0;
}

static void
corpc_hdlr(crt_rpc_t *rpc)
{
	struct bulk_test_in *input;
	crt_bulk_t           remote_bulk;
	int                  rc;

	DBG_PRINT("corpc handler called on rank %d\n", g_my_rank);
	g_corpc_hdlr_called = true;

	input = crt_req_get(rpc);
	D_ASSERTF(input != NULL, "bulk corpc input is NULL\n");
	D_ASSERTF(input->bulk_size == TEST_CORPC_BULK_SIZE, "unexpected bulk size=" DF_U64 "\n",
		  input->bulk_size);

	remote_bulk = g_inline_bulk ? rpc->cr_co_bulk_hdl : input->bulk_hdl;
	D_ASSERTF(remote_bulk != CRT_BULK_NULL, "bulk handle is not set\n");

	if (g_inline_bulk) {
		verify_implicit_bulk(remote_bulk, input->bulk_size);
		rc = crt_reply_send(rpc);
		D_ASSERTF(rc == 0, "implicit bulk reply failed\n");
		return;
	}

	/* Issue a bulk transfer to get the data */
	{
		struct crt_bulk_desc bulk_desc;
		crt_bulk_t           dst_bulk;
		d_sg_list_t          sgl;
		uint8_t             *dst_buf;

		D_ALLOC_ARRAY(dst_buf, input->bulk_size);
		if (dst_buf == NULL)
			error_exit();

		rc = d_sgl_init(&sgl, 1);
		if (rc != 0)
			error_exit();

		sgl.sg_iovs[0].iov_buf     = dst_buf;
		sgl.sg_iovs[0].iov_buf_len = input->bulk_size;
		sgl.sg_iovs[0].iov_len     = input->bulk_size;

		rc = crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RW, &dst_bulk);
		if (rc != 0)
			error_exit();

		RPC_PUB_ADDREF(rpc);
		bulk_desc.bd_rpc        = rpc;
		bulk_desc.bd_bulk_op    = CRT_BULK_GET;
		bulk_desc.bd_remote_hdl = remote_bulk;
		bulk_desc.bd_remote_off = 0;
		bulk_desc.bd_local_hdl  = dst_bulk;
		bulk_desc.bd_local_off  = 0;
		bulk_desc.bd_len        = input->bulk_size;

		rc = crt_bulk_transfer(&bulk_desc, bulk_transfer_done_cb, dst_buf, NULL);
		if (rc != 0) {
			DL_ERROR(rc, "bulk transfer failed\n");
			error_exit();
		}
	}
}

static void
shutdown_hdlr(crt_rpc_t *rpc)
{
	int rc;

	DBG_PRINT("shutdown handler called\n");

	rc = crt_reply_send(rpc);
	D_ASSERT(rc == 0);

	crtu_progress_stop();
}

static void
corpc_response_hdlr(const struct crt_cb_info *info)
{
	sem_t *sem;

	D_ASSERTF(info != NULL, "cb_info is null\n");
	D_ASSERTF(info->cci_rc == 0, "CORPC completed with an error\n");

	sem = (sem_t *)info->cci_arg;
	sem_post(sem);
}

static void
shutdown_resp_hdlr(const struct crt_cb_info *info)
{
	sem_t *sem;

	D_ASSERTF(info != NULL, "cb_info is null\n");
	D_ASSERTF(info->cci_rc == 0, "Shutdown RPC completed with an error\n");

	sem = (sem_t *)info->cci_arg;
	sem_post(sem);
}

static struct crt_proto_rpc_format proto_rpc_fmt[] = {{
							  .prf_flags   = 0,
							  .prf_req_fmt = &CQF_bulk_test,
							  .prf_hdlr    = corpc_hdlr,
							  .prf_co_ops  = &corpc_bulk_ops,
						      },
						      {
							  .prf_flags   = 0,
							  .prf_req_fmt = &CQF_shutdown,
							  .prf_hdlr    = shutdown_hdlr,
							  .prf_co_ops  = NULL,
						      }};

static struct crt_proto_format     my_proto = {
	.cpf_name  = "my-proto-corpc-bulks",
	.cpf_ver   = TEST_CORPC_BULKS_VER,
	.cpf_count = ARRAY_SIZE(proto_rpc_fmt),
	.cpf_prf   = &proto_rpc_fmt[0],
	.cpf_base  = TEST_CORPC_BULKS_BASE,
};

static void
show_usage(const char *prog)
{
	printf("Usage: %s [-b]\n", prog);
	printf("Options:\n");
	printf("-i: Use inline bulk for CORPC\n");
}

static int
parse_args(int argc, char **argv)
{
	int c;

	g_inline_bulk = false;

	while ((c = getopt(argc, argv, "i")) != -1) {
		switch (c) {
		case 'i':
			g_inline_bulk = true;
			break;
		default:
			show_usage(argv[0]);
			return -1;
		}
	}

	if (optind < argc) {
		show_usage(argv[0]);
		return -1;
	}

	return 0;
}

static void
init_source_bulk(crt_context_t ctx)
{
	d_sg_list_t sgl;
	int         rc;

	memset(g_source_buf, TEST_CORPC_BULK_PATTERN, sizeof(g_source_buf));

	rc = d_sgl_init(&sgl, 1);
	D_ASSERTF(rc == 0, "d_sgl_init() failed; rc=%d\n", rc);

	sgl.sg_iovs[0].iov_buf     = g_source_buf;
	sgl.sg_iovs[0].iov_buf_len = sizeof(g_source_buf);
	sgl.sg_iovs[0].iov_len     = sizeof(g_source_buf);

	rc = crt_bulk_create(ctx, &sgl, CRT_BULK_RO, &g_source_bulk);
	D_ASSERTF(rc == 0, "crt_bulk_create() for source bulk failed; rc=%d\n", rc);
}

int
main(int argc, char **argv)
{
	int                  rc;
	crt_context_t        ctx;
	d_rank_list_t       *rank_list;
	crt_rpc_t           *rpc;
	uint32_t             grp_size;
	crt_group_t         *grp;
	char                *env_self_rank;
	char                *grp_cfg_file;
	pthread_t            progress_thread;
	sem_t                sem;
	crt_endpoint_t       server_ep;
	int                  i;
	static d_rank_t      my_rank;
	struct bulk_test_in *input;

	rc = parse_args(argc, argv);
	if (rc != 0)
		return rc;

	/* get self rank from the env that crt_launch prepares */
	d_agetenv_str(&env_self_rank, "CRT_L_RANK");
	my_rank   = atoi(env_self_rank);
	g_my_rank = my_rank;
	d_freeenv_str(&env_self_rank);

	rc = sem_init(&sem, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	/* rank, num_attach_retries, is_server, D_ASSERT_on_error */
	crtu_test_init(my_rank, 20, true, true);
	crtu_set_shutdown_delay(0);

	rc = d_log_init();
	D_ASSERT(rc == 0);

	rc = crt_init(NULL, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	D_ASSERTF(rc == 0, "crt_init() failed\n");

	rc = crt_proto_register(&my_proto);
	D_ASSERTF(rc == 0, "crt_proto_register() failed\n");

	rc = crt_context_create(&ctx);
	D_ASSERTF(rc == 0, "crt_context_create() failed\n");

	d_agetenv_str(&grp_cfg_file, "CRT_L_GRP_CFG");

	rc = crt_rank_self_set(my_rank, 1 /* group_version_min */);
	D_ASSERTF(rc == 0, "crt_rank_self_set(%d) failed\n", my_rank);

	grp = crt_group_lookup(NULL);
	D_ASSERTF(grp != NULL, "Failed to lookup group\n");

	/* load group info from a config file and delete file upon return */
	rc = crtu_load_group_from_file(grp_cfg_file, ctx, grp, my_rank, true);
	d_freeenv_str(&grp_cfg_file);
	D_ASSERTF(rc == 0, "crtu_load_group_from_file() failed; rc=%d\n", rc);

	/* test requires 5 ranks */
	rc = crt_group_size(grp, &grp_size);
	D_ASSERTF(rc == 0, "crt_group_size() failed\n");
	D_ASSERTF(grp_size == 5, "This test requires 5 ranks\n");

	rc = crt_group_ranks_get(grp, &rank_list);
	D_ASSERTF(rc == 0, "crt_group_ranks_get() failed; rc=%d\n", rc);

	rc = pthread_create(&progress_thread, 0, crtu_progress_fn, &ctx);
	D_ASSERTF(rc == 0, "pthread_create() failed; rc=%d\n", rc);

	if (my_rank == 0)
		init_source_bulk(ctx);

	/* rank=0 is initiator of the test, the rest of ranks wait for rpcs */
	if (my_rank != 0)
		D_GOTO(wait_for_rpcs, 0);

	/* Wait for all ranks to come up, 5 seconds per ping, 100 seconds max */
	rc = crtu_wait_for_ranks(ctx, grp, rank_list, 0, 1, 5, 100.0);
	D_ASSERTF(rc == 0, "wait_for_ranks() failed; rc=%d\n", rc);

	d_rank_list_free(rank_list);
	rank_list = NULL;

	rc = crt_corpc_req_create(ctx, grp, NULL, TEST_OPC_BULK_TEST,
				  g_inline_bulk ? g_source_bulk : CRT_BULK_NULL, NULL, 0,
				  crt_tree_topo(CRT_TREE_KNOMIAL, 4), &rpc);
	D_ASSERTF(rc == 0, "crt_corpc_req_create() failed\n");

	DBG_PRINT("Sending CORPC with %s bulk\n", g_inline_bulk ? "inline" : "explicit");

	input = crt_req_get(rpc);
	D_ASSERTF(input != NULL, "bulk corpc input is NULL\n");

	input->bulk_hdl  = g_inline_bulk ? CRT_BULK_NULL : g_source_bulk;
	input->bulk_size = TEST_CORPC_BULK_SIZE;

	rc = crt_req_send(rpc, corpc_response_hdlr, &sem);
	D_ASSERT(rc == 0);

	/* wait for corpc completion */
	crtu_sem_timedwait(&sem, 61, __LINE__);

	/* Send shutdown RPCs to all ranks */
	server_ep.ep_grp = NULL;
	server_ep.ep_tag = 0;
	for (i = 1; i < grp_size; i++) {
		server_ep.ep_rank = i;

		rc = crt_req_create(ctx, &server_ep, TEST_OPC_SHUTDOWN, &rpc);
		D_ASSERTF(rc == 0, "crt_req_create() TEST_OPC_SHUTDOWN failed\n");

		rc = crt_req_send(rpc, shutdown_resp_hdlr, &sem);
		D_ASSERTF(rc == 0, "crt_req_send() TEST_OPC_SHUTDOWN failed\n");
		crtu_sem_timedwait(&sem, 61, __LINE__);
	}

	if (g_source_bulk != CRT_BULK_NULL)
		crt_bulk_free(g_source_bulk);

	crtu_progress_stop();

wait_for_rpcs:
	/* Wait until progress thread exits */
	pthread_join(progress_thread, NULL);

	D_ASSERTF(g_corpc_hdlr_called == true, "bulk corpc_handler was not called\n");
	DBG_PRINT("All tests done\n");

	rc = sem_destroy(&sem);
	D_ASSERTF(rc == 0, "sem_destroy() failed\n");

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed\n");

	d_log_fini();
	return 0;
}
