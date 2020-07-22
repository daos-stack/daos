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
#include "test_proto_common.h"

static void
test_run(d_rank_t my_rank)
{
	crt_group_t		*grp = NULL;
	uint32_t		 grp_size;
	int			 rc;

	fprintf(stderr, "local group: %s remote group: %s\n",
		test.tg_local_group_name, test.tg_remote_group_name);

	tc_srv_start_basic(test.tg_local_group_name, &test.tg_crt_ctx,
			   &test.tg_tid, &grp, &grp_size, NULL);

	rc = sem_init(&test.tg_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	if (test.tg_save_cfg && my_rank == 0) {
		rc = crt_group_config_path_set(test.tg_cfg_path);
		D_ASSERTF(rc == 0, "crt_group_config_path_set failed %d\n", rc);

		rc = crt_group_config_save(NULL, true);
		D_ASSERTF(rc == 0,
			  "crt_group_config_save() failed. rc: %d\n", rc);
	}

	switch (test.tg_num_proto) {
	case 4:
		rc = crt_proto_register(&my_proto_fmt_3);
		D_ASSERT(rc == 0);
	case 3:
		rc = crt_proto_register(&my_proto_fmt_2);
		D_ASSERT(rc == 0);
	case 2:
		rc = crt_proto_register(&my_proto_fmt_1);
		D_ASSERT(rc == 0);
	case 1:
		rc = crt_proto_register(&my_proto_fmt_0);
		D_ASSERT(rc == 0);
	default:
		break;
	}

	rc = pthread_join(test.tg_tid, NULL);
	D_ASSERTF(rc == 0, "pthread_join failed. rc: %d\n", rc);
	D_DEBUG(DB_TRACE, "joined progress thread.\n");

	rc = sem_destroy(&test.tg_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	if (test.tg_save_cfg) {
		rc = crt_group_config_remove(NULL);
		D_ASSERTF(rc == 0,
			  "crt_group_config_remove() failed. rc: %d\n", rc);
	}

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	d_log_fini();
	D_DEBUG(DB_TRACE, "exiting.\n");
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
