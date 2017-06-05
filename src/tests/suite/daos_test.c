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

#define DD_SUBSYS	DD_FAC(tests)
#include <getopt.h>
#include "daos_test.h"

/** All tests in default order ('d' must be the last one) */
static const char *all_tests = "mpceiACoRdr";

/** Server crt group ID */
static const char *server_group;

/** Pool service replicas: {0, 1, ..., svc_nreplicas - 1} */
static unsigned int svc_nreplicas = 1;

int
test_setup(void **state, unsigned int step, bool multi_rank)
{
	test_arg_t	*arg;
	int		 i;
	int		 rc;

	arg = malloc(sizeof(test_arg_t));
	if (arg == NULL)
		return -1;

	memset(arg, 0, sizeof(*arg));

	MPI_Comm_rank(MPI_COMM_WORLD, &arg->myrank);
	MPI_Comm_size(MPI_COMM_WORLD, &arg->rank_size);
	arg->multi_rank = multi_rank;

	arg->svc.rl_nr.num = svc_nreplicas;
	arg->svc.rl_nr.num_out = svc_nreplicas;
	arg->svc.rl_ranks = arg->ranks;
	for (i = 0; i < svc_nreplicas; i++)
		arg->svc.rl_ranks[i] = i;

	arg->mode = 0731;
	arg->uid = geteuid();
	arg->gid = getegid();

	arg->group = server_group;
	uuid_clear(arg->pool_uuid);
	uuid_clear(arg->co_uuid);

	arg->hdl_share = false;
	arg->poh = DAOS_HDL_INVAL;
	arg->coh = DAOS_HDL_INVAL;

	rc = daos_eq_create(&arg->eq);
	if (rc)
		return rc;

	if (step == SETUP_EQ)
		goto out;

	/** create pool */
	if (arg->myrank == 0) {
		print_message("setup: creating pool\n");

		rc = daos_pool_create(0731, geteuid(), getegid(), arg->group,
				      NULL, "pmem", 1024*1024*1024, &arg->svc,
				      arg->pool_uuid, NULL);
		if (rc)
			print_message("daos_pool_create failed, rc: %d\n", rc);
		else
			print_message("setup: created pool "DF_UUIDF"\n",
				       DP_UUID(arg->pool_uuid));
	}
	/** broadcast pool create result */
	if (multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** broadcast pool UUID and svc addresses */
	if (arg->myrank == 0)
		arg->svc.rl_nr.num = arg->svc.rl_nr.num_out;
	if (multi_rank) {
		MPI_Bcast(arg->pool_uuid, 16, MPI_CHAR, 0, MPI_COMM_WORLD);
		MPI_Bcast(&arg->svc, sizeof(arg->pool_info), MPI_CHAR, 0,
			  MPI_COMM_WORLD);
	}

	if (step == SETUP_POOL_CREATE)
		goto out;

	/** connect to pool */
	if (arg->myrank == 0) {
		print_message("setup: connecting to pool\n");
		rc = daos_pool_connect(arg->pool_uuid, arg->group, &arg->svc,
				       DAOS_PC_RW, &arg->poh, &arg->pool_info,
				       NULL /* ev */);
		if (rc)
			print_message("daos_pool_connect failed, rc: %d\n", rc);
	}
	/** broadcast pool connect result */
	if (multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** broadcast pool info */
	if (multi_rank)
		MPI_Bcast(&arg->pool_info, sizeof(arg->pool_info), MPI_CHAR, 0,
			  MPI_COMM_WORLD);

	/** l2g and g2l the pool handle */
	if (multi_rank)
		handle_share(&arg->poh, HANDLE_POOL, arg->myrank, arg->poh, 0);

	if (step == SETUP_POOL_CONNECT)
		goto out;

	/** create container */
	if (arg->myrank == 0) {
		print_message("setup: creating container\n");
		uuid_generate(arg->co_uuid);
		rc = daos_cont_create(arg->poh, arg->co_uuid, NULL);
		if (rc)
			print_message("daos_cont_create failed, rc: %d\n", rc);
	}
	/** broadcast container create result */
	if (multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** broadcast container UUID */
	if (multi_rank)
		MPI_Bcast(arg->co_uuid, 16, MPI_CHAR, 0, MPI_COMM_WORLD);

	if (step == SETUP_CONT_CREATE)
		goto out;

	/** open container */
	if (arg->myrank == 0) {
		print_message("setup: opening container\n");
		rc = daos_cont_open(arg->poh, arg->co_uuid, DAOS_COO_RW,
				    &arg->coh, &arg->co_info, NULL);
		if (rc)
			print_message("daos_cont_open failed, rc: %d\n", rc);
	}
	/** broadcast container open result */
	if (multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** l2g and g2l the container handle */
	if (multi_rank)
		handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->poh, 0);

	if (step == SETUP_CONT_CONNECT)
		goto out;
out:
	*state = arg;
	return 0;
}

int
test_teardown(void **state)
{
	test_arg_t	*arg = *state;
	int		 rc, rc_reduce = 0;

	if (arg->multi_rank)
		MPI_Barrier(MPI_COMM_WORLD);

	if (!daos_handle_is_inval(arg->coh)) {
		rc = daos_cont_close(arg->coh, NULL);
		if (arg->multi_rank) {
			MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN,
				      MPI_COMM_WORLD);
			rc = rc_reduce;
		}
		if (rc) {
			print_message("failed to close container "DF_UUIDF
				      ": %d\n", DP_UUID(arg->co_uuid), rc);
			return rc;;
		}
	}

	if (!uuid_is_null(arg->co_uuid)) {
		if (arg->myrank == 0)
			rc = daos_cont_destroy(arg->poh, arg->co_uuid, 1, NULL);
		if (arg->multi_rank)
			MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
		if (rc) {
			print_message("failed to destroy container "DF_UUIDF
				      ": %d\n", DP_UUID(arg->co_uuid), rc);
			return rc;
		}
	}

	if (!daos_handle_is_inval(arg->poh)) {
		rc = daos_pool_disconnect(arg->poh, NULL /* ev */);
		if (arg->multi_rank) {
			MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
			rc = rc_reduce;
		}
		if (rc) {
			print_message("failed to disconnect pool "DF_UUIDF
				      ": %d\n", DP_UUID(arg->pool_uuid), rc);
			return rc;
		}
	}

	if (!uuid_is_null(arg->pool_uuid)) {
		if (arg->myrank == 0) {
			rc = daos_pool_destroy(arg->pool_uuid, arg->group, 1,
					       NULL);
			if (rc)
				print_message("daos_pool_destroy failed, rc: %d\n", rc);
		}
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
	print_message("daos_test -s|--svc NREPLICAS {0, 1, ..., N-1}\n");
	print_message("daos_test -h|--help\n");
	print_message("Default <daos_tests> runs all tests\n=============\n");
}

static inline void
daos_test_print(int rank, char *message)
{
	if (!rank)
		print_message("%s\n", message);
}

static int
run_specified_tests(const char *tests, int rank, int size)
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
			nr_failed += run_daos_io_test(rank, size);
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
			nr_failed += run_daos_rebuild_test(rank, size);
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
	int		 ntests = 0;
	int		 nr_failed;
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
		{"degraded",	no_argument,		NULL,	'd'},
		{"rebuild",	no_argument,		NULL,	'r'},
		{"group",	required_argument,	NULL,	'g'},
		{"svc",		required_argument,	NULL,	's'},
		{"help",	no_argument,		NULL,	'h'}
	};

	rc = daos_init();
	if (rc) {
		print_message("daos_init() failed with %d\n", rc);
		return -1;
	}

	memset(tests, 0, sizeof(tests));

	while ((opt = getopt_long(argc, argv, "ampcCdiAeoRg:hr",
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
		default:
			daos_test_print(rank, "Unknown Option\n");
			print_usage(rank);
			goto exit;
		}
	}

	if (svc_nreplicas > ARRAY_SIZE(arg->ranks) && rank == 0) {
		print_message("at most %zu service replicas allowed\n",
			      ARRAY_SIZE(arg->ranks));
		return -1;
	}

	nr_failed = run_specified_tests(tests, rank, size);

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
