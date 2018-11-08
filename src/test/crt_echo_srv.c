/* Copyright (C) 2016-2018 Intel Corporation
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

static inline void
echo_sem_timedwait(sem_t *sem, int sec, int line_number)
{
	struct timespec			deadline;
	int				rc;

	rc = clock_gettime(CLOCK_REALTIME, &deadline);
	D_ASSERTF(rc == 0, "clock_gettime() failed at line %d rc: %d\n",
		  line_number, rc);
	deadline.tv_sec += sec;
	rc = sem_timedwait(sem, &deadline);
	D_ASSERTF(rc == 0, "sem_timedwait() failed at line %d rc: %d\n",
		  line_number, rc);
}

static int
echo_check_in(d_rank_t src_rank, d_rank_t dst_rank, uint32_t dst_tag)
{
	crt_rpc_t			*rpc_req = NULL;
	char				*pchar;
	crt_endpoint_t			 svr_ep = {0};
	struct crt_echo_checkin_in	*e_req;
	int				 rc;

	svr_ep.ep_grp = NULL;
	svr_ep.ep_rank = dst_rank;
	svr_ep.ep_tag = dst_tag;
	rc = crt_req_create(gecho.crt_ctx, &svr_ep, ECHO_OPC_CHECKIN, &rpc_req);
	assert(rc == 0 && rpc_req != NULL);

	D_ALLOC(pchar, 256);
	assert(pchar != NULL);
	snprintf(pchar, 256, "Guest_%d@server-side", src_rank);

	e_req = crt_req_get(rpc_req);
	e_req->name = pchar;
	e_req->age = 32;
	e_req->days = src_rank;
	e_req->rank = dst_rank;
	e_req->tag = dst_tag;

	D_DEBUG(DB_TEST, "server(rank %d) sending checkin request, name: %s, "
		"age: %d, days: %d.\n", src_rank, e_req->name, e_req->age,
		e_req->days);

	rc = crt_req_send(rpc_req, client_cb_common, NULL);
	assert(rc == 0);
	/* wait for completion */
	echo_sem_timedwait(&gecho.token_to_proceed, 61, __LINE__);

	D_FREE(pchar);

	return rc;
}

static int run_echo_srver(void)
{
	d_rank_t			 myrank;
	uint32_t			 mysize;
	int				 i;
	int				 j;
	int				 rc;

	rc = crt_group_rank(NULL, &myrank);
	assert(rc == 0);
	rc = crt_group_size(NULL, &mysize);
	assert(rc == 0);

	/* create progress thread */
	rc = pthread_create(&echo_srv.progress_thread, NULL, progress_handler,
			    NULL);
	if (rc != 0) {
		printf("progress thread creating failed, rc: %d.\n", rc);
		goto out;
	}

	/* ============= test-1 ============ */

	/* send checkin RPC to all tags on all ranks in my group */
	for (i = 0; i < mysize; i++) {
		for (j = 0; j < ECHO_EXTRA_CONTEXT_NUM + 1; j++) {
			rc = echo_check_in(myrank, i, j);
			assert(rc == 0);
		}
	}

	/* ==================================== */
	/* test group API and bcast RPC */
	crt_group_id_t		grp_id = "example_grpid";
	d_rank_t		grp_ranks[6] = {5, 7, 4, 1, 2, 6};
	d_rank_list_t	grp_membs;
	d_rank_t		excluded_ranks[4] = {1, 4, 2, 9};
	d_rank_list_t	excluded_membs;

	grp_membs.rl_nr = 6;
	grp_membs.rl_ranks = grp_ranks;
	excluded_membs.rl_nr = 4;
	excluded_membs.rl_ranks = excluded_ranks;

	if (mysize >= 8 && myrank == 4) {
		crt_rpc_t				*corpc_req;
		struct crt_echo_corpc_example_in	*corpc_in;
		uint32_t				 flags;

		rc = crt_group_create(grp_id, &grp_membs, 0, grp_create_cb,
				      &myrank);
		printf("crt_group_create rc: %d, priv %p.\n", rc, &myrank);
		/* make sure grp is populated */
		echo_sem_timedwait(&gecho.token_to_proceed, 61, __LINE__);

		flags = gecho.grp_destroy_piggyback ?
			CRT_RPC_FLAG_GRP_DESTROY : 0;
		rc = crt_corpc_req_create(gecho.crt_ctx, example_grp_hdl,
					  &excluded_membs, ECHO_CORPC_EXAMPLE,
					  NULL, NULL, flags,
					  crt_tree_topo(CRT_TREE_KNOMIAL, 4),
					  &corpc_req);
		D_ASSERT(rc == 0 && corpc_req != NULL);
		corpc_in = crt_req_get(corpc_req);
		D_ASSERT(corpc_in != NULL);
		corpc_in->co_msg = "testing corpc example from rank 4";

		rc = crt_req_send(corpc_req, client_cb_common, NULL);
		D_ASSERT(rc == 0);
		/* make sure corpc has been handled */
		echo_sem_timedwait(&gecho.token_to_proceed, 61, __LINE__);

		if (!gecho.grp_destroy_piggyback) {
			rc = crt_group_destroy(example_grp_hdl, grp_destroy_cb,
					       &myrank);
			printf("crt_group_destroy rc: %d, arg %p.\n",
				rc, &myrank);
		}
	}

	echo_srv.shutdown_by_self = 1;
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

void
echo_srv_checkin(crt_rpc_t *rpc_req)
{
	struct crt_echo_checkin_in	*e_req;
	struct crt_echo_checkin_out	*e_reply;
	char				*raw_buf;
	d_rank_t			 myrank;
	int				 mytag;
	int				 rc;

	rc = crt_group_rank(NULL, &myrank);
	assert(rc == 0);
	rc = crt_context_idx(rpc_req->cr_ctx, &mytag);
	assert(rc == 0);

	/* CaRT internally already allocated the input/output buffer */
	e_req = crt_req_get(rpc_req);
	D_ASSERT(e_req != NULL);

	printf("tier1 echo_srver recv'd checkin, opc: %#x.\n",
	       rpc_req->cr_opc);
	printf("tier1 checkin input - age: %d, name: %s, days: %d.\n",
	       e_req->age, e_req->name, e_req->days);
	if (e_req->raw_package.iov_len != 0) {
		D_ASSERT(e_req->raw_package.iov_buf != NULL);
		raw_buf = e_req->raw_package.iov_buf;
		printf("tier1 checkin, extra message in the raw_package: %s.\n",
		       raw_buf);
	}

	D_ASSERTF(e_req->rank == myrank,
		"rank mismatch, dst_rank %d myrank %d\n", e_req->rank, myrank);
	D_ASSERTF(e_req->tag == mytag,
		"tag mismatch, dst_tag %d mytag %d\n", e_req->tag, mytag);

	e_reply = crt_reply_get(rpc_req);
	D_ASSERT(e_reply != NULL);
	e_reply->ret = 0;
	e_reply->room_no = g_roomno++;
	e_reply->rank = myrank;
	e_reply->tag = mytag;

	crt_reply_send(rpc_req);

	printf("tier1 echo_srver sent checkin reply, ret: %d, room_no: %d.\n",
	       e_reply->ret, e_reply->room_no);
}

void
echo_srv_shutdown(crt_rpc_t *rpc_req)
{
	printf("tier1 echo_srver received shutdown request, opc: %#x.\n",
	       rpc_req->cr_opc);

	assert(rpc_req->cr_input == NULL);
	assert(rpc_req->cr_output == NULL);

	echo_srv.shutdown_by_client = 1;
	printf("tier1 echo_srver set shutdown flag.\n");
}

int main(int argc, char *argv[])
{
	parse_options(argc, argv);

	echo_init(1, false);

	run_echo_srver();

	echo_fini();

	return 0;
}
