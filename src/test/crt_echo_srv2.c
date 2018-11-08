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

static int run_echo_srver_tier2(void)
{
	int			rc;

	/* create progress thread */
	rc = pthread_create(&echo_srv.progress_thread, NULL, progress_handler,
			    NULL);
	if (rc != 0) {
		printf("progress thread creating failed, rc: %d.\n", rc);
		goto out;
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

void
echo_srv_shutdown(crt_rpc_t *rpc_req)
{
	printf("tier2 echo_srver received shutdown request, opc: %#x.\n",
	       rpc_req->cr_opc);

	assert(rpc_req->cr_input == NULL);
	assert(rpc_req->cr_output == NULL);

	echo_srv.shutdown_by_client = 1;
	printf("tier2 echo_srver set shutdown flag.\n");
}

int g_roomno = 2082;
void echo_srv_checkin(crt_rpc_t *rpc_req)
{
	struct crt_echo_checkin_in *e_req;
	struct crt_echo_checkin_out *e_reply;

	/* CaRT internally already allocated the input/output buffer */
	e_req = crt_req_get(rpc_req);
	D_ASSERT(e_req != NULL);

	printf("tier2 echo_srver recv'd checkin, opc: %#x.\n",
	       rpc_req->cr_opc);
	printf("tier2 checkin input - age: %d, name: %s, days: %d.\n",
	       e_req->age, e_req->name, e_req->days);

	e_reply = crt_reply_get(rpc_req);
	D_ASSERT(e_reply != NULL);
	e_reply->ret = 0;
	e_reply->room_no = g_roomno++;

	crt_reply_send(rpc_req);

	printf("tier2 echo_srver sent checkin reply, ret: %d, room_no: %d.\n",
	       e_reply->ret, e_reply->room_no);
}

int main(int argc, char *argv[])
{
	echo_init(1, true);

	run_echo_srver_tier2();

	echo_fini();

	return 0;
}
