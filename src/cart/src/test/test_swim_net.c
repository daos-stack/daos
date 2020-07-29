/*
 * (C) Copyright 2018-2020 Intel Corporation.
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
/**
 * This is a simple example of SWIM implementation on top of CaRT APIs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <cart/api.h>
#include <cart/types.h>
#include <gurt/common.h>
#include <cart/swim.h>

/* CRT internal opcode definitions, must be 0xFF00xxxx.*/
#define CRT_OPC_SWIM_PROTO (0x10000000)

#define DEBUG		1
#define FAILED_MEMBER	1

#define CRT_ISEQ_RPC_SWIM	/* input fields */		 \
	((uint64_t)		     (src)		CRT_VAR) \
	((struct swim_member_update) (upds)		CRT_ARRAY)

#define CRT_OSEQ_RPC_SWIM	/* output fields */

static int
crt_proc_struct_swim_member_update(crt_proc_t proc,
				   struct swim_member_update *data)
{
	int rc;

	rc = crt_proc_memcpy(proc, data, sizeof(*data));
	if (rc != 0)
		return -DER_HG;

	return 0;
}

CRT_RPC_DECLARE(crt_rpc_swim, CRT_ISEQ_RPC_SWIM, CRT_OSEQ_RPC_SWIM)
CRT_RPC_DEFINE(crt_rpc_swim, CRT_ISEQ_RPC_SWIM, CRT_OSEQ_RPC_SWIM)

static void swim_srv_cb(crt_rpc_t *rpc_req);

static struct crt_proto_rpc_format swim_proto_rpc_fmt[] = {
	{
		.prf_flags	= CRT_RPC_FEAT_NO_REPLY,
		.prf_req_fmt	= &CQF_crt_rpc_swim,
		.prf_hdlr	= swim_srv_cb,
		.prf_co_ops	= NULL,
	}
};

static struct crt_proto_format swim_proto_fmt = {
	.cpf_name	= "swim-proto",
	.cpf_ver	= 0,
	.cpf_count	= ARRAY_SIZE(swim_proto_rpc_fmt),
	.cpf_prf	= &swim_proto_rpc_fmt[0],
	.cpf_base	= CRT_OPC_SWIM_PROTO,
};

struct swim_global_srv {
	struct swim_member_state *swim_ms;
	struct swim_context	*swim_ctx;
	crt_context_t		 crt_ctx;
	pthread_t		 progress_thid;
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

static struct swim_global_srv global_srv;

/*========================== GLOBAL ===========================*/

static void swim_srv_cb(crt_rpc_t *rpc_req)
{
	struct crt_rpc_swim_in *rpc_cli_input;
	int rc = 0;

	dbg("---%s--->", __func__);

	rpc_cli_input = crt_req_get(rpc_req);
	D_ASSERT(rpc_cli_input != NULL);

	dbg("receive RPC %u <== %lu", global_srv.my_rank, rpc_cli_input->src);
	if (global_srv.my_rank != FAILED_MEMBER &&
	    rpc_cli_input->src != FAILED_MEMBER) {
		rc = swim_parse_message(global_srv.swim_ctx, rpc_cli_input->src,
					rpc_cli_input->upds.ca_arrays,
					rpc_cli_input->upds.ca_count);
		D_ASSERTF(rc == 0, "swim_parse_rpc() failed rc=%d", rc);
	} else {
		dbg("*** DROP ****");
	}

	dbg("<---%s---", __func__);
}

static void swim_cli_cb(const struct crt_cb_info *cb_info)
{
	struct crt_rpc_swim_in *rpc_swim_input;

	dbg("---%s--->", __func__);

	rpc_swim_input = crt_req_get(cb_info->cci_rpc);
	D_ASSERT(rpc_swim_input != NULL);

	dbg("opc: %#x cci_rc: %d", cb_info->cci_rpc->cr_opc, cb_info->cci_rc);

	D_FREE(rpc_swim_input->upds.ca_arrays);

	dbg("<---%s---", __func__);
}

static int swim_send_message(struct swim_context *ctx, swim_id_t to,
			     struct swim_member_update *upds, size_t nupds)
{
	struct swim_global_srv *srv = swim_data(ctx);
	struct crt_rpc_swim_in *swim_rpc_input;
	crt_rpc_t *rpc_req;
	crt_endpoint_t ep;
	crt_opcode_t opc;
	swim_id_t self = swim_self_get(ctx);
	int rc = 0;

	dbg("---%s--->", __func__);

	dbg("sending RPC %lu ==> %lu", self, to);

	ep.ep_grp  = NULL;
	ep.ep_rank = to;
	ep.ep_tag  = 0;

	/* get the opcode of the first RPC in version 0 of OPC_SWIM_PROTO */
	opc = CRT_PROTO_OPC(CRT_OPC_SWIM_PROTO, 0, 0);
	rc = crt_req_create(srv->crt_ctx, &ep, opc, &rpc_req);
	D_ASSERTF(rc == 0, "crt_req_create() failed rc=%d", rc);

	rc = crt_req_set_timeout(rpc_req, 1);
	D_ASSERTF(rc == 0, "crt_req_set_timeout() failed rc=%d", rc);

	swim_rpc_input = crt_req_get(rpc_req);
	D_ASSERT(swim_rpc_input != NULL);
	swim_rpc_input->src = self;
	swim_rpc_input->upds.ca_arrays = upds;
	swim_rpc_input->upds.ca_count  = nupds;
	rc = crt_req_send(rpc_req, swim_cli_cb, NULL);
	D_ASSERTF(rc == 0, "crt_req_send() failed rc=%d", rc);

	dbg("<---%s---", __func__);
	return rc;
}

static swim_id_t swim_get_dping_target(struct swim_context *ctx)
{
	struct swim_global_srv *srv = swim_data(ctx);
	swim_id_t self = swim_self_get(ctx);
	static swim_id_t id = SWIM_ID_INVALID;
	int count = 0;

	if (id == SWIM_ID_INVALID)
		id = self;
	do {
		if (count++ > srv->grp_size)
			return SWIM_ID_INVALID;
		id = (id + 1) % srv->grp_size;
	} while (id == self || srv->swim_ms[id].sms_status == SWIM_MEMBER_DEAD);

	dbg("dping target: %lu ==> %lu", self, id);

	return id;
}

static swim_id_t swim_get_iping_target(struct swim_context *ctx)
{
	struct swim_global_srv *srv = swim_data(ctx);
	swim_id_t self = swim_self_get(ctx);
	static swim_id_t id = SWIM_ID_INVALID;
	int count = 0;

	if (id == SWIM_ID_INVALID)
		id = self;
	do {
		if (count++ > srv->grp_size)
			return SWIM_ID_INVALID;
		id = (id - 1) % srv->grp_size;
	} while (id == self ||
		 srv->swim_ms[id].sms_status != SWIM_MEMBER_ALIVE);

	dbg("iping target: %lu ==> %lu", self, id);

	return id;
}

static int swim_get_member_state(struct swim_context *ctx,
				 swim_id_t id, struct swim_member_state *state)
{
	struct swim_global_srv *srv = swim_data(ctx);
	int rc = 0;

	*state = srv->swim_ms[id];

	return rc;
}

static int swim_set_member_state(struct swim_context *ctx,
				 swim_id_t id, struct swim_member_state *state)
{
	struct swim_global_srv *srv = swim_data(ctx);
	swim_id_t self = (swim_id_t)srv->my_rank;
	int rc = 0;

	switch (state->sms_status) {
	case SWIM_MEMBER_INACTIVE:
		break;
	case SWIM_MEMBER_ALIVE:
		break;
	case SWIM_MEMBER_SUSPECT:
		fprintf(stderr, "%lu: notify %lu SUSPECT\n", self, id);
		break;
	case SWIM_MEMBER_DEAD:
		fprintf(stderr, "%lu: notify %lu DEAD\n", self, id);
		break;
	default:
		fprintf(stderr, "%lu: notify %lu unknown\n", self, id);
		break;
	}

	srv->swim_ms[id] = *state;
	return rc;
}

static void *srv_progress(void *data)
{
	crt_context_t *ctx = (crt_context_t *)data;
	int rc = 0;

	dbg("---%s--->", __func__);

	D_ASSERTF(ctx != NULL, "ctx=%p\n", ctx);

	while (global_srv.shutdown == 0) {
		rc = crt_progress(*ctx, 1);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("crt_progress() failed rc=%d\n", rc);
			break;
		}
	}

	dbg("<---%s---", __func__);
	return NULL;
}

static void swim_progress_cb(crt_context_t ctx, void *arg)
{
	struct swim_global_srv *srv = arg;
	swim_id_t self_id = swim_self_get(srv->swim_ctx);
	int rc = 0;

	dbg("---%s--->", __func__);

	if (self_id == SWIM_ID_INVALID)
		goto out;

	rc = swim_progress(srv->swim_ctx, 1);
	if (rc == -ESHUTDOWN)
		srv->shutdown = 1;
	else if (rc != -ETIMEDOUT)
		D_ERROR("swim_progress() failed rc=%d\n", rc);
out:
	dbg("<---%s---", __func__);
}

static void srv_fini(void)
{
	int rc = 0;

	dbg("---%s--->", __func__);

	global_srv.shutdown = 1;
	dbg("main thread wait progress thread...");

	if (global_srv.progress_thid)
		pthread_join(global_srv.progress_thid, NULL);

	rc = crt_unregister_progress_cb(swim_progress_cb, 0, &global_srv);
	D_ASSERTF(rc == 0, "crt_unregister_progress_cb() failed %d\n", rc);

	swim_fini(global_srv.swim_ctx);
	free(global_srv.swim_ms);
	global_srv.swim_ctx = NULL;

	rc = crt_context_destroy(global_srv.crt_ctx, 0);
	D_ASSERTF(rc == 0, "crt_context_destroy failed rc=%d\n", rc);

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize failed rc=%d\n", rc);

	dbg("<---%s---", __func__);
}

static struct swim_ops swim_ops = {
	.send_message     = &swim_send_message,
	.get_dping_target = &swim_get_dping_target,
	.get_iping_target = &swim_get_iping_target,
	.get_member_state = &swim_get_member_state,
	.set_member_state = &swim_set_member_state,
};

static int srv_init(void)
{
	uint32_t i, j;
	int rc = 0;

	dbg("---%s--->", __func__);

	rc = crt_init(CRT_DEFAULT_GRPID, CRT_FLAG_BIT_SERVER);
	D_ASSERTF(rc == 0, " crt_init failed %d\n", rc);

	rc = crt_proto_register(&swim_proto_fmt);
	D_ASSERT(rc == 0);

	rc = crt_group_rank(NULL, &global_srv.my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank failed %d\n", rc);

	rc = crt_group_size(NULL, &global_srv.grp_size);
	D_ASSERTF(rc == 0, "crt_group_size failed %d\n", rc);

	for (i = 0; i < global_srv.grp_size; i++) {
		global_srv.swim_ms =
			malloc(global_srv.grp_size *
				sizeof(*global_srv.swim_ms));
		D_ASSERTF(global_srv.swim_ms != NULL,
			  "malloc() failed\n");

		for (j = 0; j < global_srv.grp_size; j++) {
			global_srv.swim_ms[j].sms_incarnation = 0;
			global_srv.swim_ms[j].sms_status = SWIM_MEMBER_ALIVE;
		}

		global_srv.swim_ctx = swim_init(global_srv.my_rank, &swim_ops,
						&global_srv);
		D_ASSERTF(global_srv.swim_ctx != NULL, "swim_init() failed\n");
	}

	rc = crt_register_progress_cb(swim_progress_cb, 0, &global_srv);
	D_ASSERTF(rc == 0, "crt_register_progress_cb() failed %d\n", rc);

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
	enum swim_member_status s;
	int t, j;

	assert(d_log_init() == 0);

	dbg("---%s--->", __func__);

	/* default value */

	srv_init();

	/* print the state of all members from all targets */
	for (t = 0; t < global_srv.grp_size + 2 && !global_srv.shutdown; t++) {
		fprintf(stderr, "%02d. %02u:", t, global_srv.my_rank);
		for (j = 0; j < global_srv.grp_size; j++) {
			s = global_srv.swim_ms[j].sms_status;
			fprintf(stderr, " %c", SWIM_STATUS_CHARS[s]);
		}
		fprintf(stderr, "\n");
		if (global_srv.my_rank == global_srv.grp_size - 1) {
			sleep(1);
			fprintf(stderr, "\n");
			sleep(1);
		} else {
			sleep(3);
		}
	}

	fprintf(stderr, "%02d. %02u: exit\n", t, global_srv.my_rank);

	srv_fini();

	dbg("<---%s---", __func__);
	d_log_fini();
	return 0;
}
