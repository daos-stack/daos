/*
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This is a simple example of rpc server based on crt APIs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include "rpc_test_common.h"

/*========================== GLOBAL ===========================*/

struct rpc_test_srv rpc_srv;

/*========================== GLOBAL ===========================*/

void
crt_srv_io_op_cb(crt_rpc_t *rpc_req)
{
	struct crt_rpc_io_in	*rpc_cli_input;
	struct crt_rpc_io_out	*rpc_srv_ouput;

	dbg("---%s--->", __func__);

	rpc_cli_input = crt_req_get(rpc_req);
	D_ASSERT(rpc_cli_input != NULL);

	dbg("cmd:=0x%X\tstatus:=0x%X\n \t\t\t\tmsg:=%s\traw_pkg:=%s\n",
	rpc_cli_input->to_srv, rpc_cli_input->from_srv,
	rpc_cli_input->msg, (char *)rpc_cli_input->raw_pkg.iov_buf);

	rpc_srv_ouput = crt_reply_get(rpc_req);
	D_ASSERT(rpc_srv_ouput != NULL);

	rpc_srv_ouput->to_srv = 0;
	rpc_srv_ouput->from_srv = rpc_srv.my_rank;
	rpc_srv_ouput->msg = strdup("D:Test Msg:= Hello from server");
	D_ASSERT(rpc_srv_ouput->msg != NULL);

	char *tmp = "Test Msg:= iov packet data from server";

	d_iov_set(&rpc_srv_ouput->raw_pkg, tmp, strlen(tmp)+1);

	dbg("cmd:=0x%X\tstatus:=0x%X\n"
	"\t\t\t\tmsg:=%s\traw_pkg:=%s\n",
	rpc_srv_ouput->to_srv,
	rpc_srv_ouput->from_srv,
	rpc_srv_ouput->msg,
	(char *)rpc_srv_ouput->raw_pkg.iov_buf);

	dbg("<---%s---", __func__);
}

void
crt_srv_err_noop(crt_rpc_t *rpc_req)
{
	struct crt_rpc_io_in	*rpc_cli_input;

	dbg("---%s--->", __func__);

	rpc_cli_input = crt_req_get(rpc_req);
	D_ASSERT(rpc_cli_input != NULL);

	dbg("cmd:=0x%X\tstatus:=0x%X\n\t\t\t\traw_pkg:=%s\n",
	rpc_cli_input->to_srv, rpc_cli_input->from_srv,
	(char *)rpc_cli_input->raw_pkg.iov_buf);

	dbg("<---%s---", __func__);
}

void
srv_corpc_io(crt_rpc_t *rpc_req)
{
	struct crt_rpc_grp_io_out	*reply;
	struct crt_rpc_grp_io_in	*req;
	d_rank_t			 my_rank;
	int				 rc = 0;

	dbg("---%s--->", __func__);

	req = crt_req_get(rpc_req);
	reply = crt_reply_get(rpc_req);
	D_ASSERT(req != NULL && reply != NULL);

	crt_group_rank(NULL, &my_rank);
	reply->from_srv = my_rank;

	dbg("rank %d got msg %s, reply %d, rc %d.\n",
	my_rank, req->msg, reply->from_srv, rc);

}

int
srv_grp_aggregate(crt_rpc_t *source, crt_rpc_t *result, void *arg)
{
	struct crt_rpc_grp_io_out	*grp_io_src_reply;
	struct crt_rpc_grp_io_out	*grp_io_result;
	d_rank_t			 my_rank;

	dbg("---%s--->", __func__);

	D_ASSERT(source != NULL && result != NULL);
	grp_io_src_reply = crt_reply_get(source);
	grp_io_result = crt_reply_get(result);
	grp_io_result->from_srv += grp_io_src_reply->from_srv;

	crt_group_rank(NULL, &my_rank);

	dbg("rank %d, co_result %d, aggregate result %d.\n",
	my_rank, grp_io_src_reply->from_srv, grp_io_result->from_srv);

	dbg("<---%s---", __func__);
	return 0;
}

struct crt_corpc_ops grp_co_ops = {
	.co_aggregate = srv_grp_aggregate,
	.co_pre_forward = NULL,
};


void
srv_common_cb(crt_rpc_t *rpc_req)
{
	struct crt_rpc_io_in	*rpc_cli_input;
	int			 rc = 0;

	dbg("---%s--->", __func__);

	dbg("client has connected to server[%u]\n", rpc_srv.my_rank);

	dbg("rpc_req->cr_opc:%#x\n", rpc_req->cr_opc);

	switch (rpc_req->cr_opc) {
	case CRT_RPC_TEST_IO:
		dbg("CRT_RPC_TEST_IO\n");
		crt_srv_io_op_cb(rpc_req);
		break;
	case CRT_RPC_TEST_ERR:
		dbg("CRT_RPC_TEST_ERR");
		crt_srv_err_noop(rpc_req);
		goto exit;
		/* break; */
	case CRT_RPC_TEST_NO_IO:
		dbg("CRT_RPC_TEST_NO_IO\n");
		break;
	case CRT_RPC_TEST_GRP_IO:
		dbg("CRT_RPC_TEST_GRP_IO\n");
		srv_corpc_io(rpc_req);
		if (rpc_srv.my_rank == 1)
			goto exit;
		break;
	case CRT_RPC_TEST_TIMEOUT:
		dbg("CRT_RPC_TEST_TIMEOUT");
		rpc_cli_input = crt_req_get(rpc_req);
		D_ASSERT(rpc_cli_input != NULL);
		dbg("cmd:=0x%X\tstatus:=0x%X\n"
		"\t\t\t\traw_package:=%s\n",
		rpc_cli_input->to_srv, rpc_cli_input->from_srv,
		(char *)rpc_cli_input->raw_pkg.iov_buf);
		sleep((rpc_cli_input->to_srv+1));
		break;
	case CRT_RPC_TEST_SHUTDOWN:
		dbg("CRT_RPC_TEST_SHUTDOWN");
		D_ASSERT(rpc_req->cr_input == NULL);
		D_ASSERT(rpc_req->cr_output == NULL);
		rpc_srv.shutdown = 1;
		goto exit;
		/* break; */
	default:
		dbg("Invalid command\n");
		break;
	}

	rc = crt_reply_send(rpc_req);
	D_ASSERTF(rc == 0, "crt_reply_send failed %d\n", rc);

exit:
	dbg("<---%s---", __func__);
}

static void
*srv_progress_handler(void *data)
{
	crt_context_t	*p_ctx = (crt_context_t *)data;
	int		 rc = 0;


	dbg("---%s--->", __func__);

	D_ASSERTF(p_ctx != NULL, "p_ctx:=%p\n", p_ctx);

	while (rpc_srv.shutdown == 0) {
		rc = crt_progress(*p_ctx, 1000);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("crt_progress failed %d\n", rc);
			break;
		}
	}

	dbg("progress_handler: progress thread exit ...\n");

	dbg("<---%s---", __func__);

	return NULL;
}

static void
srv_rpc_finalize(void)
{
	d_rank_t	myrank;
	int		rc = 0;

	dbg("---%s--->", __func__);

	rc = crt_group_rank(NULL, &myrank);
	D_ASSERTF(rc == 0, "crt_group_rank failed %d\n", rc);

	if (rpc_srv.target_multitier_grp != NULL) {
		rc = crt_group_detach(rpc_srv.target_multitier_grp);
		D_ASSERTF(rc == 0, "crt_group_detach failed %d\n", rc);
	}

	dbg("main thread wait progress thread ...\n");

	if (rpc_srv.progress_thid)
		pthread_join(rpc_srv.progress_thid, NULL);

	rc = crt_context_destroy(rpc_srv.crt_ctx, 1);
	D_ASSERTF(rc == 0, "crt_context_destroy failed %d\n", rc);

	rc = sem_destroy(&rpc_srv.srv_sem);
	D_ASSERTF(rc == 0, "sem_destroy() failed.%d\n", rc);

	if (myrank == 0) {
		rc = crt_group_config_remove(NULL);
		assert(rc == 0);
	}

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize failed %d\n", rc);

	dbg("<---%s---", __func__);
}

void
srv_common_client_cb(const struct crt_cb_info *cb_info)
{
	struct crt_rpc_grp_io_out	*grp_io_out;

	dbg("---%s--->", __func__);

	dbg("opc:%x\tcci_rc:%d\t-DER_TIMEDOUT:=%i.\n",
	cb_info->cci_rpc->cr_opc, cb_info->cci_rc, -DER_TIMEDOUT);

	dbg("server has responded\n");

	switch (cb_info->cci_rpc->cr_opc) {
	case CRT_RPC_TEST_GRP_IO:
		dbg("CRT_RPC_TEST_GRP_IO");
		grp_io_out = crt_reply_get(cb_info->cci_rpc);
		dbg("group operation  finished,: %d.\n",
		       grp_io_out->from_srv);
		printf("\nsrv:group IO test %s with rc:=%d\n\n",
		((cb_info->cci_rc == -DER_TIMEDOUT) ? "Passed" : "failed"),
		cb_info->cci_rc);
		break;
	case CRT_RPC_MULTITIER_TEST_IO:
		dbg("CRT_RPC_TEST_MULTITIER_IO\n");
		printf("\nsrv:multitier group IO test %s with rc:=%d\n\n",
		((cb_info->cci_rc == 0) ? "Passed" : "failed"),
		cb_info->cci_rc);
		break;
	case CRT_RPC_MULTITIER_TEST_NO_IO:
		dbg("CRT_RPC_MULTITIER_TEST_NO_IO\n");
		printf("\nsrv:multitier group no IO test %s with rc:=%d\n\n",
		((cb_info->cci_rc == 0) ? "Passed" : "failed"),
		cb_info->cci_rc);
		break;
	default:
		dbg("default\n");
		break;
	}

	/* set completion flag */
	dbg("setting the completion flag\n");
	if ((int *) cb_info->cci_arg)
		*(int *) cb_info->cci_arg = 1;

	sem_post(&rpc_srv.srv_sem);

	dbg("<---%s---", __func__);

}

void
srv_sem_timedwait(sem_t *sem, int sec)
{
	struct timespec	deadline;
	int		rc;


	rc = clock_gettime(CLOCK_REALTIME, &deadline);
	D_ASSERTF(rc == 0, "clock_gettime() failed with rc:%d err:=%d\n",
		rc, errno);
	deadline.tv_sec += sec;
	rc = sem_timedwait(sem, &deadline);
	D_ASSERTF(rc == 0, "sem_timedwait() failed with rc:%d err:=%d\n",
		rc, errno);
}

int
srv_rpc_multitier_io(void)
{
	struct crt_rpc_io_in	*cli_rpc_input;
	crt_endpoint_t		 svr_ep;
	crt_rpc_t		*rpc_req = NULL;
	int			 rc = 0, complete_flag;

	dbg("---%s--->", __func__);

	svr_ep.ep_grp = rpc_srv.target_multitier_grp;
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;

	rc = crt_req_create(rpc_srv.crt_ctx, &svr_ep,
		CRT_RPC_MULTITIER_TEST_NO_IO, &rpc_req);
	D_ASSERTF(rc == 0, "crt_req_create failed %d\n", rc);

	complete_flag = 0;
	rc = crt_req_send(rpc_req, srv_common_client_cb,
			&complete_flag);
	D_ASSERTF(rc == 0, "crt_req_send failed %d\n", rc);

	srv_sem_timedwait(&rpc_srv.srv_sem, 61);

	dbg("completion flag has been set to %d\n", complete_flag);

	svr_ep.ep_grp = rpc_srv.target_multitier_grp;
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	rc = crt_req_create(rpc_srv.crt_ctx, &svr_ep,
			CRT_RPC_MULTITIER_TEST_IO, &rpc_req);
	D_ASSERTF(rc == 0, "crt_req_create failed %d\n", rc);

	cli_rpc_input = crt_req_get(rpc_req);
	D_ASSERT(cli_rpc_input != NULL);

	cli_rpc_input->to_srv = CRT_RPC_MULTITIER_TEST_IO;
	cli_rpc_input->from_srv = 0;

	char *tmp = "Test Msg:= iov packet data from default grp server";

	d_iov_set(&cli_rpc_input->raw_pkg, tmp, strlen(tmp)+1);

	dbg("cmd:=0x%X\tstatus:=0x%X\n"
	"\t\t\t\traw_pkg:=%s\n",
	cli_rpc_input->to_srv,
	cli_rpc_input->from_srv,
	(char *)cli_rpc_input->raw_pkg.iov_buf);

	complete_flag = 0;
	rc = crt_req_send(rpc_req, srv_common_client_cb, &complete_flag);
	D_ASSERTF(rc == 0, "crt_req_send failed %d\n", rc);

	srv_sem_timedwait(&rpc_srv.srv_sem, 61);

	dbg("completion flag has been set to %d\n", complete_flag);

	dbg("<---%s---", __func__);

	return 0;
}

int
grp_create_cb(crt_group_t *grp, void *priv, int status)
{
	dbg("---%s--->", __func__);

	dbg("grp:=%p\tpriv:=%pstatus:=%d.\n", grp, priv, status);

	rpc_srv.local_group = grp;

	dbg("rpc_srv.local_group:=%p\n", rpc_srv.local_group);
	sem_post(&rpc_srv.srv_sem);
	dbg("<---%s---", __func__);

	return 0;
}

int
grp_destroy_cb(void *arg, int status)
{
	dbg("---%s--->", __func__);

	dbg("arg:=%p,status:=%d.\n", arg, status);

	dbg("<---%s---", __func__);

	return 0;
}

int
grp_rpc_test(void)
{
	crt_group_id_t	grp_id = "rpc_grp_test";
	d_rank_list_t	grp_membs;
	d_rank_list_t	excluded_membs;
	d_rank_t	grp_ranks[4] = { 1, 2, 3, 4};
	d_rank_t	excluded_ranks[2] = {2, 3};
	d_rank_t	myrank;
	uint32_t	mysize;
	int		rc = 0, complete_flag;

	dbg("---%s--->", __func__);

	grp_membs.rl_nr = 4;
	grp_membs.rl_ranks = grp_ranks;
	excluded_membs.rl_nr = 2;
	excluded_membs.rl_ranks = excluded_ranks;

	rc = crt_group_rank(NULL, &myrank);
	D_ASSERTF(rc == 0, "crt_group_rank failed %d\n", rc);

	rc = crt_group_size(NULL, &mysize);
	D_ASSERTF(rc == 0, "crt_group_size failed %d\n", rc);

	dbg("myrank:=%d\tmysize:=%d", myrank, mysize);

	if (mysize >= 4 && myrank == 3) {
		struct crt_rpc_grp_io_in	*corpc_in;
		crt_rpc_t			*corpc_req;

		rc = crt_group_create(grp_id, &grp_membs, 0, grp_create_cb,
				&myrank);
		dbg("crt_group_create rc: %d, priv %p.", rc, &myrank);

		/* just to ensure grp populated */
		srv_sem_timedwait(&rpc_srv.srv_sem, 61);

		rc = crt_corpc_req_create(rpc_srv.crt_ctx,
				rpc_srv.local_group, &excluded_membs,
				CRT_RPC_TEST_GRP_IO, NULL, NULL, 0,
				crt_tree_topo(CRT_TREE_KNOMIAL, 4),
				&corpc_req);
		D_ASSERT(rc == 0 && corpc_req != NULL);

		corpc_in = crt_req_get(corpc_req);
		D_ASSERT(corpc_in != NULL);
		corpc_in->msg = "testing grp io example from rank 3";

		complete_flag = 0;
		rc = crt_req_send(corpc_req, srv_common_client_cb,
				  &complete_flag);
		D_ASSERTF(rc == 0, "crt_req_send failed %d\n", rc);

		srv_sem_timedwait(&rpc_srv.srv_sem, 61);
		dbg("completion flag has been set to %d\n", complete_flag);

		rc = crt_group_destroy(rpc_srv.local_group, grp_destroy_cb,
				&myrank);

		dbg("group destroyed  rc:=%d,arg:=%p\trank:=%d\n",
		rc, &myrank, myrank);
	}
	dbg("<---%s---", __func__);

	return rc;
}

static struct crt_proto_rpc_format my_proto_rpc_fmt_test_srv[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_rpc_io,
		.prf_hdlr	= srv_common_cb,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_test_no_io,
		.prf_hdlr	= srv_common_cb,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_test_err,
		.prf_hdlr	= srv_common_cb,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_test_timeout,
		.prf_hdlr	= srv_common_cb,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= CRT_RPC_FEAT_NO_REPLY,
		.prf_req_fmt	= &CQF_crt_test_shutdown,
		.prf_hdlr	= srv_common_cb,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_rpc_grp_io,
		.prf_hdlr	= srv_common_cb,
		.prf_co_ops	= &grp_co_ops,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_multitier_test_io,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_multitier_test_no_io,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}
};


static struct crt_proto_format my_proto_fmt_test_srv = {
	.cpf_name = "my-proto-test-srv",
	.cpf_ver = TEST_RPC_COMMON_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_test_srv),
	.cpf_prf = &my_proto_rpc_fmt_test_srv[0],
	.cpf_base = TEST_RPC_COMMON_BASE,
};

int
srv_rpc_init(void)
{
	int	rc = 0;

	dbg("---%s--->", __func__);

	rc = crt_init(CRT_DEFAULT_GRPID, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	D_ASSERTF(rc == 0, " crt_init failed %d\n", rc);

	rc = crt_group_config_path_set(rpc_srv.config_path);
	D_ASSERTF(rc == 0, "crt_group_config_path_set failed %d\n", rc);

	rc  = crt_group_config_save(NULL, false);
	D_ASSERTF(rc == 0, "crt_group_config_save failed %d\n", rc);

	rc = crt_proto_register(&my_proto_fmt_test_srv);
	D_ASSERTF(rc == 0, "crt_proto_register failed %d\n", rc);

	rc = crt_group_rank(NULL, &rpc_srv.my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank failed %d\n", rc);

	rc = crt_group_size(NULL, &rpc_srv.grp_size);
	D_ASSERTF(rc == 0, "crt_group_size failed %d\n", rc);

	rc = crt_context_create(&rpc_srv.crt_ctx);
	D_ASSERTF(rc == 0, "crt_context_create failed %d\n", rc);

	rc = sem_init(&rpc_srv.srv_sem, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.%d\n", rc);

	/* create progress thread */
	rc = pthread_create(&rpc_srv.progress_thid, NULL,
			srv_progress_handler, &rpc_srv.crt_ctx);
	if (rc != 0)
		printf("progress thread creating failed, rc: %d.\n", rc);

	dbg("my_rank:=%u,\tgroup_size:=%i\tsrv_pid:=%d\n",
		rpc_srv.my_rank, rpc_srv.grp_size, getpid());


	/* try until success to avoid failures until server isn't ready . */
	do {
		sleep(1);
		rc = crt_group_attach(CRT_RPC_MULTITIER_GRPID,
			&rpc_srv.target_multitier_grp);

		dbg("Attaching to multitier server grp\n");

	} while (rc != 0);
	if (rc == 0 && rpc_srv.target_multitier_grp != NULL) {
		D_DEBUG(DB_ALL, "testing multitier io.\n");
		srv_rpc_multitier_io();
	} else {
		dbg("multitier group attachment failed:=%d", rc);
	}

	dbg("<---%s---", __func__);
	return rc;
}

void
print_usage(char *argv[])
{
	dbg("---%s--->", __func__);

	printf("Usage:%s\n", ((char *)strrchr(argv[0], '/') + 1));
	printf("OPTIONS:\n");
	printf("-c config path\n");

	dbg("<---%s---", __func__);
}

int
main(int argc, char *argv[])
{
	int	ch;

	assert(d_log_init() == 0);

	dbg("---%s--->", __func__);

	dbg("srv_pid:=%d", getpid());

	dbg("argc:=%d\n", argc);

	if (argc <= 1) {
		print_usage(argv);
		exit(1);
	}

	while ((ch = getopt(argc, argv, "c:")) != -1) {
		switch (ch) {
		case 'c':
			dbg("-c:=%s\n", optarg);
			snprintf(rpc_srv.config_path, 256, "%s", optarg);
			break;
		default:
			dbg("default\n");
			print_usage(argv);
			exit(1);
		}
	}

	dbg("config_path: = %s", rpc_srv.config_path);

	/* default value */


	srv_rpc_init();
	grp_rpc_test();

	dbg("main thread wait progress thread ...\n");
	if (rpc_srv.progress_thid)
		pthread_join(rpc_srv.progress_thid, NULL);

	srv_rpc_finalize();

	dbg("<---%s---", __func__);
	d_log_fini();

	return 0;
} /* main */
