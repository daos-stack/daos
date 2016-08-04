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
 * This is a simple example of dtp_echo rpc server based on dtp APIs.
 */

#include <dtp_echo.h>

struct gecho gecho;

struct echo_serv {
	int		do_shutdown;
	pthread_t	progress_thread;
} echo_srv;

static void *progress_handler(void *arg)
{
	int rc, loop = 0, i;
	assert(arg == NULL);
	/* progress loop */
	do {
		rc = dtp_progress(gecho.dtp_ctx, 1, NULL, NULL);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("dtp_progress failed rc: %d.\n", rc);
			break;
		}

		if (ECHO_EXTRA_CONTEXT_NUM > 0) {
			for (i = 0; i < ECHO_EXTRA_CONTEXT_NUM; i++) {
				rc = dtp_progress(gecho.extra_ctx[i], 1, NULL,
						  NULL);
				if (rc != 0 && rc != -DER_TIMEDOUT) {
					D_ERROR("dtp_progress failed rc: %d.\n",
					       rc);
					break;
				}
			}
		}

		if (echo_srv.do_shutdown != 0) {
			/* to ensure the last SHUTDOWN request be handled */
			loop++;
			if (loop >= 100)
				break;
		}
	} while (1);

	printf("progress_handler: rc: %d, echo_srv.do_shutdown: %d.\n",
	       rc, echo_srv.do_shutdown);
	printf("progress_handler: progress thread exit ...\n");

	pthread_exit(NULL);
}

dtp_group_t *example_grp;
int grp_create_cb(dtp_group_t *grp, void *priv, int status)
{
	printf("in grp_create_cb, grp %p, priv %p, status %d.\n",
		grp, priv, status);
	example_grp = grp;
	return 0;
}

int grp_destroy_cb(void *arg, int status)
{
	printf("in grp_destroy_cb, arg %p, status %d.\n", arg, status);
	return 0;
}

static int run_echo_srver(void)
{
	dtp_endpoint_t		svr_ep;
	dtp_rpc_t		*rpc_req = NULL;
	char			*pchar;
	daos_rank_t		myrank;
	uint32_t		mysize;
	int			rc, loop = 0;
	struct dtp_echo_checkin_req *e_req;

	rc = dtp_group_rank(NULL, &myrank);
	assert(rc == 0);
	rc = dtp_group_size(NULL, &mysize);
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
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	rc = dtp_req_create(gecho.dtp_ctx, svr_ep, ECHO_OPC_CHECKIN, &rpc_req);
	assert(rc == 0 && rpc_req != NULL);

	D_ALLOC(pchar, 256);
	assert(pchar != NULL);
	snprintf(pchar, 256, "Guest_%d@server-side", myrank);

	e_req = dtp_req_get(rpc_req);
	e_req->name = pchar;
	e_req->age = 32;
	e_req->days = myrank;

	D_DEBUG(DF_UNKNOWN, "server(rank %d) sending checkin request, name: %s,"
	       " age: %d, days: %d.\n", myrank,
		e_req->name, e_req->age, e_req->days);

	gecho.complete = 0;
	rc = dtp_req_send(rpc_req, client_cb_common, &gecho.complete);
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
	D_FREE(pchar, 256);

	/* ==================================== */
	/* test group API and bcast RPC */
	dtp_group_id_t		grp_id = "example_grp";
	daos_rank_t		grp_ranks[4] = {5, 4, 1, 2};
	daos_rank_list_t	grp_membs;
	daos_rank_t		excluded_ranks[2] = {1, 2};
	daos_rank_list_t	excluded_membs;

	grp_membs.rl_nr.num = 4;
	grp_membs.rl_ranks = grp_ranks;
	excluded_membs.rl_nr.num = 2;
	excluded_membs.rl_ranks = excluded_ranks;

	if (mysize >= 6 && myrank == 4) {
		dtp_rpc_t				*corpc_req;
		struct dtp_echo_corpc_example_req	*corpc_in;

		rc = dtp_group_create(grp_id, &grp_membs, 0, grp_create_cb,
				      &myrank);
		printf("dtp_group_create rc: %d, priv %p.\n", rc, &myrank);
		sleep(1); /* just to ensure grp populated */

		rc = dtp_corpc_req_create(gecho.dtp_ctx, example_grp,
					  &excluded_membs, ECHO_CORPC_EXAMPLE,
					  NULL, NULL, 0, 0, &corpc_req);
		D_ASSERT(rc == 0 && corpc_req != NULL);
		corpc_in = dtp_req_get(corpc_req);
		D_ASSERT(corpc_in != NULL);
		corpc_in->co_msg = "testing corpc example from rank 4";

		gecho.complete = 0;
		rc = dtp_req_send(corpc_req, client_cb_common,
				  &gecho.complete);
		D_ASSERT(rc == 0);
		sleep(1); /* just to ensure corpc handled */
		D_ASSERT(gecho.complete == 1);

		rc = dtp_group_destroy(example_grp, grp_destroy_cb, &myrank);
		printf("dtp_group_destroy rc: %d, arg %p.\n", rc, &myrank);
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

int echo_srv_shutdown(dtp_rpc_t *rpc_req)
{
	int rc = 0;

	printf("echo_srver received shutdown request, opc: 0x%x.\n",
	       rpc_req->dr_opc);

	assert(rpc_req->dr_input == NULL);
	assert(rpc_req->dr_output == NULL);

	rc = dtp_reply_send(rpc_req);
	printf("echo_srver done issuing shutdown responses.\n");

	echo_srv.do_shutdown = 1;
	printf("echo_srver set shutdown flag.\n");

	return rc;
}

int echo_srv_corpc_example(dtp_rpc_t *rpc_req)
{
	struct dtp_echo_corpc_example_req *req;
	struct dtp_echo_corpc_example_reply *reply;
	daos_rank_t my_rank;
	int rc = 0;

	req = dtp_req_get(rpc_req);
	reply = dtp_reply_get(rpc_req);
	D_ASSERT(req != NULL && reply != NULL);

	dtp_group_rank(NULL, &my_rank);
	reply->co_result = my_rank;

	rc = dtp_reply_send(rpc_req);

	printf("echo_srv_corpc_example, rank %d got msg %s, reply %d, rc %d.\n",
	       my_rank, req->co_msg, reply->co_result, rc);

	return rc;
}

int corpc_example_aggregate(dtp_rpc_t *source, dtp_rpc_t *result, void *priv)
{
	struct dtp_echo_corpc_example_reply *reply_source, *reply_result;
	daos_rank_t my_rank;

	D_ASSERT(source != NULL && result != NULL);
	reply_source = dtp_reply_get(source);
	reply_result = dtp_reply_get(result);
	reply_result->co_result += reply_source->co_result;

	dtp_group_rank(NULL, &my_rank);
	printf("corpc_example_aggregate, rank %d, co_result %d, aggregate "
	       "result %d.\n", my_rank, reply_source->co_result,
	       reply_result->co_result);

	return 0;
}

struct dtp_corpc_ops echo_co_ops = {
	.co_aggregate = corpc_example_aggregate,
};

int g_roomno = 1082;
int echo_srv_checkin(dtp_rpc_t *rpc_req)
{
	struct dtp_echo_checkin_req *e_req;
	struct dtp_echo_checkin_reply *e_reply;
	int rc = 0;

	/* dtp internally already allocated the input/output buffer */
	e_req = dtp_req_get(rpc_req);
	D_ASSERT(e_req != NULL);

	printf("echo_srver recv'd checkin, opc: 0x%x.\n", rpc_req->dr_opc);
	printf("checkin input - age: %d, name: %s, days: %d.\n",
		e_req->age, e_req->name, e_req->days);

	e_reply = dtp_reply_get(rpc_req);
	D_ASSERT(e_reply != NULL);
	e_reply->ret = 0;
	e_reply->room_no = g_roomno++;

	rc = dtp_reply_send(rpc_req);

	printf("echo_srver sent checkin reply, ret: %d, room_no: %d.\n",
	       e_reply->ret, e_reply->room_no);

	return rc;
}

int bulk_test_cb(const struct dtp_bulk_cb_info *cb_info)
{
	dtp_rpc_t			*rpc_req;
	struct dtp_bulk_desc		*bulk_desc;
	dtp_bulk_t			local_bulk_hdl;
	daos_iov_t			*iovs;
	struct dtp_echo_bulk_out_reply	*e_reply;
	struct dtp_echo_bulk_in_req	*e_req;
	int				rc = 0;

	rc = cb_info->bci_rc;
	bulk_desc = cb_info->bci_bulk_desc;
	/* printf("in bulk_test_cb, dci_rc: %d.\n", rc); */
	rpc_req = bulk_desc->bd_rpc;
	iovs = (daos_iov_t *)cb_info->bci_arg;
	assert(rpc_req != NULL && iovs != NULL);

	local_bulk_hdl = bulk_desc->bd_local_hdl;
	assert(local_bulk_hdl != NULL);

	e_reply = dtp_reply_get(rpc_req);
	D_ASSERT(e_reply != NULL);
	if (rc != 0) {
		printf("bulk transferring failed, dci_rc: %d.\n", rc);
		e_reply->ret = rc;
		e_reply->echo_msg = "bulk failed with data corruption.";
		goto out;
	}
	/* calculate md5 checksum to verify data */
	MD5_CTX md5_ctx;
	unsigned char md5[16];
	dtp_string_t md5_str = (dtp_string_t)malloc(33);
	memset(md5_str, 0, 33);

	rc = MD5_Init(&md5_ctx);
	assert(rc == 1);
	rc = MD5_Update(&md5_ctx, iovs[0].iov_buf, iovs[0].iov_buf_len);
	assert(rc == 1);
	rc = MD5_Final(md5, &md5_ctx);
	assert(rc == 1);
	echo_md5_to_string(md5, md5_str);

	e_req = dtp_req_get(rpc_req);
	D_ASSERT(e_req != NULL);
	rc = strcmp(md5_str, e_req->bulk_md5_ptr);
	if (rc == 0) {
		printf("data verification success, md5: %s.\n", md5_str);
		e_reply->ret = 0;
		e_reply->echo_msg = "bulk succeed (data verified).";
	} else {
		printf("data verification failed, md5: %s, origin_md5: %s.\n",
		       md5_str, e_req->bulk_md5_ptr);
		e_reply->ret = rc;
		e_reply->echo_msg = "bulk failed with data corruption.";
	}

	free(md5_str);

out:
	free(iovs[0].iov_buf);
	free(iovs);

	rc = dtp_bulk_free(local_bulk_hdl);
	assert(rc == 0);

	/* need to call dtp_reply_send first and then call dtp_req_decref,
	 * if changing the sequence possibly cause the RPC request be destroyed
	 * before sending reply. */
	rc = dtp_reply_send(rpc_req);
	assert(rc == 0);

	printf("echo_srver sent bulk_test reply, echo_msg: %s.\n",
	       e_reply->echo_msg);

	rc = dtp_req_decref(rpc_req);
	assert(rc == 0);

	return 0;
}

int echo_srv_bulk_test(dtp_rpc_t *rpc_req)
{
	dtp_bulk_t			local_bulk_hdl;
	daos_sg_list_t			sgl;
	daos_iov_t			*iovs = NULL;
	daos_size_t			bulk_len;
	unsigned int			bulk_sgnum;
	struct dtp_bulk_desc		bulk_desc;
	dtp_bulk_opid_t			bulk_opid;
	struct dtp_echo_bulk_in_req	*e_req;
	int				rc = 0;

	e_req = dtp_req_get(rpc_req);
	D_ASSERT(e_req != NULL);
	rc = dtp_bulk_get_len(e_req->remote_bulk_hdl, &bulk_len);
	assert(rc == 0);
	rc = dtp_bulk_get_sgnum(e_req->remote_bulk_hdl, &bulk_sgnum);

	printf("echo_srver recv'd bulk_test, opc: 0x%x, intro_msg: %s, "
	       "bulk_len: %ld, bulk_sgnum: %d.\n", rpc_req->dr_opc,
		e_req->bulk_intro_msg, bulk_len, bulk_sgnum);

	iovs = (daos_iov_t *)malloc(sizeof(daos_iov_t));
	iovs[0].iov_buf = malloc(bulk_len);
	iovs[0].iov_buf_len = bulk_len;
	memset(iovs[0].iov_buf, 0, iovs[0].iov_buf_len);
	sgl.sg_nr.num = 1;
	sgl.sg_iovs = iovs;

	rc = dtp_bulk_create(rpc_req->dr_ctx, &sgl, DTP_BULK_RW,
			     &local_bulk_hdl);
	assert(rc == 0);

	rc = dtp_req_addref(rpc_req);
	assert(rc == 0);

	bulk_desc.bd_rpc = rpc_req;

	bulk_desc.bd_bulk_op = DTP_BULK_GET;
	bulk_desc.bd_remote_hdl = e_req->remote_bulk_hdl;
	bulk_desc.bd_remote_off = 0;
	bulk_desc.bd_local_hdl = local_bulk_hdl;
	bulk_desc.bd_local_off = 0;
	bulk_desc.bd_len = bulk_len;

	/* user needs to register the complete_cb inside which can do:
	 * 1) resource reclaim includes freeing the:
	 *    a) the buffers for bulk, maybe also the daos_iov_t (iovs)
	 *    b) local bulk handle
	 *    c) cbinfo if needed,
	 * 2) reply to original RPC request (if the bulk is derived from a RPC)
	 * 3) dtp_req_decref (before return in this RPC handler, need to take a
	 *    reference to avoid the RPC request be destroyed by DTP, then need
	 *    to release the reference at bulk's complete_cb);
	 */
	rc = dtp_bulk_transfer(&bulk_desc, bulk_test_cb, iovs, &bulk_opid);
	assert(rc == 0);

	return rc;
}

int main(int argc, char *argv[])
{
	echo_init(1);

	run_echo_srver();

	echo_fini();

	return 0;
}
