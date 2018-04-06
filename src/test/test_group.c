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
 *
 * This is a simple example of crt echo rpc group test based on crt APIs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <getopt.h>
#include <semaphore.h>

#include <gurt/common.h>
#include <cart/api.h>
#include "crt_fake_events.h"

#define TEST_CTX_MAX_NUM	 (72)

#define ECHO_OPC_CHECKIN    (0xA1)
#define ECHO_OPC_SHUTDOWN   (0x100)

struct test_group_t {
	crt_group_t	*tg_local_group;
	crt_group_t	*tg_remote_group;
	char		*tg_local_group_name;
	char		*tg_remote_group_name;
	uint32_t	 tg_remote_group_size;
	d_rank_t	 tg_my_rank;
	uint32_t	 tg_should_attach:1,
			 tg_shutdown:1,
			 tg_complete:1;
	int		 tg_is_service;
	int		 tg_infinite_loop;
	int		 tg_hold;
	uint32_t	 tg_hold_time;
	unsigned int	 tg_ctx_num;
	crt_context_t	 tg_crt_ctx[TEST_CTX_MAX_NUM];
	int		 tg_thread_id[TEST_CTX_MAX_NUM]; /* logical tid */
	pthread_t	 tg_tid[TEST_CTX_MAX_NUM];
	sem_t		 tg_token_to_proceed;
	int		 tg_roomno;
};

struct test_group_t test = { .tg_hold_time = 0, .tg_ctx_num = 1,
			     .tg_roomno = 1082 };

struct crt_msg_field *echo_ping_checkin[] = {
	&CMF_UINT32,
	&CMF_UINT32,
	&CMF_STRING,
};
struct crt_echo_checkin_req {
	int		age;
	int		days;
	d_string_t	name;
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

static inline void
test_sem_timedwait(sem_t *sem, int sec, int line_number)
{
	struct timespec			deadline;
	int				rc;

	rc = clock_gettime(CLOCK_REALTIME, &deadline);
	D_ASSERTF(rc == 0, "clock_gettime() failed at line %d rc: %d\n",
		  line_number, rc);
	deadline.tv_sec += sec;
	rc = sem_timedwait(sem, &deadline);
	D_ASSERTF(rc == 0, "sem_timedwait() failed at line %d rc: %d\n",
		  line_number, rc);
}

void
echo_checkin_handler(crt_rpc_t *rpc_req)
{
	struct crt_echo_checkin_req	*e_req;
	struct crt_echo_checkin_reply	*e_reply;
	int				 rc = 0;

	/* CaRT internally already allocated the input/output buffer */
	e_req = crt_req_get(rpc_req);
	D_ASSERTF(e_req != NULL, "crt_req_get() failed. e_req: %p\n", e_req);

	printf("tier1 echo_server recv'd checkin, opc: %#x.\n",
	       rpc_req->cr_opc);
	printf("tier1 checkin input - age: %d, name: %s, days: %d.\n",
	       e_req->age, e_req->name, e_req->days);

	e_reply = crt_reply_get(rpc_req);
	D_ASSERTF(e_reply != NULL, "crt_reply_get() failed. e_reply: %p\n",
		  e_reply);
	e_reply->ret = 0;
	e_reply->room_no = test.tg_roomno++;

	rc = crt_reply_send(rpc_req);
	D_ASSERTF(rc == 0, "crt_reply_send() failed. rc: %d\n", rc);

	printf("tier1 echo_srver sent checkin reply, ret: %d, room_no: %d.\n",
	       e_reply->ret, e_reply->room_no);
}

void
client_cb_common(const struct crt_cb_info *cb_info)
{
	crt_rpc_t				*rpc_req;
	struct crt_echo_checkin_req		*rpc_req_input;
	struct crt_echo_checkin_reply		*rpc_req_output;

	rpc_req = cb_info->cci_rpc;

	if (cb_info->cci_arg != NULL)
		*(int *) cb_info->cci_arg = 1;

	switch (cb_info->cci_rpc->cr_opc) {
	case ECHO_OPC_CHECKIN:
		rpc_req_input = crt_req_get(rpc_req);
		if (rpc_req_input == NULL)
			return;
		rpc_req_output = crt_reply_get(rpc_req);
		if (rpc_req_output == NULL)
			return;
		if (cb_info->cci_rc != 0) {
			D_ERROR("rpc (opc: %#x) failed, rc: %d.\n",
				rpc_req->cr_opc, cb_info->cci_rc);
			D_FREE(rpc_req_input->name);
			break;
		}
		printf("%s checkin result - ret: %d, room_no: %d.\n",
		       rpc_req_input->name, rpc_req_output->ret,
		       rpc_req_output->room_no);
		D_FREE(rpc_req_input->name);
		sem_post(&test.tg_token_to_proceed);
		break;
	case ECHO_OPC_SHUTDOWN:
		test.tg_complete = 1;
		sem_post(&test.tg_token_to_proceed);
		break;
	default:
		break;
	}
}

static void *progress_thread(void *arg)
{
	crt_context_t	ctx;
	pthread_t	current_thread = pthread_self();
	int		num_cores = sysconf(_SC_NPROCESSORS_ONLN);
	cpu_set_t	cpuset;
	int		t_idx;
	int		rc;

	t_idx = *(int *)arg;
	CPU_ZERO(&cpuset);
	CPU_SET(t_idx % num_cores, &cpuset);
	pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);

	fprintf(stderr, "progress thread %d running on core %d...\n",
		t_idx, sched_getcpu());

	ctx = (crt_context_t)test.tg_crt_ctx[t_idx];
	/* progress loop */
	do {
		rc = crt_progress(ctx, 0, NULL, NULL);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("crt_progress failed rc: %d.\n", rc);
		}
		if (test.tg_shutdown == 1 && test.tg_complete == 1)
			break;
	} while (!dead);

	printf("progress_thread: rc: %d, echo_srv.do_shutdown: %d.\n",
	       rc, test.tg_shutdown);
	printf("progress_thread: progress thread exit ...\n");

	pthread_exit(NULL);
}

void echo_shutdown_handler(crt_rpc_t *rpc_req)
{
	printf("tier1 echo_srver received shutdown request, opc: %#x.\n",
	       rpc_req->cr_opc);

	D_ASSERTF(rpc_req->cr_input == NULL, "RPC request has invalid input\n");
	D_ASSERTF(rpc_req->cr_output == NULL, "RPC request output is NULL\n");

	test.tg_shutdown = 1;
	printf("tier1 echo_srver set shutdown flag.\n");
}

void
test_group_init(void)
{
	uint32_t	flag;
	int		i;
	int		rc = 0;

	fprintf(stderr, "local group: %s remote group: %s\n",
		test.tg_local_group_name, test.tg_remote_group_name);

	rc = sem_init(&test.tg_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	flag = test.tg_is_service ? CRT_FLAG_BIT_SERVER : 0;
	rc = crt_init(test.tg_local_group_name, flag);
	D_ASSERTF(rc == 0, "crt_init() failed, rc: %d\n", rc);

	rc = crt_group_rank(NULL, &test.tg_my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank() failed. rc: %d\n", rc);
	if (test.tg_is_service) {
		rc = crt_group_config_save(NULL, true);
		D_ASSERTF(rc == 0, "crt_group_config_save() failed. rc: %d\n",
			rc);
		crt_fake_event_init(test.tg_my_rank);
		D_ASSERTF(rc == 0, "crt_fake_event_init() failed. rc: %d\n",
			  rc);
	}

	/* register RPCs */
	if (test.tg_is_service) {
		rc = crt_rpc_srv_register(ECHO_OPC_CHECKIN, 0,
					  &CQF_ECHO_PING_CHECK,
					  echo_checkin_handler);
		D_ASSERTF(rc == 0, "crt_rpc_srv_register() failed. rc: %d\n",
			  rc);
		rc = crt_rpc_srv_register(ECHO_OPC_SHUTDOWN,
					  CRT_RPC_FEAT_NO_REPLY, NULL,
					  echo_shutdown_handler);
		D_ASSERTF(rc == 0, "crt_rpc_srv_register() failed. rc: %d\n",
			  rc);
	} else {
		rc = crt_rpc_register(ECHO_OPC_CHECKIN, 0,
				      &CQF_ECHO_PING_CHECK);
		D_ASSERTF(rc == 0, "crt_rpc_register() failed. rc: %d\n", rc);
		rc = crt_rpc_register(ECHO_OPC_SHUTDOWN, CRT_RPC_FEAT_NO_REPLY,
				      NULL);
		D_ASSERTF(rc == 0, "crt_rpc_register() failed. rc: %d\n", rc);
	}

	for (i = 0; i < test.tg_ctx_num; i++) {
		test.tg_thread_id[i] = i;
		rc = crt_context_create(&test.tg_crt_ctx[i]);
		D_ASSERTF(rc == 0, "crt_context_create() failed. rc: %d\n", rc);
		rc = pthread_create(&test.tg_tid[i], NULL, progress_thread,
				    &test.tg_thread_id[i]);
		D_ASSERTF(rc == 0, "pthread_create() failed. rc: %d\n", rc);
	}
	test.tg_complete = 1;
}

void
check_in(crt_group_t *remote_group, int rank)
{
	crt_rpc_t			*rpc_req = NULL;
	struct crt_echo_checkin_req	*rpc_req_input;
	crt_endpoint_t			 server_ep = {0};
	char				*buffer;
	int				 rc;

	server_ep.ep_grp = remote_group;
	server_ep.ep_rank = rank;
	rc = crt_req_create(test.tg_crt_ctx[0], &server_ep,
			ECHO_OPC_CHECKIN, &rpc_req);
	D_ASSERTF(rc == 0 && rpc_req != NULL, "crt_req_create() failed,"
			" rc: %d rpc_req: %p\n", rc, rpc_req);

	rpc_req_input = crt_req_get(rpc_req);
	D_ASSERTF(rpc_req_input != NULL, "crt_req_get() failed."
			" rpc_req_input: %p\n", rpc_req_input);
	D_ALLOC(buffer, 256);
	D_ASSERTF(buffer != NULL, "Cannot allocate memory.\n");
	snprintf(buffer,  256, "Guest %d", test.tg_my_rank);
	rpc_req_input->name = buffer;
	rpc_req_input->age = 21;
	rpc_req_input->days = 7;
	D_DEBUG(DB_TEST, "client(rank %d) sending checkin rpc with tag "
		"%d, name: %s, age: %d, days: %d.\n",
		test.tg_my_rank, server_ep.ep_tag, rpc_req_input->name,
		rpc_req_input->age, rpc_req_input->days);

	/* send an rpc, print out reply */
	rc = crt_req_send(rpc_req, client_cb_common, NULL);
	D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);
}

void
test_group_run(void)
{
	crt_group_t			*remote_group = NULL;
	int				 ii;
	int				 rc;

	if (!test.tg_should_attach)
		return;

	if (test.tg_is_service) {
		rc = crt_init(test.tg_local_group_name, 0);
		D_ASSERTF(rc == 0, "crt_init() failed. rc: %d\n", rc);
	}

	/* try until success to avoid intermittent failures under valgrind. */
	do {
		sleep(1);
		rc = crt_group_attach(test.tg_remote_group_name,
				      &test.tg_remote_group);
	} while (rc != 0);
	D_ASSERTF(rc == 0, "crt_group_attach failed, rc: %d\n", rc);
	D_ASSERTF(test.tg_remote_group != NULL, "NULL attached srv_grp\n");

	test.tg_complete = 0;
	remote_group = crt_group_lookup(test.tg_remote_group_name);
	D_ASSERTF(remote_group != NULL, "crt_group_lookup() failed. "
		  "remote_group = %p\n", remote_group);
	crt_group_size(remote_group, &test.tg_remote_group_size);
	fprintf(stderr, "size of %s is %d\n", test.tg_remote_group_name,
		test.tg_remote_group_size);

	for (ii = 0; ii < test.tg_remote_group_size; ii++)
		check_in(test.tg_remote_group, ii);

	for (ii = 0; ii < test.tg_remote_group_size; ii++)
		test_sem_timedwait(&test.tg_token_to_proceed, 61, __LINE__);

	while (test.tg_infinite_loop) {
		check_in(test.tg_remote_group, 1);
		test_sem_timedwait(&test.tg_token_to_proceed, 61, __LINE__);
	}
}

void
test_group_fini()
{
	int				 ii;
	crt_endpoint_t			 server_ep = {0};
	crt_rpc_t			*rpc_req = NULL;
	int				 rc = 0;

	if (test.tg_should_attach && test.tg_my_rank == 0) {
		/* client rank 0 tells all servers to shut down */
		for (ii = 0; ii < test.tg_remote_group_size; ii++) {
			server_ep.ep_grp = test.tg_remote_group;
			server_ep.ep_rank = ii;
			rc = crt_req_create(test.tg_crt_ctx[0], &server_ep,
					    ECHO_OPC_SHUTDOWN, &rpc_req);
			D_ASSERTF(rc == 0 && rpc_req != NULL,
				  "crt_req_create() failed. "
				  "rc: %d, rpc_req: %p\n", rc, rpc_req);
			rc = crt_req_send(rpc_req, client_cb_common, NULL);
			D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n",
				  rc);

			test_sem_timedwait(&test.tg_token_to_proceed, 61,
					   __LINE__);
		}
	}
	if (test.tg_should_attach) {
		rc = crt_group_detach(test.tg_remote_group);
		D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	}
	if (!test.tg_is_service)
		test.tg_shutdown = 1;

	for (ii = 0; ii < test.tg_ctx_num; ii++) {
		rc = pthread_join(test.tg_tid[ii], NULL);
		if (rc != 0)
			fprintf(stderr, "pthread_join failed. rc: %d\n", rc);
		D_DEBUG(DB_TEST, "joined progress thread.\n");
		rc = crt_context_destroy(test.tg_crt_ctx[ii], 1);
		D_ASSERTF(rc == 0, "crt_context_destroy() failed. rc: %d\n",
			  rc);
		D_DEBUG(DB_TEST, "destroyed crt_ctx.\n");
	}

	if (test.tg_is_service)
		crt_fake_event_fini(test.tg_my_rank);
	rc = sem_destroy(&test.tg_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");
	/* corresponding to the crt_init() in run_test_group() */
	if (test.tg_should_attach && test.tg_is_service) {
		rc = crt_finalize();
		D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);
	}
	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);
	D_DEBUG(DB_TEST, "exiting.\n");

}

int
test_group_parse_args(int argc, char **argv)
{
	int				option_index = 0;
	int				rc = 0;
	struct option			long_options[] = {
		{"name", required_argument, 0, 'n'},
		{"attach_to", required_argument, 0, 'a'},
		{"holdtime", required_argument, 0, 'h'},
		{"hold", no_argument, &test.tg_hold, 1},
		{"is_service", no_argument, &test.tg_is_service, 1},
		{"ctx_num", required_argument, 0, 'c'},
		{"loop", no_argument, &test.tg_infinite_loop, 1},
		{0, 0, 0, 0}
	};

	while (1) {
		rc = getopt_long(argc, argv, "n:a:c:h:", long_options,
				 &option_index);
		if (rc == -1)
			break;
		switch (rc) {
		case 0:
			if (long_options[option_index].flag != 0)
				break;
		case 'n':
			test.tg_local_group_name = optarg;
			break;
		case 'a':
			test.tg_remote_group_name = optarg;
			test.tg_should_attach = 1;
			break;
		case 'c': {
			unsigned int	nr;
			char		*end;

			nr = strtoul(optarg, &end, 10);
			if (end == optarg || nr == 0 || nr > TEST_CTX_MAX_NUM) {
				fprintf(stderr, "invalid ctx_num %d exceed "
					"[%d, %d], using 1 for test.\n", nr,
					1, TEST_CTX_MAX_NUM);
			} else {
				test.tg_ctx_num = nr;
				fprintf(stderr, "will create %d contexts.\n",
					nr);
			}
			break;
		}
		case 'h':
			test.tg_hold = 1;
			test.tg_hold_time = atoi(optarg);
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

	return 0;
}

int main(int argc, char **argv)
{
	int	rc;

	rc = test_group_parse_args(argc, argv);
	if (rc != 0) {
		fprintf(stderr, "test_group_parse_args() failed, rc: %d.\n",
			rc);
		return rc;
	}

	test_group_init();
	test_group_run();
	if (test.tg_hold)
		sleep(test.tg_hold_time);
	test_group_fini();

	return rc;
}
