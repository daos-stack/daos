/* Copyright (C) 2016-2017 Intel Corporation
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
 *
 * This is a simple example of crt echo rpc group test based on crt APIs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <getopt.h>

#include <crt_util/common.h>
#include <crt_api.h>
#include "crt_fake_events.h"

#define ECHO_OPC_CHECKIN    (0xA1)
#define ECHO_OPC_SHUTDOWN   (0x100)

/* for service process: received shutdown command from client */
static int g_shutdown;
/* for client process: received shutdown confirmation from server */
static int g_complete;

pthread_t			 tid;
crt_context_t			 crt_ctx;
crt_rank_t			 myrank;
int				 should_attach;
uint32_t			 target_group_size;
crt_group_t			*srv_grp;

int g_roomno = 1082;
struct crt_msg_field *echo_ping_checkin[] = {
	&CMF_UINT32,
	&CMF_UINT32,
	&CMF_STRING,
};
struct crt_echo_checkin_req {
	int		age;
	int		days;
	crt_string_t	name;
};
struct crt_msg_field *echo_ping_checkout[] = {
	&CMF_INT,
	&CMF_UINT32,
};
struct crt_echo_checkin_reply {
	int		ret;
	uint32_t	room_no;
};
struct crt_req_format CQF_ECHO_PING_CHECK =
	DEFINE_CRT_REQ_FMT("ECHO_PING_CHECK", echo_ping_checkin,
			   echo_ping_checkout);

int echo_checkin_handler(crt_rpc_t *rpc_req)
{
	struct crt_echo_checkin_req	*e_req;
	struct crt_echo_checkin_reply	*e_reply;
	int				 rc = 0;

	/* CaRT internally already allocated the input/output buffer */
	e_req = crt_req_get(rpc_req);
	C_ASSERTF(e_req != NULL, "crt_req_get() failed. e_req: %p\n", e_req);

	printf("tier1 echo_srver recv'd checkin, opc: 0x%x.\n",
		rpc_req->cr_opc);
	printf("tier1 checkin input - age: %d, name: %s, days: %d.\n",
		e_req->age, e_req->name, e_req->days);

	e_reply = crt_reply_get(rpc_req);
	C_ASSERTF(e_reply != NULL, "crt_reply_get() failed. e_reply: %p\n",
		  e_reply);
	e_reply->ret = 0;
	e_reply->room_no = g_roomno++;

	rc = crt_reply_send(rpc_req);
	C_ASSERTF(rc == 0, "crt_reply_send() failed. rc: %d\n", rc);

	printf("tier1 echo_srver sent checkin reply, ret: %d, room_no: %d.\n",
	       e_reply->ret, e_reply->room_no);

	return rc;
}

int client_cb_common(const struct crt_cb_info *cb_info)
{
	crt_rpc_t				*rpc_req;
	struct crt_echo_checkin_req		*rpc_req_input;
	struct crt_echo_checkin_reply		*rpc_req_output;

	rpc_req = cb_info->cci_rpc;

	*(int *) cb_info->cci_arg = 1;

	switch (cb_info->cci_rpc->cr_opc) {
	case ECHO_OPC_CHECKIN:
		rpc_req_input = crt_req_get(rpc_req);
		if (rpc_req_input == NULL)
			return -CER_INVAL;
		rpc_req_output = crt_reply_get(rpc_req);
		if (rpc_req_output == NULL)
			return -CER_INVAL;
		printf("%s checkin result - ret: %d, room_no: %d.\n",
		       rpc_req_input->name, rpc_req_output->ret,
		       rpc_req_output->room_no);
		C_FREE(rpc_req_input->name, 256);
		break;
	case ECHO_OPC_SHUTDOWN:
		g_complete = 1;
		break;
	default:
		break;
	}

	return 0;
}

static void *progress_thread(void *arg)
{
	int			rc;
	crt_context_t		crt_ctx;

	crt_ctx = (crt_context_t) arg;
	/* progress loop */
	do {
		rc = crt_progress(crt_ctx, 1, NULL, NULL);
		if (rc != 0 && rc != -CER_TIMEDOUT) {
			C_ERROR("crt_progress failed rc: %d.\n", rc);
			break;
		}


		if (g_shutdown == 1 && g_complete == 1)
			break;
	} while (1);

	printf("progress_thread: rc: %d, echo_srv.do_shutdown: %d.\n",
	       rc, g_shutdown);
	printf("progress_thread: progress thread exit ...\n");

	pthread_exit(NULL);
}

int echo_shutdown_handler(crt_rpc_t *rpc_req)
{
	int		rc = 0;

	printf("tier1 echo_srver received shutdown request, opc: 0x%x.\n",
	       rpc_req->cr_opc);

	C_ASSERTF(rpc_req->cr_input == NULL, "RPC request has invalid input\n");
	C_ASSERTF(rpc_req->cr_output == NULL, "RPC request output is NULL\n");

	rc = crt_reply_send(rpc_req);
	printf("tier1 echo_srver done issuing shutdown responses.\n");

	g_shutdown = 1;
	printf("tier1 echo_srver set shutdown flag.\n");

	return rc;
}

void
test_group_init(char *local_group_name, char *target_group_name,
		int is_service)
{
	char				 hostname[1024];
	uint32_t			 flag;
	int				 rc = 0;

	gethostname(hostname, sizeof(hostname));
	fprintf(stderr, "Running on %s\n", hostname);


	fprintf(stderr, "local group: %s remote group: %s\n",
		local_group_name, target_group_name);
	flag = is_service ? CRT_FLAG_BIT_SERVER : 0;
	rc = crt_init(local_group_name, flag);
	C_ASSERTF(rc == 0, "crt_init() failed, rc: %d\n", rc);

	rc = crt_group_rank(NULL, &myrank);
	C_ASSERTF(rc == 0, "crt_group_rank() failed. rc: %d\n", rc);
	if (is_service) {
		crt_fake_event_init(myrank);
		C_ASSERTF(rc == 0, "crt_fake_event_init() failed. rc: %d\n",
			  rc);
	}

	rc = crt_context_create(NULL, &crt_ctx);
	C_ASSERTF(rc == 0, "crt_context_create() failed. rc: %d\n", rc);
	/* register RPCs */
	if (is_service) {
		rc = crt_rpc_srv_register(ECHO_OPC_CHECKIN,
				&CQF_ECHO_PING_CHECK, echo_checkin_handler);
		C_ASSERTF(rc == 0, "crt_rpc_srv_register() failed. rc: %d\n",
			  rc);
		rc = crt_rpc_srv_register(ECHO_OPC_SHUTDOWN, NULL,
				echo_shutdown_handler);
		C_ASSERTF(rc == 0, "crt_rpc_srv_register() failed. rc: %d\n",
			  rc);
	} else {
		rc = crt_rpc_register(ECHO_OPC_CHECKIN, &CQF_ECHO_PING_CHECK);
		C_ASSERTF(rc == 0, "crt_rpc_register() failed. rc: %d\n", rc);
		rc = crt_rpc_register(ECHO_OPC_SHUTDOWN, NULL);
		C_ASSERTF(rc == 0, "crt_rpc_register() failed. rc: %d\n", rc);
	}
	rc = pthread_create(&tid, NULL, progress_thread, crt_ctx);
	C_ASSERTF(rc == 0, "pthread_create() failed. rc: %d\n", rc);
	g_complete = 1;
}

void
run_test_group(char *local_group_name, char *target_group_name, int is_service)
{
	crt_group_t			*target_group = NULL;
	crt_rpc_t			*rpc_req = NULL;
	struct crt_echo_checkin_req	*rpc_req_input;
	crt_endpoint_t			 server_ep;
	char				 *buffer;
	int				 ii;
	int				 complete;
	int				 rc = 0;

	if (!should_attach)
		return;

	if (is_service) {
		rc = crt_init(local_group_name, 0);
		C_ASSERTF(rc == 0, "crt_rpc_srv_register() failed. rc: %d\n",
			  rc);
	} else {
		sleep(2);
	}
	rc = crt_group_attach(target_group_name, &srv_grp);
	C_ASSERTF(rc == 0, "crt_group_attach failed, rc: %d\n", rc);
	C_ASSERTF(srv_grp != NULL, "NULL attached srv_grp\n");
	g_complete = 0;
	target_group = crt_group_lookup(target_group_name);
	C_ASSERTF(target_group != NULL, "crt_group_lookup() failed. "
		  "target_group = %p\n", target_group);
	crt_group_size(target_group, &target_group_size);
	fprintf(stderr, "size of %s is %d\n", target_group_name,
		target_group_size);
	for (ii = 0; ii < target_group_size; ii++) {
		server_ep.ep_grp = srv_grp;
		server_ep.ep_rank = ii;
		server_ep.ep_tag = 0;
		rc = crt_req_create(crt_ctx, server_ep,
				ECHO_OPC_CHECKIN, &rpc_req);
		C_ASSERTF(rc == 0 && rpc_req != NULL, "crt_req_create() failed,"
			  " rc: %d rpc_req: %p\n", rc, rpc_req);

		rpc_req_input = crt_req_get(rpc_req);
		C_ASSERTF(rpc_req_input != NULL, "crt_req_get() failed."
			  " rpc_req_input: %p\n", rpc_req_input);
		C_ALLOC(buffer, 256);
		C_ASSERTF(buffer != NULL, "Cannot allocate memory.\n");
		snprintf(buffer,  256, "Guest %d", myrank);
		rpc_req_input->name = buffer;
		rpc_req_input->age = 21;
		rpc_req_input->days = 7;
		C_DEBUG("client(rank %d) sending checkin rpc with tag "
			"%d, name: %s, age: %d, days: %d.\n",
			myrank, server_ep.ep_tag, rpc_req_input->name,
			rpc_req_input->age, rpc_req_input->days);
		complete = 0;
		/* send an rpc, print out reply */
		rc = crt_req_send(rpc_req, client_cb_common, &complete);
		C_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);
	}
}

void
test_group_fini(int is_service)
{
	int				 ii;
	crt_endpoint_t			 server_ep;
	crt_rpc_t			*rpc_req = NULL;
	int				 complete;
	int				 rc = 0;

	if (should_attach && myrank == 0) {
		/* client rank 0 tells all servers to shut down */
		for (ii = 0; ii < target_group_size; ii++) {
			server_ep.ep_grp = srv_grp;
			server_ep.ep_rank = ii;
			server_ep.ep_tag = 0;
			rc = crt_req_create(crt_ctx, server_ep,
					ECHO_OPC_SHUTDOWN, &rpc_req);
			C_ASSERTF(rc == 0 && rpc_req != NULL,
				  "crt_req_create() failed. "
				  "rc: %d, rpc_req: %p\n", rc, rpc_req);
			complete = 0;
			rc = crt_req_send(rpc_req, client_cb_common, &complete);
			C_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n",
				  rc);
		}
	}
	if (should_attach) {
		rc = crt_group_detach(srv_grp);
		C_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	}
	if (!is_service)
		g_shutdown = 1;

	rc = pthread_join(tid, NULL);
	if (rc != 0)
		fprintf(stderr, "pthread_join failed. rc: %d\n", rc);
	C_DEBUG("joined progress thread.\n");
	rc = crt_context_destroy(crt_ctx, 0);
	C_ASSERTF(rc == 0, "crt_context_destroy() failed. rc: %d\n", rc);
	C_DEBUG("destroyed crt_ctx.\n");
	if (is_service)
		crt_fake_event_fini(myrank);
	rc = crt_finalize();
	C_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);
	C_DEBUG("exiting.\n");

}

int main(int argc, char **argv)
{
	int				 hold = 0;
	uint64_t			 hold_time = 5;
	char				*local_group_name = NULL;
	char				*target_group_name = NULL;
	int				 rc = 0;
	int				 option_index = 0;
	int				 is_service = 0;
	struct option			 long_options[] = {
		{"name", required_argument, 0, 'n'},
		{"attach_to", required_argument, 0, 'a'},
		{"holdtime", required_argument, 0, 'h'},
		{"hold", no_argument, &hold, 1},
		{"is_service", no_argument, &is_service, 1},
		{0, 0, 0, 0}
	};

	while (1) {
		rc = getopt_long(argc, argv, "n:a:", long_options,
				&option_index);
		if (rc == -1)
			break;
		switch (rc) {
		case 0:
			if (long_options[option_index].flag != 0)
				break;
		case 'n':
			local_group_name = optarg;
			break;
		case 'a':
			target_group_name = optarg;
			should_attach = 1;
			break;
		case 'h':
			hold = 1;
			hold_time = atoi(optarg);
			break;
		case '?':
			return 1;
		default:
			return 1;
		}
	}
	if (optind < argc) {
		fprintf(stderr, "non-option argv elements encountered");
		return 1;
	}
	rc = 0;

	test_group_init(local_group_name, target_group_name, is_service);
	run_test_group(local_group_name, target_group_name, is_service);
	if (hold)
		sleep(hold_time);
	test_group_fini(is_service);

	return rc;
}
