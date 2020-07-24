/*
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This is a simple example of cart test_group server, running with no pmix.
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
test_run(d_rank_t my_rank)
{
	crt_group_t		*grp = NULL;
	uint32_t		 grp_size;
	int			 i;
	int			 rc = 0;

	tc_srv_start_basic(test_g.t_local_group_name, &test_g.t_crt_ctx[0],
			   &test_g.t_tid[0], &grp, &grp_size, NULL);

	DBG_PRINT("Basic server started, group_size=%d\n", grp_size);
	rc = sem_init(&test_g.t_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	test_g.t_fault_attr_1000 = d_fault_attr_lookup(1000);
	test_g.t_fault_attr_5000 = d_fault_attr_lookup(5000);

	rc = crt_proto_register(&my_proto_fmt_test_group1);
	D_ASSERTF(rc == 0, "crt_proto_register() failed. rc: %d\n",
			rc);

	DBG_PRINT("Protocol registered\n");
	for (i = 1; i < test_g.t_srv_ctx_num; i++) {
		rc = crt_context_create(&test_g.t_crt_ctx[i]);
		D_ASSERTF(rc == 0, "crt_context_create() failed. rc: %d\n", rc);
		DBG_PRINT("Context %d created\n", i);

		rc = pthread_create(&test_g.t_tid[i], NULL, tc_progress_fn,
				    &test_g.t_crt_ctx[i]);
		D_ASSERTF(rc == 0, "pthread_create() failed. rc: %d\n", rc);
		DBG_PRINT("Progress thread %d started\n", i);
	}
	DBG_PRINT("Contexts created %d\n", test_g.t_srv_ctx_num);

	if (test_g.t_save_cfg && my_rank == 0) {
		rc = crt_group_config_path_set(test_g.t_cfg_path);
		D_ASSERTF(rc == 0, "crt_group_config_path_set failed %d\n", rc);

		rc = crt_group_config_save(NULL, true);
		D_ASSERTF(rc == 0,
			  "crt_group_config_save() failed. rc: %d\n", rc);
		DBG_PRINT("Group config file saved\n");
	}


	if (test_g.t_hold)
		sleep(test_g.t_hold_time);

	for (i = 0; i < test_g.t_srv_ctx_num; i++) {
		rc = pthread_join(test_g.t_tid[i], NULL);
		if (rc != 0)
			fprintf(stderr, "pthread_join failed. rc: %d\n", rc);
		D_DEBUG(DB_TEST, "joined progress thread.\n");
	}

	DBG_PRINT("Exiting server\n");
	rc = sem_destroy(&test_g.t_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	if (test_g.t_save_cfg && my_rank == 0) {
		rc = crt_group_config_remove(NULL);
		D_ASSERTF(rc == 0,
			  "crt_group_config_remove() failed. rc: %d\n", rc);
	}

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	d_log_fini();

	D_DEBUG(DB_TEST, "exiting.\n");
}

int main(int argc, char **argv)
{
	char		*env_self_rank;
	d_rank_t	 my_rank;
	int		 rc;

	rc = test_parse_args(argc, argv);
	if (rc != 0) {
		fprintf(stderr, "test_parse_args() failed, rc: %d.\n", rc);
		return rc;
	}

	env_self_rank = getenv("CRT_L_RANK");
	my_rank = atoi(env_self_rank);

	/* rank, num_attach_retries, is_server, assert_on_error */
	tc_test_init(my_rank, 20, true, true);

	DBG_PRINT("STARTING SERVER\n");
	test_run(my_rank);

	return rc;
}
