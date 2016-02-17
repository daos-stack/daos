/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015 Intel Corporation.
 */
/**
 * This is a simple example of dtp_echo rpc client based on dtp APIs.
 */

#include <dtp_echo.h>

struct gecho gecho;

static int client_wait(int num_retries, unsigned int wait_len_ms,
		       int *complete_flag)
{
	int retry, rc;
	for (retry = 0; retry < num_retries; retry++) {
		rc = dtp_progress(gecho.dtp_ctx, wait_len_ms, NULL, NULL,
				  NULL);
		if (rc != 0 && rc != -ETIMEDOUT) {
			printf("dtp_progress failed rc: %d.\n", rc);
			break;
		}
		if (*complete_flag)
			return 0;
	}
	return -ETIMEDOUT;
}

static int client_cb(const dtp_cb_info_t *cb_info)
{
	dtp_rpc_t	*rpc_req;

	rpc_req = cb_info->dci_rpc;

	/* set complete flag */
	printf("in client_cb, opc: 0x%x.\n", cb_info->dci_rpc->dr_opc);
	*(int *) cb_info->dci_arg = 1;

	if (cb_info->dci_rpc->dr_opc == ECHO_OPC_CHECKIN) {
		echo_checkin_in_t	*checkin_input;
		echo_checkin_out_t	*checkin_output;

		checkin_input = rpc_req->dr_input;
		checkin_output = rpc_req->dr_output;
		assert(checkin_input != NULL && checkin_output != NULL);
		printf("%s checkin result - ret: %d, room_no: %d.\n",
		       checkin_input->name, checkin_output->ret,
		       checkin_output->room_no);
	}

	return 0;
}

static void run_client(void)
{
	dtp_endpoint_t		svr_ep;
	dtp_rpc_t		*rpc_req = NULL;
	echo_checkin_in_t	*checkin_input = NULL;
	echo_checkin_out_t	*checkin_output = NULL;
	int			rc = 0;

	/* send checkin RPC: "32" years old "Tom" checkin, want "3" days */
	rc = dtp_req_create(gecho.dtp_ctx, svr_ep, ECHO_OPC_CHECKIN, &rpc_req);
	assert(rc == 0 && rpc_req != NULL);

	/*
	 * The dtp_req_create already allocated the input/output buffer
	 * based on the input_size/output_size per the opcode
	 */
	checkin_input = (echo_checkin_in_t *)rpc_req->dr_input;
	assert(checkin_input != NULL);
	checkin_output = (echo_checkin_out_t *)rpc_req->dr_output;
	assert(checkin_output != NULL);

	/*
	 * No strdup will cause mercury crash when HG_Free_input
	 * in dtp_hg_reply_send_cb
	 */
	checkin_input->name = strdup("Tom@IBM");
	checkin_input->age = 32;
	checkin_input->days = 3;

	printf("sending checkin request, name: %s, age: %d, days: %d.\n",
	       checkin_input->name, checkin_input->age, checkin_input->days);

	gecho.complete = 0;
	rc = dtp_req_send(rpc_req, 0, client_cb, &gecho.complete);
	assert(rc == 0);
	/* wait two minutes (in case of manually starting up clients) */
	rc = client_wait(120, 1000, &gecho.complete);
	assert(rc == 0);

	printf("client checkin request sent.\n");

	/* ====================== */

	/* ====================== */
	/* send an RPC to kill the server */
	printf("press enter to send shutdown request to server...\n");
	getchar();
	printf("client sending shutdown request...\n");
	gecho.complete = 0;
	rpc_req = NULL;
	rc = dtp_req_create(gecho.dtp_ctx, svr_ep, ECHO_OPC_SHUTDOWN, &rpc_req);
	assert(rc == 0 && rpc_req != NULL);

	assert(rpc_req->dr_input == NULL);
	assert(rpc_req->dr_output == NULL);

	rc = dtp_req_send(rpc_req, 0, client_cb, &gecho.complete);
	assert(rc == 0);

	rc = client_wait(100, 100, &gecho.complete);
	assert(rc == 0);

	printf("client shuting down...\n");
}

static void usage()
{
	fputs("usage: /hg_test_iv_client <uri>\n"
	      "  uri  connect address\n", stderr);
	exit(1);
}


int main(int argc, char *argv[])
{
	if (argc != 2)
		usage();

	gecho.uri = argv[1];

	printf("connecting to uri: %s.\n", argv[1]);

	echo_init(0);

	run_client();

	echo_fini();

	return 0;
}
