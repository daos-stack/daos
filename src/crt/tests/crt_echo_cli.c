/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This is a simple example of crt_echo rpc client based on crt APIs.
 */

#include <crt_echo.h>

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

	rpc_req = cb_info->dci_rpc;
	bulk_test_cbinfo = (struct bulk_test_cli_cbinfo *)cb_info->dci_arg;

	printf("in bulk_test_req_cb, opc: 0x%x, dci_rc: %d.\n",
	       rpc_req->dr_opc, cb_info->dci_rc);

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
	crt_endpoint_t			svr_ep;
	crt_rpc_t			*rpc_req = NULL;
	crt_sg_list_t			sgl, sgl_query;
	crt_iov_t			*iovs = NULL, iovs_query[2];
	crt_bulk_t			bulk_hdl;
	struct bulk_test_cli_cbinfo	*bulk_req_cbinfo;
	char				*pchar;
	crt_rank_t			myrank;
	struct crt_echo_checkin_req	*e_req;
	struct crt_echo_bulk_in_req	*e_bulk_req;
	int				rc = 0, i;

	rc = crt_group_rank(NULL, &myrank);
	assert(rc == 0);

	/* ============= test-1 ============ */

	/* send checkin RPC to different contexts of server*/
	for (i = 0; i <= ECHO_EXTRA_CONTEXT_NUM; i++) {
		svr_ep.ep_grp = NULL;
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

		C_ERROR("client(rank %d) sending checkin rpc with tag %d, "
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

	/* ============= test-2 ============
	 * simple bulk transferring */
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
	assert(rc == 0);
	free(iovs[0].iov_buf);
	free(iovs[1].iov_buf);
	free(iovs);
	C_FREE(pchar, 256);

	/* ====================== */
	/* send an RPC to kill the server */
	printf("client (rank 0) sending shutdown request...\n");
	gecho.complete = 0;
	assert(rc == 0);
	if (myrank != 0)
		goto out;

	rpc_req = NULL;
	svr_ep.ep_grp = NULL;
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	rc = crt_req_create(gecho.crt_ctx, svr_ep, ECHO_OPC_SHUTDOWN, &rpc_req);
	assert(rc == 0 && rpc_req != NULL);

	assert(rpc_req->dr_input == NULL);
	assert(rpc_req->dr_output == NULL);

	rc = crt_req_send(rpc_req, client_cb_common, &gecho.complete);
	assert(rc == 0);

	rc = client_wait(100, 100, &gecho.complete);
	assert(rc == 0);

out:
	printf("client(rank %d) shuting down...\n", myrank);
}

int main(int argc, char *argv[])
{
	echo_init(0);

	run_client();

	echo_fini();

	return 0;
}
