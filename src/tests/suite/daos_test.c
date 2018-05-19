/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_test
 */
#define D_LOGFAC	DD_FAC(tests)
#include <getopt.h>
#include "daos_test.h"

/** All tests in default order ('d' must be the last one) */
static const char *all_tests = "mpceiACoROdr";

/** Server crt group ID */
static const char *server_group;

/** Pool service replicas */
static unsigned int svc_nreplicas = 1;

static int
test_setup_pool_create(void **state, struct test_pool *pool)
{
	test_arg_t *arg = *state;
	int rc;

	if (pool != NULL) {
		/* copy the info from passed in pool */
		assert_int_equal(pool->slave, 0);
		arg->pool.pool_size = pool->pool_size;
		uuid_copy(arg->pool.pool_uuid, pool->pool_uuid);
		arg->pool.svc.rl_nr = pool->svc.rl_nr;
		memcpy(arg->pool.ranks, pool->ranks,
		       sizeof(arg->pool.ranks[0]) * TEST_RANKS_MAX_NUM);
		arg->pool.slave = 1;
		if (arg->multi_rank)
			MPI_Barrier(MPI_COMM_WORLD);
		return 0;
	}

	if (arg->myrank == 0) {
		char	*env;

		env = getenv("POOL_SIZE");
		if (env) {
			int size_gb;

			size_gb = atoi(env);
			if (size_gb != 0)
				arg->pool.pool_size =
					(daos_size_t)size_gb << 30;
		}

		print_message("setup: creating pool size="DF_U64" GB\n",
			      (arg->pool.pool_size >> 30));
		rc = daos_pool_create(0731, geteuid(), getegid(), arg->group,
				      NULL, "pmem", arg->pool.pool_size,
				      &arg->pool.svc, arg->pool.pool_uuid,
				      NULL);
		if (rc)
			print_message("daos_pool_create failed, rc: %d\n", rc);
		else
			print_message("setup: created pool "DF_UUIDF"\n",
				       DP_UUID(arg->pool.pool_uuid));
	}
	/** broadcast pool create result */
	if (arg->multi_rank) {
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		/** broadcast pool UUID and svc addresses */
		if (!rc) {
			MPI_Bcast(arg->pool.pool_uuid, 16,
				  MPI_CHAR, 0, MPI_COMM_WORLD);
			MPI_Bcast(&arg->pool.svc.rl_nr,
				  sizeof(arg->pool.svc.rl_nr),
				  MPI_CHAR, 0, MPI_COMM_WORLD);
			MPI_Bcast(arg->pool.ranks,
				  sizeof(arg->pool.ranks[0]) *
					 arg->pool.svc.rl_nr,
				  MPI_CHAR, 0, MPI_COMM_WORLD);
		}
	}
	return rc;
}

static int
test_setup_pool_connect(void **state, struct test_pool *pool)
{
	test_arg_t *arg = *state;
	int rc;

	if (pool != NULL) {
		assert_int_equal(arg->pool.slave, 1);
		assert_int_equal(pool->slave, 0);
		memcpy(&arg->pool.pool_info, &pool->pool_info,
		       sizeof(pool->pool_info));
		arg->pool.poh = pool->poh;
		if (arg->multi_rank)
			MPI_Barrier(MPI_COMM_WORLD);
		return 0;
	}

	if (arg->myrank == 0) {
		daos_pool_info_t	info;

		print_message("setup: connecting to pool\n");
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       &arg->pool.svc, DAOS_PC_RW,
				       &arg->pool.poh, &arg->pool.pool_info,
				       NULL /* ev */);
		if (rc)
			print_message("daos_pool_connect failed, rc: %d\n", rc);
		else
			print_message("connected to pool, ntarget=%d\n",
				      arg->pool.pool_info.pi_ntargets);

		if (rc == 0) {
			rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL);

			if (rc == 0) {
				arg->srv_ntgts = info.pi_ntargets;
				arg->srv_disabled_ntgts = info.pi_ndisabled;
			}
		}
	}

	/** broadcast pool connect result */
	if (arg->multi_rank) {
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		if (!rc) {
			/** broadcast pool info */
			MPI_Bcast(&arg->pool.pool_info,
				  sizeof(arg->pool.pool_info),
				  MPI_CHAR, 0, MPI_COMM_WORLD);
			/** l2g and g2l the pool handle */
			handle_share(&arg->pool.poh, HANDLE_POOL,
				     arg->myrank, arg->pool.poh, 0);
		}
	}
	return rc;
}

static int
test_setup_cont_create(void **state)
{
	test_arg_t *arg = *state;
	int rc;

	if (arg->myrank == 0) {
		uuid_generate(arg->co_uuid);
		print_message("setup: creating container "DF_UUIDF"\n",
			      DP_UUID(arg->co_uuid));
		rc = daos_cont_create(arg->pool.poh, arg->co_uuid, NULL);
		if (rc)
			print_message("daos_cont_create failed, rc: %d\n", rc);
	}
	/** broadcast container create result */
	if (arg->multi_rank) {
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		/** broadcast container UUID */
		if (!rc)
			MPI_Bcast(arg->co_uuid, 16,
				  MPI_CHAR, 0, MPI_COMM_WORLD);
	}
	return rc;
}


static int
test_setup_cont_open(void **state)
{
	test_arg_t *arg = *state;
	int rc;

	if (arg->myrank == 0) {
		print_message("setup: opening container\n");
		rc = daos_cont_open(arg->pool.poh, arg->co_uuid, DAOS_COO_RW,
				    &arg->coh, &arg->co_info, NULL);
		if (rc)
			print_message("daos_cont_open failed, rc: %d\n", rc);
	}
	/** broadcast container open result */
	if (arg->multi_rank) {
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		/** l2g and g2l the container handle */
		if (!rc)
			handle_share(&arg->coh, HANDLE_CO,
				     arg->myrank, arg->pool.poh, 0);
	}
	return rc;
}

int
test_setup_next_step(void **state, struct test_pool *pool)
{
	test_arg_t *arg = *state;

	switch (arg->setup_state) {
	default:
		arg->setup_state = SETUP_EQ;
		return daos_eq_create(&arg->eq);
	case SETUP_EQ:
		arg->setup_state = SETUP_POOL_CREATE;
		return test_setup_pool_create(state, pool);
	case SETUP_POOL_CREATE:
		arg->setup_state = SETUP_POOL_CONNECT;
		return test_setup_pool_connect(state, pool);
	case SETUP_POOL_CONNECT:
		arg->setup_state = SETUP_CONT_CREATE;
		return test_setup_cont_create(state);
	case SETUP_CONT_CREATE:
		arg->setup_state = SETUP_CONT_CONNECT;
		return test_setup_cont_open(state);
	}
}

int
test_setup(void **state, unsigned int step, bool multi_rank,
	   daos_size_t pool_size, struct test_pool *pool)
{
	test_arg_t	*arg = *state;
	struct timeval	 now;
	unsigned int	 seed;
	int		 rc = 0;

	/* feed a seed for pseudo-random number generator */
	gettimeofday(&now, NULL);
	seed = (unsigned int)(now.tv_sec * 1000000 + now.tv_usec);
	srandom(seed);

	if (arg == NULL) {
		arg = malloc(sizeof(test_arg_t));
		if (arg == NULL)
			return -1;
		*state = arg;
		memset(arg, 0, sizeof(*arg));

		MPI_Comm_rank(MPI_COMM_WORLD, &arg->myrank);
		MPI_Comm_size(MPI_COMM_WORLD, &arg->rank_size);
		arg->multi_rank = multi_rank;
		arg->pool.pool_size = pool_size;
		arg->setup_state = -1;

		arg->pool.svc.rl_nr = svc_nreplicas;
		arg->pool.svc.rl_ranks = arg->pool.ranks;
		arg->pool.slave = false;

		arg->mode = 0731;
		arg->uid = geteuid();
		arg->gid = getegid();

		arg->group = server_group;
		uuid_clear(arg->pool.pool_uuid);
		uuid_clear(arg->co_uuid);

		arg->hdl_share = false;
		arg->pool.poh = DAOS_HDL_INVAL;
		arg->coh = DAOS_HDL_INVAL;

		arg->pool.destroyed = false;
	}

	while (!rc && step != arg->setup_state)
		rc = test_setup_next_step(state, pool);

	 if (rc) {
		free(arg);
		*state = NULL;
	}
	return rc;
}

static int
pool_destroy_safe(test_arg_t *arg)
{
	daos_pool_info_t		 pinfo;
	daos_handle_t			 poh = arg->pool.poh;
	bool				 connected = false;
	int				 rc;

	if (daos_handle_is_inval(poh)) {
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       &arg->pool.svc, DAOS_PC_RW,
				       &poh, &arg->pool.pool_info,
				       NULL /* ev */);
		if (rc != 0) { /* destory straightaway */
			print_message("failed to connect pool: %d\n", rc);
			poh = DAOS_HDL_INVAL;
		} else {
			connected = true;
		}
	}

	while (!daos_handle_is_inval(poh)) {
		struct daos_rebuild_status *rstat = &pinfo.pi_rebuild_st;

		memset(&pinfo, 0, sizeof(pinfo));
		rc = daos_pool_query(poh, NULL, &pinfo, NULL);
		if (rc != 0) {
			fprintf(stderr, "pool query failed: %d\n", rc);
			return rc;
		}

		if (rstat->rs_done == 0) {
			print_message("waiting for rebuild\n");
			sleep(1);
			continue;
		}

		/* no rebuild */
		if (connected)
			daos_pool_disconnect(poh, NULL);
		break;
	}

	rc = daos_pool_destroy(arg->pool.pool_uuid, arg->group, 1, NULL);
	if (rc && rc != -DER_TIMEDOUT)
		print_message("daos_pool_destroy failed, rc: %d\n", rc);
	return rc;
}

int
test_teardown(void **state)
{
	test_arg_t	*arg = *state;
	int		 rc = 0;
	int              rc_reduce = 0;

	if (arg->multi_rank)
		MPI_Barrier(MPI_COMM_WORLD);

	if (!daos_handle_is_inval(arg->coh)) {
		rc = daos_cont_close(arg->coh, NULL);
		if (arg->multi_rank) {
			MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN,
				      MPI_COMM_WORLD);
			rc = rc_reduce;
		}
		arg->coh = DAOS_HDL_INVAL;
		if (rc) {
			print_message("failed to close container "DF_UUIDF
				      ": %d\n", DP_UUID(arg->co_uuid), rc);
			return rc;;
		}
	}

	if (!uuid_is_null(arg->co_uuid)) {
		while (arg->myrank == 0) {
			rc = daos_cont_destroy(arg->pool.poh, arg->co_uuid, 1,
					       NULL);
			if (rc == -DER_BUSY) {
				print_message("Container is busy, wait\n");
				sleep(1);
				continue;
			}
			break;
		}
		if (arg->multi_rank)
			MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		if (rc) {
			print_message("failed to destroy container "DF_UUIDF
				      ": %d\n", DP_UUID(arg->co_uuid), rc);
			return rc;
		}
	}

	if (!daos_handle_is_inval(arg->pool.poh) && !arg->pool.slave) {
		rc = daos_pool_disconnect(arg->pool.poh, NULL /* ev */);
		arg->pool.poh = DAOS_HDL_INVAL;
		if (arg->multi_rank) {
			MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN,
				      MPI_COMM_WORLD);
			rc = rc_reduce;
		}
		if (rc) {
			print_message("failed to disconnect pool "DF_UUIDF
				      ": %d\n", DP_UUID(arg->pool.pool_uuid),
				      rc);
			return rc;
		}
	}

	if (!uuid_is_null(arg->pool.pool_uuid) && !arg->pool.slave) {
		if (arg->myrank == 0)
			pool_destroy_safe(arg);

		if (arg->multi_rank)
			MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		if (rc)
			return rc;
	}

	if (!daos_handle_is_inval(arg->eq)) {
		rc = daos_eq_destroy(arg->eq, 0);
		if (rc) {
			print_message("failed to destroy eq: %d\n", rc);
			return rc;
		}
	}

	D_FREE_PTR(arg);
	return 0;
}

d_rank_t ranks_to_kill[MAX_KILLS];

bool
test_runable(test_arg_t *arg, unsigned int required_tgts)
{
	int		 i;
	static bool	 runable = true;

	if (arg->myrank == 0) {
		if (arg->srv_ntgts - arg->srv_disabled_ntgts < required_tgts) {
			if (arg->myrank == 0)
				print_message("Not enough targets, skipping "
					      "(%d/%d)\n",
					      arg->srv_ntgts,
					      arg->srv_disabled_ntgts);
			runable = false;
		}

		for (i = 0; i < MAX_KILLS; i++)
			ranks_to_kill[i] = arg->srv_ntgts -
					   arg->srv_disabled_ntgts - i - 1;
	}

	MPI_Bcast(&runable, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Barrier(MPI_COMM_WORLD);
	return runable;
}

int
test_pool_get_info(test_arg_t *arg, daos_pool_info_t *pinfo)
{
	bool	   connect_pool = false;
	int	   rc;

	if (daos_handle_is_inval(arg->pool.poh)) {
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       &arg->pool.svc, DAOS_PC_RW,
				       &arg->pool.poh, pinfo,
				       NULL /* ev */);
		if (rc) {
			print_message("pool_connect "DF_UUIDF
				      " failed, rc: %d\n",
				      DP_UUID(arg->pool.pool_uuid), rc);
			return rc;
		}
		connect_pool = true;
	}

	rc = daos_pool_query(arg->pool.poh, NULL, pinfo, NULL);
	if (rc != 0)
		print_message("pool query failed %d\n", rc);

	if (connect_pool) {
		rc = daos_pool_disconnect(arg->pool.poh, NULL);
		if (rc)
			print_message("disconnect failed: %d\n",
				      rc);
		arg->pool.poh = DAOS_HDL_INVAL;
	}

	return rc;
}

static bool
rebuild_pool_wait(test_arg_t *arg)
{
	daos_pool_info_t	   pinfo = { 0 };
	struct daos_rebuild_status *rst;
	int			   rc;
	bool			   done = false;

	rc = test_pool_get_info(arg, &pinfo);
	rst = &pinfo.pi_rebuild_st;
	if (rst->rs_done || rc != 0) {
		print_message("Rebuild "DF_UUIDF" (ver=%d) is done %d/%d\n",
			       DP_UUID(arg->pool.pool_uuid), rst->rs_version, rc,
			       rst->rs_errno);
		done = true;
	} else {
		print_message("wait for rebuild pool "DF_UUIDF"(ver=%u), "
			      "already rebuilt obj="DF_U64", rec="DF_U64"\n",
			      DP_UUID(arg->pool.pool_uuid), rst->rs_version,
			      rst->rs_obj_nr, rst->rs_rec_nr);
	}

	return done;
}

int
test_get_leader(test_arg_t *arg, d_rank_t *rank)
{
	daos_pool_info_t	pinfo = { 0 };
	int			rc;

	rc = test_pool_get_info(arg, &pinfo);
	if (rc)
		return rc;

	*rank = pinfo.pi_leader;
	return 0;
}

bool
test_rebuild_query(test_arg_t **args, int args_cnt)
{
	bool all_done = true;
	int i;

	for (i = 0; i < args_cnt; i++) {
		bool done;

		done = rebuild_pool_wait(args[i]);
		if (!done)
			all_done = false;
	}
	return all_done;
}

void
test_rebuild_wait(test_arg_t **args, int args_cnt)
{
	while (!test_rebuild_query(args, args_cnt))
		sleep(2);
}

static void
print_usage(int rank)
{
	if (rank)
		return;

	print_message("\n\nDAOS TESTS\n=============================\n");
	print_message("Use one of these options(s) for specific test\n");
	print_message("daos_test -m|--mgmt\n");
	print_message("daos_test -p|--daos_pool_tests\n");
	print_message("daos_test -c|--daos_container_tests\n");
	print_message("daos_test -C|--capa\n");
	print_message("daos_test -i|--daos_io_tests\n");
	print_message("daos_test -A|--array\n");
	print_message("daos_test -d|--degraded\n");
	print_message("daos_test -e|--daos_epoch_tests\n");
	print_message("daos_test -o|--daos_epoch_recovery_tests\n");
	print_message("daos_test -r|--rebuild\n");
	print_message("daos_test -a|--daos_all_tests\n");
	print_message("daos_test -g|--group GROUP\n");
	print_message("daos_test -s|--svcn NSVCREPLICAS\n");
	print_message("daos_test -O|--oid_alloc\n");
	print_message("daos_test -h|--help\n");
	print_message("Default <daos_tests> runs all tests\n=============\n");
}

int
run_daos_sub_tests(const struct CMUnitTest *tests, int tests_size,
		   daos_size_t pool_size, int *sub_tests,
		   int sub_tests_size, test_setup_cb_t cb)
{
	void *state = NULL;
	int i;
	int rc;

	D_ASSERT(pool_size > 0);
	rc = test_setup(&state, SETUP_CONT_CONNECT, true, pool_size, NULL);
	if (rc)
		return rc;

	if (cb != NULL) {
		rc = cb(&state);
		if (rc)
			return rc;
	}

	for (i = 0; i < sub_tests_size; i++) {
		int idx = sub_tests[i];

		if (idx >= tests_size) {
			print_message("No test %d\n", idx);
			continue;
		}

		print_message("%s\n", tests[idx].name);
		if (tests[idx].setup_func)
			tests[idx].setup_func(&state);

		tests[idx].test_func(&state);
		if (tests[idx].teardown_func)
			tests[idx].teardown_func(&state);
	}
	test_teardown(&state);
	return 0;
}

static int
run_specified_tests(const char *tests, int rank, int size,
		    int *sub_tests, int sub_tests_size)
{
	int nr_failed = 0;

	if (strlen(tests) == 0)
		tests = all_tests;

	while (*tests != '\0') {
		switch (*tests) {
		case 'm':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS management tests..");
			daos_test_print(rank, "=====================");
			nr_failed = run_daos_mgmt_test(rank, size);
			break;
		case 'p':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS pool tests..");
			daos_test_print(rank, "=====================");
			nr_failed += run_daos_pool_test(rank, size);
			break;
		case 'c':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS container tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_cont_test(rank, size);
			break;
		case 'C':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS capability tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_capa_test(rank, size);
			break;
		case 'i':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS IO test..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_io_test(rank, size, sub_tests,
						      sub_tests_size);
			break;
		case 'A':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS Array test..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_array_test(rank, size);
			break;
		case 'e':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS Epoch tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_epoch_test(rank, size);
			break;
		case 'o':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS Epoch recovery tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_epoch_recovery_test(rank, size);
			break;
		case 'R':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS MD replication tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_md_replication_test(rank, size);
			break;
		case 'O':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS OID Allocator tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_oid_alloc_test(rank, size);
			break;
		case 'd':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS degraded-mode tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_degraded_test(rank, size);
			break;
		case 'r':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS rebuild tests..");
			daos_test_print(rank, "=================");
			nr_failed += run_daos_rebuild_test(rank, size,
							   sub_tests,
							   sub_tests_size);
			break;
		default:
			D_ASSERT(0);
		}

		tests++;
	}

	return nr_failed;
}

int
main(int argc, char **argv)
{
	test_arg_t	*arg;
	char		 tests[64];
	char		*sub_tests_str = NULL;
	int		 sub_tests[1024];
	int		 sub_tests_idx = 0;
	int		 ntests = 0;
	int		 nr_failed = 0;
	int		 nr_total_failed = 0;
	int		 opt = 0, index = 0;
	int		 rank;
	int		 size;
	int		 rc;

	MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Barrier(MPI_COMM_WORLD);

	static struct option long_options[] = {
		{"all",		no_argument,		NULL,	'a'},
		{"mgmt",	no_argument,		NULL,	'm'},
		{"pool",	no_argument,		NULL,	'p'},
		{"cont",	no_argument,		NULL,	'c'},
		{"capa",	no_argument,		NULL,	'C'},
		{"io",		no_argument,		NULL,	'i'},
		{"array",	no_argument,		NULL,	'A'},
		{"epoch",	no_argument,		NULL,	'e'},
		{"erecov",	no_argument,		NULL,	'o'},
		{"mdr",		no_argument,		NULL,	'R'},
		{"oid_alloc",	no_argument,		NULL,	'O'},
		{"degraded",	no_argument,		NULL,	'd'},
		{"rebuild",	no_argument,		NULL,	'r'},
		{"group",	required_argument,	NULL,	'g'},
		{"svcn",	required_argument,	NULL,	's'},
		{"subtests",	required_argument,	NULL,	'u'},
		{"help",	no_argument,		NULL,	'h'}
	};

	rc = daos_init();
	if (rc) {
		print_message("daos_init() failed with %d\n", rc);
		return -1;
	}

	memset(tests, 0, sizeof(tests));

	while ((opt = getopt_long(argc, argv, "ampcCdiAeoROg:s:u:hr",
				  long_options, &index)) != -1) {
		if (strchr(all_tests, opt) != NULL) {
			tests[ntests] = opt;
			ntests++;
			continue;
		}
		switch (opt) {
		case 'a':
			break;
		case 'g':
			server_group = optarg;
			break;
		case 'h':
			print_usage(rank);
			goto exit;
		case 's':
			svc_nreplicas = atoi(optarg);
			break;
		case 'u':
			sub_tests_str = optarg;
			break;
		default:
			daos_test_print(rank, "Unknown Option\n");
			print_usage(rank);
			goto exit;
		}
	}

	if (svc_nreplicas > ARRAY_SIZE(arg->pool.ranks) && rank == 0) {
		print_message("at most %zu service replicas allowed\n",
			      ARRAY_SIZE(arg->pool.ranks));
		return -1;
	}

	if (sub_tests_str != NULL) {
		/* sub_tests="1,2,3" sub_tests="2-8" */
		char *ptr = sub_tests_str;
		char *tmp;
		int start = -1;
		int end = -1;

		while (*ptr) {
			int number = -1;

			while (!isdigit(*ptr) && *ptr)
				ptr++;

			if (!*ptr)
				break;

			tmp = ptr;
			while (isdigit(*ptr))
				ptr++;

			/* get the current number */
			number = atoi(tmp);
			if (*ptr == '-') {
				if (start != -1) {
					print_message("str is %s\n",
						      sub_tests_str);
					return -1;
				}
				start = number;
				continue;
			} else {
				if (start != -1)
					end = number;
				else
					start = number;
			}

			if (start != -1 || end != -1) {
				if (end != -1) {
					int i;

					for (i = start; i <= end; i++) {
						sub_tests[sub_tests_idx] = i;
						sub_tests_idx++;
					}
				} else {
					sub_tests[sub_tests_idx] = start;
					sub_tests_idx++;
				}
				start = -1;
				end = -1;
			}
		}
	}

	nr_failed = run_specified_tests(tests, rank, size,
					sub_tests, sub_tests_idx);

exit:
	MPI_Allreduce(&nr_failed, &nr_total_failed, 1, MPI_INT, MPI_SUM,
		      MPI_COMM_WORLD);

	rc = daos_fini();
	if (rc)
		print_message("daos_fini() failed with %d\n", rc);

	if (!rank) {
		print_message("\n============ Summary %s\n", __FILE__);
		if (nr_total_failed == 0)
			print_message("OK - NO TEST FAILURES\n");
		else
			print_message("ERROR, %i TEST(S) FAILED\n",
				      nr_total_failed);
	}

	MPI_Finalize();

	return nr_failed;
}
