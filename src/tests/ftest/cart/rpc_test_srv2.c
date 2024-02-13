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

	dbg("cmd:=0x%X\tstatus:=0x%X\n"
	"\t\t\t\tmsg:=%s\traw_pkg:=%s\n",
	rpc_cli_input->to_srv,
	rpc_cli_input->from_srv,
	rpc_cli_input->msg,
	(char *)rpc_cli_input->raw_pkg.iov_buf);

	rpc_srv_ouput = crt_reply_get(rpc_req);
	D_ASSERT(rpc_srv_ouput != NULL);
	rpc_srv_ouput->to_srv = 0;
	rpc_srv_ouput->from_srv = rpc_srv.my_rank;

	rpc_srv_ouput->msg = strdup("M:Test Msg:= Hello from server");
	D_ASSERT(rpc_srv_ouput->msg != NULL);

	char *tmp = "Test Msg:= iov packet data from multitier server";

	d_iov_set(&rpc_srv_ouput->raw_pkg, tmp, strlen(tmp)+1);

	dbg("cmd:=0x%X\tstatus:=0x%X\n"
	"\t\t\t\tmsg:=%s\traw_package:=%s\n",
	rpc_srv_ouput->to_srv,
	rpc_srv_ouput->from_srv,
	rpc_srv_ouput->msg,
	(char *)rpc_srv_ouput->raw_pkg.iov_buf);

	dbg("<---%s---", __func__);
}

void
srv_common_cb(crt_rpc_t *rpc_req)
{
	int	rc = 0;

	dbg("---%s--->", __func__);

	dbg("client has connected to server[%u]\n", rpc_srv.my_rank);

	dbg("rpc_req->cr_opc:0x%X\n", rpc_req->cr_opc);

	switch (rpc_req->cr_opc) {
	case CRT_RPC_TEST_IO:
		dbg("CRT_RPC_TEST_IO_OP\n");
		crt_srv_io_op_cb(rpc_req);
		break;
	case CRT_RPC_MULTITIER_TEST_IO:
		dbg("CRT_RPC_MULTITIER_TEST_IO\n");
		crt_srv_io_op_cb(rpc_req);
		break;
	case CRT_RPC_MULTITIER_TEST_NO_IO:
		dbg("CRT_RPC_MULTITIER_TEST_NO_IO\n");
		break;
	case CRT_RPC_TEST_SHUTDOWN:
		dbg("CRT_RPC_TEST_SHUTDOWN");
		D_ASSERT(rpc_req->cr_input == NULL);
		D_ASSERT(rpc_req->cr_output == NULL);
		rpc_srv.shutdown = 1;
		goto exit;
		/* break; */
	default:
		dbg("Invalid opcode\n");
	}

	rc = crt_reply_send(rpc_req);
	D_ASSERTF(rc == 0, "crt_reply_send failed %d\n", rc);

exit:

	dbg("<---%s---", __func__);
}

static void
*progress_handler(void *data)
{
	int	rc = 0;

	crt_context_t *p_ctx = (crt_context_t *)data;

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
	int		rc = 0;

	dbg("---%s--->", __func__);

	rc = crt_context_destroy(rpc_srv.crt_ctx, 1);
	D_ASSERTF(rc == 0, "crt_context_destroy failed %d\n", rc);

	if (rpc_srv.my_rank == 0) {
		rc = crt_group_config_remove(NULL);
		assert(rc == 0);
	}

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize failed %d\n", rc);

	dbg("<---%s---", __func__);
}

static struct crt_proto_rpc_format my_proto_rpc_fmt_test_srv2[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_rpc_io,
		.prf_hdlr	= srv_common_cb,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= NULL,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= NULL,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= NULL,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= CRT_RPC_FEAT_NO_REPLY,
		.prf_req_fmt	= &CQF_crt_test_shutdown,
		.prf_hdlr	= srv_common_cb,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= NULL,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_multitier_test_io,
		.prf_hdlr	= srv_common_cb,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_multitier_test_no_io,
		.prf_hdlr	= srv_common_cb,
		.prf_co_ops	= NULL,
	}
};

static struct crt_proto_format my_proto_fmt_test_srv2 = {
	.cpf_name = "my-proto-test-srv2",
	.cpf_ver = TEST_RPC_COMMON_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_test_srv2),
	.cpf_prf = &my_proto_rpc_fmt_test_srv2[0],
	.cpf_base = TEST_RPC_COMMON_BASE,
};


int
srv_rpc_init(void)
{
	int	rc = 0;

	dbg("---%s--->", __func__);

	rc = crt_init(CRT_RPC_MULTITIER_GRPID, CRT_FLAG_BIT_SERVER |
					       CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	D_ASSERTF(rc == 0, "crt_init failed %d\n", rc);

	rc = crt_group_config_path_set(rpc_srv.config_path);
	D_ASSERTF(rc == 0, "crt_group_config_path_set failed %d\n", rc);

	rc  = crt_group_config_save(NULL, false);
	D_ASSERTF(rc == 0, "crt_group_config_save failed %d\n", rc);

	rc = crt_proto_register(&my_proto_fmt_test_srv2);
	D_ASSERTF(rc == 0, "crt_proto_register failed %d\n", rc);

	rc = crt_group_rank(NULL, &rpc_srv.my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank failed %d\n", rc);

	rc = crt_group_size(NULL, &rpc_srv.grp_size);
	D_ASSERTF(rc == 0, "crt_group_size failed %d\n", rc);

	rc = crt_context_create(&rpc_srv.crt_ctx);
	D_ASSERTF(rc == 0, " crt_context_create failed %d\n", rc);


	/* create progress thread */
	rc = pthread_create(&rpc_srv.progress_thid, NULL, progress_handler,
			&rpc_srv.crt_ctx);
	if (rc != 0)
		printf("progress thread creating failed, rc: %d.\n", rc);

	dbg("my_rank:=%u,group_size:=%i\n", rpc_srv.my_rank, rpc_srv.grp_size);

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

	dbg("srv2_pid:=%d", getpid());

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

	dbg("main thread wait progress thread ...\n");
	/* wait progress thread */
	if (pthread_join(rpc_srv.progress_thid, NULL) != 0)
		dbg("pthread_join failed rc:\n");

	srv_rpc_finalize();

	dbg("<---%s---", __func__);
	d_log_fini();

	return 0;
} /*main*/
