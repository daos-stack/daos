/*
 * (C) Copyright 2018-2022 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <semaphore.h>

#include "crt_utils.h"
#include "test_proto_common.h"

static void
test_run(d_rank_t my_rank)
{
	crt_group_t		*grp = NULL;
	uint32_t		 grp_size;
	int			 rc;

	fprintf(stderr, "local group: %s remote group: %s\n",
		test.tg_local_group_name, test.tg_remote_group_name);

	rc = crtu_srv_start_basic(test.tg_local_group_name, &test.tg_crt_ctx, &test.tg_tid, &grp,
				  &grp_size, NULL, NULL);
	D_ASSERTF(rc == 0, "crtu_srv_start_basic() failed\n");

	rc = sem_init(&test.tg_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	/* START: FIXME: always save */
	if (my_rank == 0) {
		rc = crt_group_config_save(NULL, true);
		D_ASSERTF(rc == 0,
			  "crt_group_config_save() failed. rc: %d\n", rc);
	}
	/* END: FIXME: always save */

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

	if (my_rank == 0) {
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

	d_agetenv_str(&env_self_rank, "CRT_L_RANK");
	my_rank = atoi(env_self_rank);
	d_freeenv_str(&env_self_rank);

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(my_rank, 40, true, true);

	test_run(my_rank);

	return rc;
}
