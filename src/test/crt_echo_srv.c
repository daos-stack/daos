/* Copyright (C) 2016 Intel Corporation
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
 * This is a simple example of crt_echo rpc server based on crt APIs.
 */

#include "crt_echo_srv.h"

static int run_echo_srver(void)
{
	crt_endpoint_t		svr_ep;
	crt_rpc_t		*rpc_req = NULL;
	char			*pchar;
	crt_rank_t		myrank;
	uint32_t		mysize;
	int			rc, loop = 0;
	struct crt_echo_checkin_req *e_req;

	rc = crt_group_rank(NULL, &myrank);
	assert(rc == 0);
	rc = crt_group_size(NULL, &mysize);
	assert(rc == 0);

	echo_srv.do_shutdown = 0;

	/* create progress thread */
	rc = pthread_create(&echo_srv.progress_thread, NULL, progress_handler,
			    NULL);
	if (rc != 0) {
		printf("progress thread creating failed, rc: %d.\n", rc);
		goto out;
	}

	/* ============= test-1 ============ */

	/* send checkin RPC */
	svr_ep.ep_grp = NULL;
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	rc = crt_req_create(gecho.crt_ctx, svr_ep, ECHO_OPC_CHECKIN, &rpc_req);
	assert(rc == 0 && rpc_req != NULL);

	C_ALLOC(pchar, 256);
	assert(pchar != NULL);
	snprintf(pchar, 256, "Guest_%d@server-side", myrank);

	e_req = crt_req_get(rpc_req);
	e_req->name = pchar;
	e_req->age = 32;
	e_req->days = myrank;

	C_DEBUG("server(rank %d) sending checkin request, name: %s, age: %d, "
	       "days: %d.\n", myrank, e_req->name, e_req->age, e_req->days);

	gecho.complete = 0;
	rc = crt_req_send(rpc_req, client_cb_common, &gecho.complete);
	assert(rc == 0);
	/* wait completion */
	while (1) {
		if (gecho.complete) {
			printf("server(rank %d) checkin request sent.\n",
			       myrank);
			break;
		}
		usleep(10*1000);
		if (++loop > 1000) {
			printf("wait failed.\n");
			break;
		}
	}
	C_FREE(pchar, 256);

	/* ==================================== */
	/* test group API and bcast RPC */
	crt_group_id_t		grp_id = "example_grp";
	crt_rank_t		grp_ranks[4] = {5, 4, 1, 2};
	crt_rank_list_t	grp_membs;
	crt_rank_t		excluded_ranks[2] = {1, 2};
	crt_rank_list_t	excluded_membs;

	grp_membs.rl_nr.num = 4;
	grp_membs.rl_ranks = grp_ranks;
	excluded_membs.rl_nr.num = 2;
	excluded_membs.rl_ranks = excluded_ranks;

	if (mysize >= 6 && myrank == 4) {
		crt_rpc_t				*corpc_req;
		struct crt_echo_corpc_example_req	*corpc_in;

		rc = crt_group_create(grp_id, &grp_membs, 0, grp_create_cb,
				      &myrank);
		printf("crt_group_create rc: %d, priv %p.\n", rc, &myrank);
		sleep(1); /* just to ensure grp populated */

		rc = crt_corpc_req_create(gecho.crt_ctx, example_grp,
					  &excluded_membs, ECHO_CORPC_EXAMPLE,
					  NULL, NULL, 0, 0, &corpc_req);
		C_ASSERT(rc == 0 && corpc_req != NULL);
		corpc_in = crt_req_get(corpc_req);
		C_ASSERT(corpc_in != NULL);
		corpc_in->co_msg = "testing corpc example from rank 4";

		gecho.complete = 0;
		rc = crt_req_send(corpc_req, client_cb_common,
				  &gecho.complete);
		C_ASSERT(rc == 0);
		sleep(1); /* just to ensure corpc handled */
		C_ASSERT(gecho.complete == 1);

		rc = crt_group_destroy(example_grp, grp_destroy_cb, &myrank);
		printf("crt_group_destroy rc: %d, arg %p.\n", rc, &myrank);
	}

	/* ==================================== */
	printf("main thread wait progress thread ...\n");
	/* wait progress thread */
	rc = pthread_join(echo_srv.progress_thread, NULL);
	if (rc != 0)
		printf("pthread_join failed rc: %d.\n", rc);

out:
	printf("echo_srver shuting down ...\n");
	return rc;
}

int g_roomno = 1082;
int echo_srv_checkin(crt_rpc_t *rpc_req)
{
	struct crt_echo_checkin_req *e_req;
	struct crt_echo_checkin_reply *e_reply;
	int rc = 0;

	/* CaRT internally already allocated the input/output buffer */
	e_req = crt_req_get(rpc_req);
	C_ASSERT(e_req != NULL);

	printf("tier1 echo_srver recv'd checkin, opc: 0x%x.\n",
		rpc_req->dr_opc);
	printf("tier1 checkin input - age: %d, name: %s, days: %d.\n",
		e_req->age, e_req->name, e_req->days);

	e_reply = crt_reply_get(rpc_req);
	C_ASSERT(e_reply != NULL);
	e_reply->ret = 0;
	e_reply->room_no = g_roomno++;

	rc = crt_reply_send(rpc_req);

	printf("tier1 echo_srver sent checkin reply, ret: %d, room_no: %d.\n",
	       e_reply->ret, e_reply->room_no);

	return rc;
}

int echo_srv_shutdown(crt_rpc_t *rpc_req)
{
	int rc = 0;

	printf("tier1 echo_srver received shutdown request, opc: 0x%x.\n",
	       rpc_req->dr_opc);

	assert(rpc_req->dr_input == NULL);
	assert(rpc_req->dr_output == NULL);

	rc = crt_reply_send(rpc_req);
	printf("tier1 echo_srver done issuing shutdown responses.\n");

	echo_srv.do_shutdown = 1;
	printf("tier1 echo_srver set shutdown flag.\n");

	return rc;
}

int main(int argc, char *argv[])
{
	echo_init(1, false);

	run_echo_srver();

	echo_fini();

	return 0;
}
