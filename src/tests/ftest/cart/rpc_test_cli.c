/*
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This is a simple example of rpc client based on crt APIs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#include "rpc_test_common.h"

/*========================== GLOBAL ===========================*/

struct rpc_test_cli rpc_cli;

/*========================== GLOBAL ===========================*/

void
crt_client_cb(const struct crt_cb_info *cb_info)
{
	struct crt_rpc_io_in	*rpc_cli_input =
				crt_req_get(cb_info->cci_rpc);
	struct crt_rpc_io_out	*rpc_srv_ouput =
				crt_reply_get(cb_info->cci_rpc);

	dbg("---%s--->", __func__);
	dbg("opc:%x\tcci_rc: %d\t-DER_TIMEDOUT:=%i\n",
	cb_info->cci_rpc->cr_opc, cb_info->cci_rc, -DER_TIMEDOUT);

	dbg("server has responded\n");

	switch (cb_info->cci_rpc->cr_opc) {
	case CRT_RPC_TEST_IO:
		dbg("CRT_RPC_TEST_IO\n");
		D_ASSERT(rpc_cli_input != NULL);
		D_ASSERT(rpc_srv_ouput != NULL);
		dbg("cmd:=0x%X\tstatus:=0x%X\n",
		rpc_cli_input->to_srv, rpc_cli_input->from_srv);

		dbg("cmd:=0x%X\tstatus:=0x%X\n"
			"\t\t\t\tmsg:=%s\traw_pkg:=%s\n",
			rpc_srv_ouput->to_srv,
			rpc_srv_ouput->from_srv,
			(char *)rpc_srv_ouput->msg,
			(char *)rpc_srv_ouput->raw_pkg.iov_buf);
		printf("\nRPC IO test %s with rc:=%d\n\n",
			((cb_info->cci_rc == 0) ? ("Passed") :
			("Failed")), cb_info->cci_rc);
		break;
	case CRT_RPC_TEST_ERR:
		dbg("CRT_RPC_TEST_ERR");
		D_ASSERT(rpc_cli_input != NULL);
		dbg("RPC return code:%d\t-DER_NOREPLY:=%i",
		cb_info->cci_rc, -DER_NOREPLY);

		dbg(
			"cmd:=0x%X\tstatus:=0x%X\traw_pkg:=%s\n",
			rpc_cli_input->to_srv,
			rpc_cli_input->from_srv,
			(char *)rpc_cli_input->raw_pkg.iov_buf);
		printf("\nRPC Error test %s with rc:=%d\n\n",
			((cb_info->cci_rc == -DER_NOREPLY) ?
			("Passed") : ("Failed")), cb_info->cci_rc);
		break;
	case CRT_RPC_TEST_NO_IO:
		dbg("CRT_RPC_TEST_NO_IO\n");
		printf("\nRPC NO IO test %s with rc:=%d\n\n",
			((cb_info->cci_rc == 0) ? ("Passed") :
			("Failed")), cb_info->cci_rc);
		break;
	case CRT_RPC_TEST_TIMEOUT:
		dbg("CRT_RPC_TEST_TIMEOUT");
		printf("\nRPC timeout test %s with rc:=%d\n\n",
			((cb_info->cci_rc == -DER_TIMEDOUT) ?
			("Passed") : ("Failed")), cb_info->cci_rc);
		break;
	case CRT_RPC_MULTITIER_TEST_IO:
		dbg("CRT_RPC_MULTITIER_TEST_IO");
		printf("\nRPC multitier IO test %s with rc:=%d\n\n",
		((cb_info->cci_rc == 0) ? ("Passed") : ("Failed")),
		cb_info->cci_rc);
		break;
	case CRT_RPC_TEST_SHUTDOWN:
		dbg("CRT_RPC_TEST_SHUTDOWN");
		printf("\nRPC without reply test %s with rc:=%d\n\n",
		((cb_info->cci_rc == 0) ? ("Passed") : ("Failed")),
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
	sem_post(&rpc_cli.cli_sem);

	dbg("<---%s---", __func__);

}

static void
*cli_progress_handler(void *data)
{
	crt_context_t	*p_ctx = (crt_context_t *)data;
	int		 rc = 0;

	dbg("---%s--->", __func__);

	D_ASSERTF(p_ctx != NULL, "p_ctx:=%p\n", p_ctx);

	while (rpc_cli.shutdown == 0) {
		rc = crt_progress(*p_ctx, 1000);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("crt_progress failed %d\n", rc);
			break;
		}
	}

	dbg("<---%s---", __func__);

	return NULL;
}

static void
cli_rpc_finalize(void)
{
	int	rc = 0;

	dbg("---%s--->", __func__);

	rpc_cli.shutdown = 1;

	sleep(3);

	if (rpc_cli.progress_thid)
		pthread_join(rpc_cli.progress_thid, NULL);

	rc = crt_context_destroy(rpc_cli.crt_ctx, 1);
	D_ASSERTF(rc == 0, "crt_context_destroy failed %d", rc);

	rc = sem_destroy(&rpc_cli.cli_sem);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize failed %d", rc);

	dbg("<---%s---", __func__);
}

void
cli_sem_timedwait(sem_t *sem, int sec)
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

void
create_rpc(crt_group_t *tgt_grp, d_rank_t rank, crt_opcode_t opc,
		crt_rpc_t **req)
{
	crt_endpoint_t	svr_ep;
	int		rc = 0;

	dbg("---%s--->", __func__);

	svr_ep.ep_grp = tgt_grp;
	svr_ep.ep_rank = rank;
	svr_ep.ep_tag = 0;

	rc = crt_req_create(rpc_cli.crt_ctx, &svr_ep, opc, req);
	D_ASSERTF(rc == 0, "crt_req_create failed %d", rc);

	dbg("<---%s---", __func__);

}

void
send_rpc_req(crt_rpc_t *req, crt_cb_t cli_cb_fun)
{
	int	rc = 0;
	int	op_complete;

	dbg("---%s--->", __func__);

	op_complete = 0;
	rc = crt_req_send(req, cli_cb_fun, &op_complete);
	D_ASSERTF(rc == 0, " crt_req_send failed %d", rc);

	cli_sem_timedwait(&rpc_cli.cli_sem, 61);

	dbg("completion flag has been set to %d\n", op_complete);

	dbg("<---%s---", __func__);

}

int
send_shutdown_to_srv(void)
{
	crt_rpc_t	*rpc_req = NULL;
	int		 rc = 0;
	int		 tgs = 0;
	int		 rank = 0;

	dbg("---%s--->", __func__);


	/*shutdown with no reply*/

	for (tgs = rpc_cli.target_grp_size; tgs >= 0; tgs--) {
		for (rank = 0; rank < rpc_cli.grp_size[tgs]; rank++) {

			dbg("sending shutdown to target_grp[%u].rank[%u]",
			tgs, rank);
			create_rpc(rpc_cli.target_group[tgs], rank,
			CRT_RPC_TEST_SHUTDOWN, &rpc_req);
			send_rpc_req(rpc_req, crt_client_cb);
		}

		dbg("target_grp[%u].grp_size[%u]\trank=%i",
		tgs, rpc_cli.grp_size[tgs], rank);
		rc = crt_group_detach(rpc_cli.target_group[tgs]);
		dbg("detached from target_grp[%u]==%s with rc:=%d",
		tgs, rpc_cli.target_group[tgs]->cg_grpid, rc);
		D_ASSERTF(rc == 0, "crt_group_detach failed %d", rc);
	}

	dbg("<---%s---", __func__);

	return rc;
}

void
rpc_no_io_test(void)
{
	crt_rpc_t	*rpc_req = NULL;

	dbg("---%s--->", __func__);

	create_rpc(rpc_cli.target_group[0], 0, CRT_RPC_TEST_NO_IO, &rpc_req);
	send_rpc_req(rpc_req, crt_client_cb);

	dbg("---%s--->", __func__);
}

void
rpc_io_test(void)
{
	struct crt_rpc_io_in	*cli_rpc_input;
	crt_rpc_t		*rpc_req = NULL;

	dbg("---%s--->", __func__);

	create_rpc(rpc_cli.target_group[0], 0, CRT_RPC_TEST_IO, &rpc_req);

	cli_rpc_input = crt_req_get(rpc_req);
	D_ASSERT(cli_rpc_input != NULL);
	cli_rpc_input->to_srv = CRT_RPC_TEST_IO;
	cli_rpc_input->from_srv = 0;
	cli_rpc_input->msg = strdup("Test Msg:= RPC IO TEST");
	D_ASSERT(cli_rpc_input->msg != NULL);

	char *tmpio = "Test Msg:= iov packet data from client";

	d_iov_set(
		&cli_rpc_input->raw_pkg,
		tmpio,
		strlen(tmpio) + 1);

	dbg("cmd:=0x%X\tstatus:=0x%X\n"
	"\t\t\t\tmsg:=%s\traw_pkg:=%s\n",
	cli_rpc_input->to_srv,
	cli_rpc_input->from_srv,
	cli_rpc_input->msg,
	(char *)cli_rpc_input->raw_pkg.iov_buf);

	send_rpc_req(rpc_req, crt_client_cb);

	if (cli_rpc_input->msg)
		free(cli_rpc_input->msg);


	dbg("---%s--->", __func__);
}

void
rpc_timeout_test(void)
{
	struct crt_rpc_io_in	*cli_rpc_input;
	crt_rpc_t		*rpc_req = NULL;
	int			 rc = 0;

	dbg("---%s--->", __func__);

	create_rpc(rpc_cli.target_group[0], 0,
		CRT_RPC_TEST_TIMEOUT, &rpc_req);

	cli_rpc_input = crt_req_get(rpc_req);
	D_ASSERT(cli_rpc_input != NULL);

	cli_rpc_input->to_srv = rpc_cli.timeout;
	cli_rpc_input->from_srv = 0;

	char *tmptmout = "Test Msg:= sending timeout value from client";

	d_iov_set(
		&cli_rpc_input->raw_pkg,
		tmptmout,
		strlen(tmptmout)+1);

	dbg("timeout:=0x%X\tstatus:=0x%X\n"
	"\t\t\t\traw_pkg:=%s\n",
	cli_rpc_input->to_srv,
	cli_rpc_input->from_srv,
	(char *)cli_rpc_input->raw_pkg.iov_buf);

	rc = crt_req_set_timeout(rpc_req, rpc_cli.timeout);
	dbg("crt_req_set_timeout rc:=%d", rc);
	D_ASSERT(rc == 0);
	send_rpc_req(rpc_req, crt_client_cb);


	dbg("---%s--->", __func__);
}

void
rpc_err_test(void)
{
	struct crt_rpc_io_in	*cli_rpc_input;
	crt_rpc_t		*rpc_req = NULL;


	dbg("---%s--->", __func__);

	create_rpc(rpc_cli.target_group[0], 0, CRT_RPC_TEST_ERR, &rpc_req);

	cli_rpc_input = crt_req_get(rpc_req);
	D_ASSERT(cli_rpc_input != NULL);

	char *tmperr = "Test Msg:= checking error from client";

	d_iov_set(
		&cli_rpc_input->raw_pkg,
		tmperr,
		strlen(tmperr)+1);

	dbg("cmd:=0x%X\tstatus:=0x%X\n"
	"\t\t\t\traw_pkg:=%s\n",
	cli_rpc_input->to_srv,
	cli_rpc_input->from_srv,
	(char *)cli_rpc_input->raw_pkg.iov_buf);

	 send_rpc_req(rpc_req, crt_client_cb);

	dbg("---%s--->", __func__);
}

void
rpc_multitier_io_test(void)
{
	struct crt_rpc_io_in	*cli_rpc_input;
	crt_rpc_t		*rpc_req = NULL;

	dbg("---%s--->", __func__);

	create_rpc(rpc_cli.target_group[1], 0,
	CRT_RPC_MULTITIER_TEST_IO, &rpc_req);

	cli_rpc_input = crt_req_get(rpc_req);
	D_ASSERT(cli_rpc_input != NULL);
	cli_rpc_input->to_srv = CRT_RPC_MULTITIER_TEST_IO;
	cli_rpc_input->from_srv = 0;
	cli_rpc_input->msg = strdup(
	"Test Msg:= RPC Multitier IO test");
	D_ASSERT(cli_rpc_input->msg != NULL);

	char *tmpmultitier = "Test Msg:= iov packet data from client";

	d_iov_set(&cli_rpc_input->raw_pkg,
	tmpmultitier, strlen(tmpmultitier) + 1);

	dbg("cmd:=0x%X\tstatus:=0x%X\n"
	"\t\t\t\tmsg:=%s\traw_pkg:=%s\n",
	cli_rpc_input->to_srv,
	cli_rpc_input->from_srv,
	cli_rpc_input->msg,
	(char *)cli_rpc_input->raw_pkg.iov_buf);

	send_rpc_req(rpc_req, crt_client_cb);

	if (cli_rpc_input->msg)
		free(cli_rpc_input->msg);

	dbg("---%s--->", __func__);
}

void
single_rpc_test(void)
{

	dbg("---%s--->", __func__);

	/* NOIO */
	rpc_no_io_test();

	/* IO */

	rpc_io_test();

	/* TIMEOUT */

	rpc_timeout_test();

	/* RPC_ERROR */

	rpc_err_test();

	/* MULTITIER IO */
	if (rpc_cli.target_group[1])
		rpc_multitier_io_test();

	dbg("<--%s---", __func__);

}


static struct crt_proto_rpc_format my_proto_rpc_fmt_cli[] = {
	{
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_rpc_io,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_test_no_io,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_test_err,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_test_timeout,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= CRT_RPC_FEAT_NO_REPLY,
		.prf_req_fmt	= &CQF_crt_test_shutdown,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= NULL,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= &CQF_crt_multitier_test_io,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= 0,
		.prf_req_fmt	= NULL,
		.prf_hdlr	= NULL,
		.prf_co_ops	= NULL,
	}
};

static struct crt_proto_format my_proto_fmt_cli = {
	.cpf_name = "my-proto-cli",
	.cpf_ver = TEST_RPC_COMMON_VER,
	.cpf_count = ARRAY_SIZE(my_proto_rpc_fmt_cli),
	.cpf_prf = &my_proto_rpc_fmt_cli[0],
	.cpf_base = TEST_RPC_COMMON_BASE,
};

void
cli_rpc_init(void)
{
	int	rc = 0;
	int	tgs = 0;

	dbg("---%s--->", __func__);

	rc = crt_init(NULL, 0);
	D_ASSERTF(rc == 0, "crt_init failed %d\n", rc);

	crt_context_create(&rpc_cli.crt_ctx);
	D_ASSERTF(rc == 0, "crt_context_create failed %d\n", rc);

	rc = pthread_create(&rpc_cli.progress_thid, NULL,
		cli_progress_handler, &rpc_cli.crt_ctx);

	D_ASSERT(rc == 0 || !rpc_cli.progress_thid);

	rc = crt_proto_register(&my_proto_fmt_cli);
	D_ASSERTF(rc == 0, "crt_proto_register failed %d\n", rc);

	rc = sem_init(&rpc_cli.cli_sem, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.%d\n", rc);

	rc = crt_group_config_path_set(rpc_cli.config_path);
	D_ASSERTF(rc == 0, "crt_group_config_path_set failed %d\n", rc);

	rpc_cli.target_group[rpc_cli.target_grp_size] = NULL;

	/* try until success to avoid failures until server isn't ready . */
	do {
		sleep(1);
		rc = crt_group_attach(CRT_DEFAULT_GRPID,
			&rpc_cli.target_group[rpc_cli.target_grp_size]);
		dbg("Attaching to default server grp\n");

	} while (rc != 0);

	rc = crt_group_size(rpc_cli.target_group[rpc_cli.target_grp_size],
				&rpc_cli.grp_size[rpc_cli.target_grp_size]);
	D_ASSERTF(rc == 0, "crt_group_size failed %d\n", rc);

	rpc_cli.target_grp_size += 1;
	rpc_cli.target_group[rpc_cli.target_grp_size] = NULL;

	/* try until success to avoid failures until server isn't ready . */
	do {
		sleep(1);
		rc = crt_group_attach(CRT_RPC_MULTITIER_GRPID,
			&rpc_cli.target_group[rpc_cli.target_grp_size]);

		dbg("Attaching to multitier server grp\n");

	} while (rc != 0);
	if (rc == 0) {

		rc = crt_group_size(
			rpc_cli.target_group[rpc_cli.target_grp_size],
			&rpc_cli.grp_size[rpc_cli.target_grp_size]);
		D_ASSERTF(rc == 0, "crt_group_size failed %d\n", rc);

	} else {
		dbg("failed to attach to %s with rc :=%d",
			CRT_RPC_MULTITIER_GRPID, rc);
		rpc_cli.target_group[rpc_cli.target_grp_size] = NULL;
		rpc_cli.target_grp_size -= 1;
	}

	dbg("target_grp_size:=%d", rpc_cli.target_grp_size);

	for (tgs = rpc_cli.target_grp_size; tgs >= 0; tgs--) {
		dbg("target_grp_size[%u].grp size[%u]:=%u",
		tgs, tgs, rpc_cli.grp_size[tgs]);
	}

	dbg("<---%s---", __func__);

}

void
print_usage(char *argv[])
{
	dbg("---%s--->", __func__);

	printf("Usage:%s\n", ((char *)strrchr(argv[0], '/') + 1));
	printf("OPTIONS:\n");
	printf("-c config path\n");
	printf("-t timeout value\n");

	dbg("<---%s---", __func__);
}

int
main(int argc, char *argv[])
{
	int	ch;

	assert(d_log_init() == 0);
	dbg("---%s--->", __func__);

	dbg("cli_pid:=%d", getpid());

	dbg("argc:=%d\n", argc);

	if (argc <= 1) {
		print_usage(argv);
		exit(1);
	}

	while ((ch = getopt(argc, argv, "c:t:f:")) != -1) {
		switch (ch) {
		case 'c':
			dbg("-c:=%s\n", optarg);
			snprintf(rpc_cli.config_path, 256, "%s", optarg);
			break;
		case 'f':
			dbg("-f:=%s\n", optarg);
			snprintf(rpc_cli.test_file_path, 256, "%s", optarg);
			break;
		case 't':
			dbg("-t:=%s\n", optarg);
			rpc_cli.timeout = atoi(optarg);
			break;
		default:
			dbg("default\n");
			print_usage(argv);
			exit(1);
		}
	}

	dbg("rpc_cli.config_path: = %s rpc_cli.timeout = %d",
	rpc_cli.config_path, rpc_cli.timeout);

	/* default values */

	rpc_cli.target_grp_size = 0;

	cli_rpc_init();
	single_rpc_test();
	send_shutdown_to_srv();
	cli_rpc_finalize();

	dbg("<---%s---", __func__);
	d_log_fini();
	return 0;
} /* main */
