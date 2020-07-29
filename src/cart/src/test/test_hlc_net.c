/*
 * (C) Copyright 2019-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <cart/api.h>
#include <cart/types.h>
#include <gurt/common.h>

/* CRT internal opcode definitions, must be 0xFF00xxxx.*/
#define CRT_OPC_TEST_PROTO (0x10000000)

#define DEBUG		1
#define MAX_SEQ		1000

#define CRT_ISEQ_RPC_TEST	/* input fields */		 \
	((uint64_t)		(seq)			CRT_VAR) \
	((uint64_t)		(hlc)			CRT_VAR) \
	((uint32_t)		(src)			CRT_VAR) \
	((uint32_t)		(dst)			CRT_VAR)

#define CRT_OSEQ_RPC_TEST	/* output fields */		 \
	((uint64_t)		(seq)			CRT_VAR) \
	((uint64_t)		(hlc)			CRT_VAR) \
	((uint32_t)		(src)			CRT_VAR) \
	((uint32_t)		(dst)			CRT_VAR)

CRT_RPC_DECLARE(crt_rpc_test, CRT_ISEQ_RPC_TEST, CRT_OSEQ_RPC_TEST)
CRT_RPC_DEFINE(crt_rpc_test, CRT_ISEQ_RPC_TEST, CRT_OSEQ_RPC_TEST)

static void test_srv_cb(crt_rpc_t *rpc);

static struct crt_proto_rpc_format test_proto_rpc_fmt[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_rpc_test,
		.prf_hdlr	= test_srv_cb,
		.prf_co_ops	= NULL,
	}
};

static struct crt_proto_format test_proto_fmt = {
	.cpf_name	= "test-proto",
	.cpf_ver	= 0,
	.cpf_count	= ARRAY_SIZE(test_proto_rpc_fmt),
	.cpf_prf	= &test_proto_rpc_fmt[0],
	.cpf_base	= CRT_OPC_TEST_PROTO,
};

struct global_srv {
	crt_context_t		 crt_ctx;
	pthread_t		 progress_thid;
	uint64_t		 seq;
	uint32_t		 my_rank;
	uint32_t		 grp_size;
	uint32_t		 shutdown;
};

#if DEBUG == 1
#define dbg(fmt, ...)	D_DEBUG(DB_TEST, fmt, ##__VA_ARGS__)
#else
#define dbg(fmt, ...)							\
	printf("%s[%d]\t[%d]\t"fmt"\n",					\
	(strrchr(__FILE__, '/')+1), __LINE__, getpid(), ##__VA_ARGS__)
#endif

/*========================== GLOBAL ===========================*/

static struct global_srv global_srv;

/*========================== GLOBAL ===========================*/

static void test_srv_cb(crt_rpc_t *rpc)
{
	struct crt_rpc_test_in	*rpc_input;
	struct crt_rpc_test_out	*rpc_output;
	uint64_t		 hlc = crt_hlc_get();
	int			 rc;

	rpc_input = crt_req_get(rpc);
	D_ASSERT(rpc_input != NULL);

	rpc_output = crt_reply_get(rpc);
	D_ASSERT(rpc_output != NULL);

	/* send HLC should be early than receive HLC */
	D_ASSERT(rpc_input->hlc < hlc);

	rpc_output->seq = rpc_input->seq;
	rpc_output->hlc = hlc;
	rpc_output->src = rpc_input->src;
	rpc_output->dst = rpc_input->dst;

	dbg("HLC=0x%lx recv RPC %02u.%03lu %02u send HLC=0x%lx\n", hlc,
	    rpc_input->src, rpc_input->seq, rpc_input->dst, rpc_input->hlc);

	rc = crt_reply_send(rpc);
	D_ASSERTF(rc == 0, "crt_reply_send failed %d\n", rc);
}

static void test_cli_cb(const struct crt_cb_info *cb_info)
{
	struct crt_rpc_test_in	*rpc_input;
	struct crt_rpc_test_out	*rpc_output;
	crt_rpc_t		*rpc = cb_info->cci_rpc;
	uint64_t		 hlc = crt_hlc_get();

	dbg("opc: %#x cci_rc: %d", rpc->cr_opc, cb_info->cci_rc);

	if (cb_info->cci_rc == 0) {
		rpc_input = crt_req_get(rpc);
		D_ASSERT(rpc_input != NULL);

		rpc_output = crt_reply_get(rpc);
		D_ASSERT(rpc_output != NULL);

		dbg("HLC=0x%lx send RPC %02u.%03lu %02u repl HLC=0x%lx "
		    "current HLC=0x%lx\n", rpc_input->hlc,
		    rpc_input->src, rpc_input->seq, rpc_input->dst,
		    rpc_output->hlc, hlc);

		D_ASSERT(rpc_output->seq == rpc_input->seq);
		D_ASSERT(rpc_output->src == rpc_input->src);
		D_ASSERT(rpc_output->dst == rpc_input->dst);

		D_ASSERT(rpc_input->hlc < rpc_output->hlc);
		D_ASSERT(rpc_output->hlc < hlc);
	}
}

static int test_send_rpc(d_rank_t to)
{
	struct crt_rpc_test_in *rpc_input;
	crt_rpc_t *rpc;
	crt_endpoint_t ep;
	crt_opcode_t opc;
	int rc = 0;

	dbg("---%s--->", __func__);

	if (global_srv.seq >= MAX_SEQ)
		return -DER_SHUTDOWN;

	ep.ep_grp  = NULL;
	ep.ep_rank = to;
	ep.ep_tag  = 0;

	/* get the opcode of the first RPC in version 0 of OPC_SWIM_PROTO */
	opc = CRT_PROTO_OPC(CRT_OPC_TEST_PROTO, 0, 0);
	rc = crt_req_create(global_srv.crt_ctx, &ep, opc, &rpc);
	D_ASSERTF(rc == 0, "crt_req_create() failed rc=%d", rc);

	rpc_input = crt_req_get(rpc);
	D_ASSERT(rpc_input != NULL);
	rpc_input->seq = global_srv.seq++;
	rpc_input->hlc = crt_hlc_get();
	rpc_input->src = global_srv.my_rank;
	rpc_input->dst = to;

	rc = crt_req_send(rpc, test_cli_cb, NULL);
	D_ASSERTF(rc == 0, "crt_req_send() failed rc=%d", rc);

	dbg("<---%s---", __func__);
	return rc;
}

static void *srv_progress(void *data)
{
	crt_context_t *ctx = (crt_context_t *)data;
	int rc = 0;

	dbg("---%s--->", __func__);

	D_ASSERTF(ctx != NULL, "ctx=%p\n", ctx);

	while (global_srv.shutdown == 0) {
		rc = crt_progress(*ctx, 1000);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("crt_progress() failed rc=%d\n", rc);
			break;
		}
	}

	dbg("<---%s---", __func__);
	return NULL;
}

static void srv_fini(void)
{
	int rc = 0;

	dbg("---%s--->", __func__);

	global_srv.shutdown = 1;
	dbg("main thread wait progress thread...");

	if (global_srv.progress_thid)
		pthread_join(global_srv.progress_thid, NULL);

	rc = crt_context_destroy(global_srv.crt_ctx, true);
	D_ASSERTF(rc == 0, "crt_context_destroy failed rc=%d\n", rc);

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize failed rc=%d\n", rc);

	dbg("<---%s---", __func__);
}

static int srv_init(void)
{
	int rc = 0;

	dbg("---%s--->", __func__);

	rc = crt_init(CRT_DEFAULT_GRPID, CRT_FLAG_BIT_SERVER);
	D_ASSERTF(rc == 0, " crt_init failed %d\n", rc);

	rc = crt_proto_register(&test_proto_fmt);
	D_ASSERT(rc == 0);

	rc = crt_group_rank(NULL, &global_srv.my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank failed %d\n", rc);

	rc = crt_group_size(NULL, &global_srv.grp_size);
	D_ASSERTF(rc == 0, "crt_group_size failed %d\n", rc);

	rc = crt_context_create(&global_srv.crt_ctx);
	D_ASSERTF(rc == 0, "crt_context_create() failed %d\n", rc);

	/* create progress thread */
	rc = pthread_create(&global_srv.progress_thid, NULL,
			    srv_progress, &global_srv.crt_ctx);
	if (rc != 0)
		D_ERROR("progress thread creating failed, rc=%d\n", rc);

	dbg("my_rank=%u, group_size=%u srv_pid=%d",
	    global_srv.my_rank, global_srv.grp_size, getpid());

	dbg("<---%s---", __func__);
	return rc;
}

int main(int argc, char *argv[])
{
	int i, rc = 0;

	dbg("---%s--->", __func__);

	/* default value */

	srv_init();

	/* print the state of all members from all targets */
	while (!global_srv.shutdown) {
		for (i = 0; i < global_srv.grp_size; i++) {
			if (i != global_srv.my_rank) {
				rc = test_send_rpc(i);
				if (rc)
					break;
			}
		}
		if (rc)
			break;
		sched_yield();
	}

	sleep(5); /* wait until other threads complete */
	srv_fini();

	dbg("<---%s---", __func__);
	return 0;
}
