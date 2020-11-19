/*
 * (C) Copyright 2018-2020 Intel Corporation.
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
#include <semaphore.h>

#include "tests_common.h"
#include "test_ep_cred_common.h"

static void
test_run(d_rank_t my_rank)
{
	crt_init_options_t	 opt = {0};
	crt_group_t		*grp = NULL;
	uint32_t		 grp_size;
	int			 rc;

	DBG_PRINT("local group: %s remote group: %s\n",
		   test.tg_local_group_name, test.tg_remote_group_name);

	opt.cio_use_credits = 1;
	opt.cio_ep_credits = test.tg_credits;

	tc_srv_start_basic(test.tg_local_group_name, &test.tg_crt_ctx,
			   &test.tg_tid, &grp, &grp_size, &opt);

	DBG_PRINT("Server started, grp_size = %d\n", grp_size);
	rc = sem_init(&test.tg_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	rc = crt_proto_register(&my_proto_fmt_0);
	D_ASSERT(rc == 0);

	if (my_rank == 0) {
		rc = crt_group_config_save(NULL, true);
		D_ASSERTF(rc == 0,
			  "crt_group_config_save() failed. rc: %d\n", rc);
		DBG_PRINT("Group config saved\n");
	}

	rc = pthread_join(test.tg_tid, NULL);
	D_ASSERTF(rc == 0, "pthread_join failed. rc: %d\n", rc);
	DBG_PRINT("joined progress thread.\n");

	rc = sem_destroy(&test.tg_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	if (my_rank == 0) {
		rc = crt_group_config_remove(NULL);
		D_ASSERTF(rc == 0,
			  "crt_group_config_remove() failed. rc: %d\n", rc);
	}

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	d_log_fini();
	DBG_PRINT("exiting.\n");
}

int
main(int argc, char **argv)
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
	tc_test_init(my_rank, 40, true, true);

	test_run(my_rank);

	return rc;
}
