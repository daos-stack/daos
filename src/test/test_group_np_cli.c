/* Copyright (C) 2016-2019 Intel Corporation
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
 * This is a simple example of cart test_group client running with no pmix.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <getopt.h>
#include <semaphore.h>

#include "tests_common.h"
#include "test_group_rpc.h"
#include "test_group_np_common.h"

void
test_run(void)
{
	crt_group_t	*grp = NULL;
	d_rank_list_t	*rank_list = NULL;
	d_rank_t	 rank;
	int		 tag;
	crt_endpoint_t	 server_ep = {0};
	crt_rpc_t	*rpc_req = NULL;
	int		 i;
	int		 rc = 0;

	if (test_g.t_save_cfg) {
		rc = crt_group_config_path_set(test_g.t_cfg_path);
		D_ASSERTF(rc == 0, "crt_group_config_path_set failed %d\n", rc);
	}

	tc_cli_start_basic(test_g.t_local_group_name,
			   test_g.t_remote_group_name,
			   &grp, &rank_list, &test_g.t_crt_ctx[0],
			   &test_g.t_tid[0], test_g.t_srv_ctx_num,
			   test_g.t_save_cfg);

	rc = sem_init(&test_g.t_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	/* register RPCs */
	rc = crt_proto_register(&my_proto_fmt_test_group2);
	D_ASSERTF(rc == 0, "crt_proto_register() failed. rc: %d\n",
			rc);

	rc = tc_wait_for_ranks(test_g.t_crt_ctx[0], grp, rank_list,
			    test_g.t_srv_ctx_num - 1, test_g.t_srv_ctx_num,
			    5, 150);
	D_ASSERTF(rc == 0, "wait_for_ranks() failed; rc=%d\n", rc);

	test_g.t_fault_attr_1000 = d_fault_attr_lookup(1000);
	test_g.t_fault_attr_5000 = d_fault_attr_lookup(5000);

	if (!test_g.t_shut_only) {

		for (i = 0; i < rank_list->rl_nr; i++) {
			rank = rank_list->rl_ranks[i];

			for (tag = 0; tag < test_g.t_srv_ctx_num; tag++) {
				DBG_PRINT("Sending rpc to %d:%d\n", rank, tag);
				check_in(grp, rank, tag);
			}
		}

		for (i = 0; i < rank_list->rl_nr; i++) {
			tc_sem_timedwait(&test_g.t_token_to_proceed, 61,
					 __LINE__);
		}
	}

	if (test_g.t_hold)
		sleep(test_g.t_hold_time);

	for (i = 0; i < rank_list->rl_nr; i++) {
		rank = rank_list->rl_ranks[i];

		server_ep.ep_grp = grp;
		server_ep.ep_rank = rank;
		rc = crt_req_create(test_g.t_crt_ctx[0], &server_ep,
				    CRT_PROTO_OPC(TEST_GROUP_BASE,
				    TEST_GROUP_VER, 1), &rpc_req);
		D_ASSERTF(rc == 0 && rpc_req != NULL,
			  "crt_req_create() failed. "
			  "rc: %d, rpc_req: %p\n", rc, rpc_req);
		rc = crt_req_send(rpc_req, client_cb_common, NULL);
		D_ASSERTF(rc == 0, "crt_req_send() failed. rc: %d\n", rc);

		tc_sem_timedwait(&test_g.t_token_to_proceed, 61, __LINE__);
	}


	D_FREE(rank_list->rl_ranks);
	D_FREE(rank_list);

	if (test_g.t_save_cfg) {
		rc = crt_group_detach(grp);
		D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	} else {
		rc = crt_group_view_destroy(grp);
		D_ASSERTF(rc == 0,
			  "crt_group_view_destroy() failed; rc=%d\n", rc);
	}

	g_shutdown = 1;

	rc = pthread_join(test_g.t_tid[0], NULL);
	if (rc != 0)
		fprintf(stderr, "pthread_join failed. rc: %d\n", rc);
	D_DEBUG(DB_TEST, "joined progress thread.\n");

	rc = sem_destroy(&test_g.t_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	D_DEBUG(DB_TEST, "exiting.\n");

	d_log_fini();
}

int main(int argc, char **argv)
{
	int		 rc;

	rc = test_parse_args(argc, argv);
	if (rc != 0) {
		fprintf(stderr, "test_parse_args() failed, rc: %d.\n", rc);
		return rc;
	}

	/* rank, num_attach_retries, is_server, assert_on_error */
	tc_test_init(0, 20, false, true);

	test_run();

	return rc;
}
