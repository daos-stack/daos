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
 * This is a simple example of crt_echo rpc client based on crt APIs.
 */

#include "crt_echo.h"

bool test_multi_tiers;

struct gecho gecho;

static int client_wait(int num_retries, unsigned int wait_len_ms,
		       int *complete_flag)
{
	int retry, rc;

	for (retry = 0; retry < num_retries; retry++) {
		rc = crt_progress(gecho.crt_ctx, wait_len_ms * 1000, NULL,
				  NULL);
		if (rc != 0 && rc != -CER_TIMEDOUT) {
			C_ERROR("crt_progress failed rc: %d.\n", rc);
			break;
		}
		if (*complete_flag)
			return 0;
	}
	return -ETIMEDOUT;
}

struct bulk_test_cli_cbinfo {
	crt_bulk_t	bulk_hdl;
	int		*complete_flag;
};

static int bulk_test_req_cb(const struct crt_cb_info *cb_info)
{
	struct bulk_test_cli_cbinfo	*bulk_test_cbinfo;
	crt_rpc_t			*rpc_req;
	struct crt_echo_bulk_out_reply	*e_reply;
	int				rc;

	rpc_req = cb_info->cci_rpc;
	bulk_test_cbinfo = (struct bulk_test_cli_cbinfo *)cb_info->cci_arg;

	printf("in bulk_test_req_cb, opc: 0x%x, cci_rc: %d.\n",
	       rpc_req->cr_opc, cb_info->cci_rc);

	e_reply = crt_reply_get(rpc_req);
	printf("bulk_test_output->bulk_echo_msg: %s. ret %d\n",
	       e_reply->echo_msg, e_reply->ret);

	rc = crt_bulk_free(bulk_test_cbinfo->bulk_hdl);
	assert(rc == 0);
	/* set complete flag */
	*(bulk_test_cbinfo->complete_flag) = 1;

	free(bulk_test_cbinfo);
	return 0;
}

static void run_client(void)
{
	crt_group_t			*pri_local_grp = NULL;
	crt_group_t			*pri_srv_grp = NULL;
	crt_group_t			*grp_tier2 = NULL;
	crt_endpoint_t			svr_ep;
	crt_rpc_t			*rpc_req = NULL;
	crt_sg_list_t			sgl, sgl_query;
	crt_iov_t			*iovs = NULL, iovs_query[2];
	crt_bulk_t			bulk_hdl;
	struct bulk_test_cli_cbinfo	*bulk_req_cbinfo;
	char				*pchar;
	crt_rank_t			myrank;
	uint32_t			grp_size_cli = 0;
	uint32_t			grp_size_srv = 0;
	struct crt_echo_checkin_req	*e_req;
	struct crt_echo_bulk_in_req	*e_bulk_req;
	int				rc = 0, i;

	rc = crt_group_rank(NULL, &myrank);
	C_ASSERT(rc == 0);
	pri_local_grp = crt_group_lookup(NULL);
	C_ASSERT(pri_local_grp != NULL);
	pri_srv_grp = crt_group_lookup("non-existent-grp");
	C_ASSERT(pri_srv_grp == NULL);
	pri_srv_grp = crt_group_lookup(CRT_DEFAULT_SRV_GRPID);
	C_ASSERT(pri_srv_grp != NULL);

	rc = crt_group_rank(pri_srv_grp, &myrank);
	C_ASSERT(rc == -CER_OOG);
	rc = crt_group_rank(pri_local_grp, &myrank);
	C_ASSERT(rc == 0);
	rc = crt_group_size(pri_local_grp, &grp_size_cli);
	C_ASSERT(rc == 0 && grp_size_cli > 0);
	rc = crt_group_size(pri_srv_grp, &grp_size_srv);
	C_ASSERT(rc == 0 && grp_size_srv > 0);

	printf("I'm rank %d in group %s(size %d), srv_group %s with size %d.\n",
	       myrank, pri_local_grp->cg_grpid, grp_size_cli,
	       pri_srv_grp->cg_grpid, grp_size_srv);

	/* ============= test-1 ============ */

	/* send checkin RPC to different contexts of server*/
	for (i = 0; i <= ECHO_EXTRA_CONTEXT_NUM; i++) {
		char		*raw_buf;
		struct timespec	t1, t2;
		double		time_us;

		svr_ep.ep_grp = NULL;
		svr_ep.ep_rank = 0;
		svr_ep.ep_tag = i;

		rc = crt_gettime(&t1);
		assert(rc == 0);
		rc = crt_req_create(gecho.crt_ctx, svr_ep, ECHO_OPC_CHECKIN,
				    &rpc_req);
		assert(rc == 0 && rpc_req != NULL);
		rc = crt_gettime(&t2);
		assert(rc == 0);
		time_us = crt_time2us(crt_timediff(t1, t2));
		printf("time for crt_req_create: %.3e uS.\n", time_us);

		e_req = crt_req_get(rpc_req);
		assert(e_req != NULL);

		C_ALLOC(pchar, 256);
		assert(pchar != NULL);
		snprintf(pchar, 256, "Guest_%d_%d@client-side",
			 myrank, svr_ep.ep_tag);

		raw_buf = "testing_only ---- data_in_raw_package";
		e_req->name = pchar;
		e_req->age = 32 + svr_ep.ep_tag;
		crt_iov_set(&e_req->raw_package, raw_buf,
			    strlen(raw_buf) + 1);
		e_req->days = myrank;

		C_DEBUG("client(rank %d) sending checkin rpc with tag %d, "
		       "name: %s, age: %d, days: %d.\n",
		       myrank, svr_ep.ep_tag, e_req->name, e_req->age,
		       e_req->days);

		gecho.complete = 0;
		rc = crt_req_send(rpc_req, client_cb_common, &gecho.complete);
		assert(rc == 0);
		/* wait two minutes (in case of manually starting up clients) */
		rc = client_wait(120, 1000, &gecho.complete);
		assert(rc == 0);
		C_FREE(pchar, 256);

		printf("client(rank %d, tag %d) checkin request sent.\n",
		       myrank, svr_ep.ep_tag);
	}

	/*
	 * ============= test-2 ============
	 * simple bulk transferring
	 */
	rpc_req = NULL;
	svr_ep.ep_grp = NULL;
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	rc = crt_req_create(gecho.crt_ctx, svr_ep, ECHO_OPC_BULK_TEST,
			    &rpc_req);
	assert(rc == 0 && rpc_req != NULL);

	iovs = (crt_iov_t *)malloc(2 * sizeof(crt_iov_t));
	iovs[0].iov_buf_len = 4097;
	iovs[0].iov_len = 4097;
	iovs[0].iov_buf = malloc(iovs[0].iov_buf_len);
	pchar = iovs[0].iov_buf;
	for (i = 0; i < iovs[0].iov_buf_len; i++)
		*(pchar++) = i + myrank;
	iovs[1].iov_buf_len = 1*1024*1024 + 11;
	iovs[1].iov_len = 1*1024*1024 + 11;
	iovs[1].iov_buf = malloc(iovs[1].iov_buf_len);
	pchar = iovs[1].iov_buf;
	for (i = 0; i < iovs[1].iov_buf_len; i++)
		*(pchar++) = random();
	sgl.sg_nr.num = 2;
	sgl.sg_iovs = iovs;

	/* calculate md5 checksum */
	MD5_CTX md5_ctx;
	unsigned char md5[16];
	crt_string_t md5_str = (crt_string_t)malloc(33);

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
	sgl_query.sg_nr.num = 1;
	rc = crt_bulk_access(bulk_hdl, &sgl_query);
	assert(rc == -CER_TRUNC && sgl_query.sg_nr.num_out == 2);

	sgl_query.sg_nr.num = 2;
	rc = crt_bulk_access(bulk_hdl, &sgl_query);
	assert(rc == 0);
	assert(sgl_query.sg_nr.num_out == 2);
	rc = memcmp(iovs, iovs_query, 2 * sizeof(crt_iov_t));
	assert(rc == 0);

	C_ALLOC(pchar, 256);
	assert(pchar != NULL);
	snprintf(pchar, 256, "simple bulk testing from client(rank %d)...\n",
		 myrank);

	e_bulk_req = crt_req_get(rpc_req);

	e_bulk_req->bulk_intro_msg = pchar;
	e_bulk_req->remote_bulk_hdl = bulk_hdl;
	e_bulk_req->bulk_md5_ptr = md5_str;

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
	assert(rc == 0);
	free(iovs[0].iov_buf);
	free(iovs[1].iov_buf);
	free(iovs);
	C_FREE(pchar, 256);

	/* ============= test-3 ============ */
	/* attach to 2nd tier and send checkin RPC */
	if (test_multi_tiers == false)
		goto send_shutdown;

	rc = crt_group_attach(ECHO_2ND_TIER_GRPID, &grp_tier2);
	assert(rc == 0 && grp_tier2 != NULL);

	for (i = 0; i <= ECHO_EXTRA_CONTEXT_NUM; i++) {
		svr_ep.ep_grp = grp_tier2;
		svr_ep.ep_rank = 0;
		svr_ep.ep_tag = i;
		rc = crt_req_create(gecho.crt_ctx, svr_ep, ECHO_OPC_CHECKIN,
				    &rpc_req);
		assert(rc == 0 && rpc_req != NULL);

		e_req = crt_req_get(rpc_req);
		assert(e_req != NULL);

		C_ALLOC(pchar, 256);
		assert(pchar != NULL);
		snprintf(pchar, 256, "Guest_%d_%d@client-side",
			 myrank, svr_ep.ep_tag);

		e_req->name = pchar;
		e_req->age = 32 + svr_ep.ep_tag;
		e_req->days = myrank;

		C_DEBUG("client(rank %d) sending checkin rpc to tier2 with "
			"tag %d, name: %s, age: %d, days: %d.\n",
			myrank, svr_ep.ep_tag, e_req->name, e_req->age,
			e_req->days);

		gecho.complete = 0;
		rc = crt_req_send(rpc_req, client_cb_common, &gecho.complete);
		assert(rc == 0);
		/* wait two minutes (in case of manually starting up clients) */
		rc = client_wait(120, 1000, &gecho.complete);
		assert(rc == 0);
		C_FREE(pchar, 256);

		printf("client(rank %d, tag %d) checkin req sent to tier2.\n",
		       myrank, svr_ep.ep_tag);
	}

	/* ====================== */
	/* send an RPC to kill the server */
	svr_ep.ep_grp = NULL;
send_shutdown:
	printf("client (rank 0) sending shutdown request...\n");
	gecho.complete = 0;
	assert(rc == 0);
	if (myrank != 0)
		goto out;

	rpc_req = NULL;
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	rc = crt_req_create(gecho.crt_ctx, svr_ep, ECHO_OPC_SHUTDOWN, &rpc_req);
	assert(rc == 0 && rpc_req != NULL);

	assert(rpc_req->cr_input == NULL);
	assert(rpc_req->cr_output == NULL);

	rc = crt_req_send(rpc_req, client_cb_common, &gecho.complete);
	assert(rc == 0);

	rc = client_wait(100, 100, &gecho.complete);
	assert(rc == 0);

	if (svr_ep.ep_grp == grp_tier2 && grp_tier2 != NULL) {
		rc = crt_group_detach(grp_tier2);
		assert(rc == 0);
		goto out;
	}

	if (test_multi_tiers == true) {
		svr_ep.ep_grp = grp_tier2;
		goto send_shutdown;
	}

out:
	printf("client(rank %d) shuting down...\n", myrank);
}

int main(int argc, char *argv[])
{
	echo_init(0, false);

	run_client();

	echo_fini();

	return 0;
}
