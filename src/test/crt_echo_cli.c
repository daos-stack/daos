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
 * This is a simple example of crt_echo rpc client based on crt APIs.
 */

#include "crt_echo.h"

struct gecho gecho;

static int client_wait(int num_retries, unsigned int wait_len_ms,
		       int *complete_flag)
{
	int retry, rc;

	for (retry = 0; retry < num_retries; retry++) {
		rc = crt_progress(gecho.crt_ctx, wait_len_ms * 1000, NULL,
				  NULL);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("crt_progress failed rc: %d.\n", rc);
			break;
		}
		sched_yield();
		if (*complete_flag)
			return 0;
	}
	return -ETIMEDOUT;
}

struct bulk_test_cli_cbinfo {
	crt_bulk_t	bulk_hdl;
	int		*complete_flag;
};

static void
bulk_test_req_cb(const struct crt_cb_info *cb_info)
{
	struct bulk_test_cli_cbinfo	*bulk_test_cbinfo;
	crt_rpc_t			*rpc_req;
	struct crt_echo_bulk_out	*e_reply;
	int				rc;

	rpc_req = cb_info->cci_rpc;
	bulk_test_cbinfo = cb_info->cci_arg;

	printf("in bulk_test_req_cb, opc: %#x, cci_rc: %d.\n",
	       rpc_req->cr_opc, cb_info->cci_rc);

	e_reply = crt_reply_get(rpc_req);
	printf("bulk_test_output->bulk_echo_msg: %s. ret %d\n",
	       e_reply->echo_msg, e_reply->ret);

	rc = crt_bulk_free(bulk_test_cbinfo->bulk_hdl);
	assert(rc == 0);
	/* set complete flag */
	*(bulk_test_cbinfo->complete_flag) = 1;

	free(bulk_test_cbinfo);
}

static int
echo_client_send_checkin(d_rank_t src_rank, crt_group_t *dst_grp,
			 d_rank_t dst_rank, uint32_t dst_tag)
{
	crt_endpoint_t			 svr_ep = {0};
	char				*raw_buf;
	struct timespec			 t1, t2;
	double				 time_us;
	crt_rpc_t			*rpc_req = NULL;
	char				*pchar;
	struct crt_echo_checkin_in	*e_req;
	int				 rc;

	svr_ep.ep_grp = dst_grp;
	svr_ep.ep_rank = dst_rank;
	svr_ep.ep_tag = dst_tag;

	rc = d_gettime(&t1);
	assert(rc == 0);
	rc = crt_req_create(gecho.crt_ctx, &svr_ep, ECHO_OPC_CHECKIN,
			    &rpc_req);
	assert(rc == 0 && rpc_req != NULL);
	rc = d_gettime(&t2);
	assert(rc == 0);
	time_us = d_time2us(d_timediff(t1, t2));
	printf("time for crt_req_create: %.3e uS.\n", time_us);

	e_req = crt_req_get(rpc_req);
	assert(e_req != NULL);

	D_ALLOC(pchar, 256);
	assert(pchar != NULL);
	snprintf(pchar, 256, "Guest_%d_%d@client-side",
		 src_rank, svr_ep.ep_tag);

	raw_buf = "testing_only ---- data_in_raw_package";
	e_req->name = pchar;
	e_req->age = 32 + svr_ep.ep_tag;
	d_iov_set(&e_req->raw_package, raw_buf, strlen(raw_buf) + 1);
	e_req->days = src_rank;
	e_req->rank = dst_rank;
	e_req->tag = dst_tag;

	D_DEBUG(DB_TEST, "client(rank %d) sending checkin rpc with "
		"tag %d, name: %s, age: %d, days: %d.\n",
		src_rank, svr_ep.ep_tag, e_req->name, e_req->age,
		e_req->days);

	gecho.complete = 0;
	rc = crt_req_send(rpc_req, client_cb_common, &gecho.complete);
	assert(rc == 0);
	/* wait two minutes (in case of manually starting up clients) */
	rc = client_wait(120, 1000, &gecho.complete);
	assert(rc == 0);
	D_FREE(pchar);

	printf("client(rank %d, tag %d) checkin request sent.\n",
	       src_rank, svr_ep.ep_tag);

	return rc;
}

static int
echo_bulk_test(crt_group_t *grp, d_rank_t myrank, bool bulk_bind)
{
	crt_endpoint_t			svr_ep = {0};
	crt_rpc_t			*rpc_req = NULL;
	d_sg_list_t			sgl, sgl_query;
	d_iov_t				*iovs = NULL, iovs_query[2];
	crt_bulk_t			bulk_hdl;
	struct crt_echo_bulk_in		*e_bulk_req;
	struct bulk_test_cli_cbinfo	*bulk_req_cbinfo;
	char				*pchar;
	int				i;
	int				rc;

	svr_ep.ep_grp = grp;
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	rc = crt_req_create(gecho.crt_ctx, &svr_ep, ECHO_OPC_BULK_TEST,
			    &rpc_req);
	assert(rc == 0 && rpc_req != NULL);

	iovs = (d_iov_t *)malloc(2 * sizeof(d_iov_t));
	assert(iovs != NULL);

	iovs[0].iov_buf_len = 4097;
	iovs[0].iov_len = 4097;
	iovs[0].iov_buf = malloc(iovs[0].iov_buf_len);
	assert(iovs[0].iov_buf != NULL);

	pchar = iovs[0].iov_buf;
	for (i = 0; i < iovs[0].iov_buf_len; i++)
		*(pchar++) = i + myrank;
	iovs[1].iov_buf_len = 1*1024*1024 + 11;
	iovs[1].iov_len = 1*1024*1024 + 11;
	iovs[1].iov_buf = malloc(iovs[1].iov_buf_len);
	assert(iovs[1].iov_buf != NULL);

	pchar = iovs[1].iov_buf;
	for (i = 0; i < iovs[1].iov_buf_len; i++)
		*(pchar++) = random();
	sgl.sg_nr = 2;
	sgl.sg_iovs = iovs;

	/* calculate md5 checksum */
	MD5_CTX md5_ctx;
	unsigned char md5[16];
	d_string_t md5_str = (d_string_t)malloc(33);
	assert(md5_str != NULL);
	memset(md5_str, 0, 33);

	rc = MD5_Init(&md5_ctx);
	assert(rc == 1);
	rc = MD5_Update(&md5_ctx, iovs[0].iov_buf, iovs[0].iov_buf_len);
	assert(rc == 1);
	rc = MD5_Update(&md5_ctx, iovs[1].iov_buf, iovs[1].iov_buf_len);
	assert(rc == 1);
	rc = MD5_Final(md5, &md5_ctx);
	assert(rc == 1);
	echo_md5_to_string(md5, md5_str);

	rc = crt_bulk_create(gecho.crt_ctx, &sgl, CRT_BULK_RO, &bulk_hdl);
	assert(rc == 0);

	/* verify result from crt_bulk_access */
	sgl_query.sg_iovs = iovs_query;
	sgl_query.sg_nr = 1;
	rc = crt_bulk_access(bulk_hdl, &sgl_query);
	assert(rc == -DER_TRUNC && sgl_query.sg_nr_out == 2);

	sgl_query.sg_nr = 2;
	rc = crt_bulk_access(bulk_hdl, &sgl_query);
	assert(rc == 0);
	assert(sgl_query.sg_nr_out == 2);
	rc = memcmp(iovs, iovs_query, 2 * sizeof(d_iov_t));
	assert(rc == 0);

	D_ALLOC(pchar, 256);
	assert(pchar != NULL);

	e_bulk_req = crt_req_get(rpc_req);

	e_bulk_req->bulk_intro_msg = pchar;
	e_bulk_req->remote_bulk_hdl = bulk_hdl;
	e_bulk_req->bulk_md5_ptr = md5_str;
	if (bulk_bind) {
		rc = crt_bulk_bind(bulk_hdl, gecho.crt_ctx);
		assert(rc == 0);
		e_bulk_req->bulk_forward = 1;
		e_bulk_req->bulk_bind = 1;
		e_bulk_req->bulk_forward_rank = 1;
		snprintf(pchar, 256,
			 "bulk forward testing from client(rank %d)...\n",
			 myrank);
	} else {
		snprintf(pchar, 256,
			 "simple bulk testing from client(rank %d)...\n",
			 myrank);
	}

	printf("client(rank %d) sending bulk_test request, md5_str: %s.\n",
	       myrank, md5_str);
	gecho.complete = 0;

	bulk_req_cbinfo = (struct bulk_test_cli_cbinfo *)malloc(
				sizeof(*bulk_req_cbinfo));
	assert(bulk_req_cbinfo != NULL);
	bulk_req_cbinfo->bulk_hdl = bulk_hdl;
	bulk_req_cbinfo->complete_flag = &gecho.complete;

	rc = crt_req_send(rpc_req, bulk_test_req_cb, bulk_req_cbinfo);
	assert(rc == 0);

	rc = client_wait(100, 100, &gecho.complete);
	free(md5_str);
	fprintf(stderr, "gecho.complete %d\n", gecho.complete);
	assert(rc == 0);
	free(iovs[0].iov_buf);
	free(iovs[1].iov_buf);
	free(iovs);
	D_FREE(pchar);

	return rc;
}

static void run_client(void)
{
	crt_group_t			*pri_local_grp = NULL;
	crt_group_t			*pri_srv_grp = NULL;
	crt_group_t			*grp_tier1 = NULL;
	crt_group_t			*grp_tier2 = NULL;
	crt_endpoint_t			svr_ep = {0};
	crt_rpc_t			*rpc_req = NULL;
	d_rank_t			myrank;
	uint32_t			grp_size_cli = 0;
	uint32_t			grp_size_srv = 0;
	uint32_t			grp_size_tier2 = 0;
	uint32_t			grp_size;
	struct crt_echo_checkin_in	*e_req;
	char				*pchar;
	int				rc = 0;
	int				i, j;

	rc = crt_group_rank(NULL, &myrank);
	D_ASSERT(rc == 0);
	pri_local_grp = crt_group_lookup(NULL);
	D_ASSERT(pri_local_grp != NULL);
	pri_srv_grp = crt_group_lookup("non-existent-grp");
	D_ASSERT(pri_srv_grp == NULL);

	/* try until success to avoid intermittent failures under valgrind. */
	do {
		sleep(1);
		rc = crt_group_attach(CRT_DEFAULT_SRV_GRPID, &grp_tier1);
	} while (rc != 0);
	D_ASSERT(grp_tier1 != NULL);
	pri_srv_grp = crt_group_lookup(CRT_DEFAULT_SRV_GRPID);
	D_ASSERT(pri_srv_grp != NULL);

	rc = crt_group_rank(pri_srv_grp, &myrank);
	D_ASSERT(rc == -DER_OOG);
	rc = crt_group_rank(pri_local_grp, &myrank);
	D_ASSERT(rc == 0);
	rc = crt_group_size(pri_local_grp, &grp_size_cli);
	D_ASSERT(rc == 0 && grp_size_cli > 0);
	rc = crt_group_size(pri_srv_grp, &grp_size_srv);
	D_ASSERT(rc == 0 && grp_size_srv > 0);
	grp_size = grp_size_srv;

	printf("I'm rank %d in group %s(size %d), srv_group %s with size %d.\n",
	       myrank, pri_local_grp->cg_grpid, grp_size_cli,
	       pri_srv_grp->cg_grpid, grp_size_srv);

	/*
	 * ============= test-1 ============
	 * send NOOP RPC which without any input or output parameter.
	 */
	svr_ep.ep_grp = NULL;
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	rc = crt_req_create(gecho.crt_ctx, &svr_ep, ECHO_OPC_NOOP, &rpc_req);
	D_ASSERT(rc == 0);
	gecho.complete = 0;
	rc = crt_req_send(rpc_req, client_cb_common, &gecho.complete);
	assert(rc == 0);
	/* wait two minutes (in case of manually starting up clients) */
	rc = client_wait(120, 1000, &gecho.complete);
	assert(rc == 0);

	/*
	 * ============= test-2 ============
	 * send checkin RPC to different contexts of server
	 */
	for (i = 0; i < grp_size_srv; i++) {
		for (j = 0; j <= ECHO_EXTRA_CONTEXT_NUM; j++) {
			rc = echo_client_send_checkin(myrank, grp_tier1, i, j);
			assert(rc == 0);
		}
	}

	/*
	 * ============= test-3 ============
	 * simple bulk transferring
	 */
	rc = echo_bulk_test(grp_tier1, myrank, false);
	assert(rc == 0);

	/*
	 * ============= test-4 ============
	 * test to forward client bulk handle from server A to server B
	 */
	if (grp_size_srv >= 2)
		rc = echo_bulk_test(grp_tier1, myrank, true);
	assert(rc == 0);

	/*
	 * ============= test-5 ============
	 * attach to 2nd tier and send checkin RPC
	 */
	if (gecho.multi_tier_test == false)
		goto send_shutdown;

	rc = crt_group_attach(ECHO_2ND_TIER_GRPID, &grp_tier2);
	assert(rc == 0 && grp_tier2 != NULL);
	rc = crt_group_size(grp_tier2, &grp_size_tier2);
	D_ASSERT(rc == 0 && grp_size_tier2 > 0);

	for (i = 0; i <= ECHO_EXTRA_CONTEXT_NUM; i++) {
		svr_ep.ep_grp = grp_tier2;
		svr_ep.ep_rank = 0;
		svr_ep.ep_tag = i;
		rc = crt_req_create(gecho.crt_ctx, &svr_ep, ECHO_OPC_CHECKIN,
				    &rpc_req);
		assert(rc == 0 && rpc_req != NULL);

		e_req = crt_req_get(rpc_req);
		assert(e_req != NULL);

		D_ALLOC(pchar, 256);
		assert(pchar != NULL);
		snprintf(pchar, 256, "Guest_%d_%d@client-side",
			 myrank, svr_ep.ep_tag);

		e_req->name = pchar;
		e_req->age = 32 + svr_ep.ep_tag;
		e_req->days = myrank;

		D_DEBUG(DB_TEST, "client(rank %d) sending checkin rpc to "
			"tier2 with tag %d, name: %s, age: %d, days: %d.\n",
			myrank, svr_ep.ep_tag, e_req->name, e_req->age,
			e_req->days);

		gecho.complete = 0;
		rc = crt_req_send(rpc_req, client_cb_common, &gecho.complete);
		assert(rc == 0);
		/* wait two minutes (in case of manually starting up clients) */
		rc = client_wait(120, 1000, &gecho.complete);
		assert(rc == 0);
		D_FREE(pchar);

		printf("client(rank %d, tag %d) checkin req sent to tier2.\n",
		       myrank, svr_ep.ep_tag);
	}

	/* ====================== */
	/* send an RPC to kill the server */
	svr_ep.ep_grp = grp_tier1;
send_shutdown:
	assert(rc == 0);
	if (myrank != 0)
		goto out;
	printf("client (rank 0) sending shutdown request...\n");

	for (i = 0; i < grp_size; i++) {
		gecho.complete = 0;

		rpc_req = NULL;
		svr_ep.ep_rank = i;
		svr_ep.ep_tag = 0;
		rc = crt_req_create(gecho.crt_ctx, &svr_ep, ECHO_OPC_SHUTDOWN,
				    &rpc_req);
		assert(rc == 0 && rpc_req != NULL);

		assert(rpc_req->cr_input == NULL);
		assert(rpc_req->cr_output == NULL);

		rc = crt_req_send(rpc_req, client_cb_common, &gecho.complete);
		assert(rc == 0);

		rc = client_wait(100, 100, &gecho.complete);
		assert(rc == 0);
	}

	if (svr_ep.ep_grp == grp_tier2 && grp_tier2 != NULL) {
		rc = crt_group_detach(grp_tier2);
		assert(rc == 0);
		goto out;
	}

	if (gecho.multi_tier_test == true) {
		svr_ep.ep_grp = grp_tier2;
		grp_size = grp_size_tier2;
		goto send_shutdown;
	}

out:
	rc = crt_group_detach(grp_tier1);
	D_ASSERT(rc == 0);

	printf("client(rank %d) shuting down...\n", myrank);
}

int main(int argc, char *argv[])
{
	parse_options(argc, argv);

	echo_init(0, false);

	run_client();

	echo_fini();

	return 0;
}
