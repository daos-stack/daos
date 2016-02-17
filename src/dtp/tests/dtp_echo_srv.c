/**
 * All rights reecho_srved. This program and the accompanying materials
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
	int rc;
	assert(arg == NULL);
	/* progress loop */
	do {
		rc = dtp_progress(gecho.dtp_ctx, 1, NULL, NULL, NULL);
		if (rc != 0 && rc != -ETIMEDOUT) {
			printf("dtp_progress failed rc: %d.\n", rc);
			break;
		}
	} while (!echo_srv.do_shutdown);

	printf("progress_handler: rc: %d, echo_srv.do_shutdown: %d.\n",
	       rc, echo_srv.do_shutdown);
	printf("progress_handler: progress thread exit ...\n");

	pthread_exit(NULL);
}

static int run_echo_srver(void)
{
	int rc = 0;

	echo_srv.do_shutdown = 0;

	/* create progress thread */
	rc = pthread_create(&echo_srv.progress_thread, NULL, progress_handler,
			    NULL);
	if (rc != 0) {
		printf("progress thread creating failed, rc: %d.\n", rc);
		goto out;
	}

	printf("main thread wait progress thread ...\n");
	/* wait progress thread */
	rc = pthread_join(echo_srv.progress_thread, NULL);
	if (rc != 0)
		printf("pthread_join failed rc: %d.\n", rc);

out:
	printf("echo_srver shuting down ...\n");
	return rc;
}

int echo_srv_shutdown(dtp_rpc_t *rpc)
{
	int rc = 0;

	printf("echo_srver received shutdown request, opc: 0x%x.\n",
	       rpc->dr_opc);

	assert(rpc->dr_input == NULL);
	assert(rpc->dr_output == NULL);

	rc = dtp_reply_send(rpc, 0, NULL, NULL);
	printf("echo_srver done issuing shutdown responses.\n");

	echo_srv.do_shutdown = 1;
	printf("echo_srver set shutdown flag.\n");

	return rc;
}

int echo_srv_checkin(dtp_rpc_t *rpc)
{
	echo_checkin_in_t	*checkin_input = NULL;
	echo_checkin_out_t	*checkin_output = NULL;
	int			rc = 0;

	/* dtp internally already allocated the input/output buffer */
	checkin_input = (echo_checkin_in_t *)rpc->dr_input;
	assert(checkin_input != NULL);
	checkin_output = (echo_checkin_out_t *)rpc->dr_output;
	assert(checkin_output != NULL);

	printf("echo_srver recv'd checkin, opc: 0x%x.\n", rpc->dr_opc);
	printf("checkin input - age: %d, name: %s, days: %d.\n",
		checkin_input->age, checkin_input->name, checkin_input->days);

	checkin_output->ret = 0;
	checkin_output->room_no = 1082;

	rc = dtp_reply_send(rpc, 0, NULL, NULL);

	printf("echo_srver sent checkin reply, ret: %d, room_no: %d.\n",
	       checkin_output->ret, checkin_output->room_no);

	return rc;
}

static void usage()
{
	fputs("usage: ./hg_test_iv_echo_srver <<uri>\n"
	      "  uri  - listen  address\n", stderr);
	exit(1);
}

int main(int argc, char *argv[])
{
	if (argc != 2)
		usage();

	gecho.uri = argv[1];
	printf("listening uri: %s.\n", gecho.uri);
	echo_init(1);

	run_echo_srver();

	echo_fini();

	return 0;
}
