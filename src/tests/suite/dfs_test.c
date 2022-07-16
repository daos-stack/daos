/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/dfs_test
 */
#define D_LOGFAC	DD_FAC(tests)

#include <getopt.h>
#include "dfs_test.h"

/**
 * Tests can be run by specifying the appropriate argument for a test or
 * all will be run if no test is specified. Tests will be run in order
 * so tests that kill nodes must be last.
 */
#define TESTS "pus"
static const char *all_tests = TESTS;

static void
print_usage(int rank)
{
	if (rank)
		return;

	print_message("\n\nDFS TESTS\n=============================\n");
	print_message("Tests: Use one of these arg(s) for specific test\n");
	print_message("dfs_test -p|--parallel\n");
	print_message("dfs_test -u|--unit\n");
	print_message("dfs_test -s|--sys\n");
	print_message("Default <daos_tests> runs all tests\n=============\n");
	print_message("dfs_test -E|--exclude TESTS\n");
	print_message("dfs_test -n|--dmg_config\n");
	print_message("\n=============================\n");
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
		case 'p':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DFS parallel tests..");
			daos_test_print(rank, "=====================");
			nr_failed += run_dfs_par_test(rank, size);
			break;
		case 'u':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DFS unit tests..");
			daos_test_print(rank, "=====================");
			nr_failed += run_dfs_unit_test(rank, size);
			break;
		case 's':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DFS Sys unit tests..");
			daos_test_print(rank, "=====================");
			nr_failed += run_dfs_sys_unit_test(rank, size);
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
	char		*exclude_str = NULL;
	int		 ntests = 0;
	int		 nr_failed = 0;
	int		 nr_total_failed = 0;
	int		 opt = 0, index = 0;
	int		 rank;
	int		 size;
	int		 rc;

	d_register_alt_assert(mock_assert);

	MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Barrier(MPI_COMM_WORLD);

	static struct option long_options[] = {
		{"all",		no_argument,		NULL,	'a'},
		{"dmg_config",	required_argument,	NULL,	'n'},
		{"parallel",	no_argument,		NULL,	'p'},
		{"unit",	no_argument,		NULL,	'u'},
		{"sys",		no_argument,		NULL,	's'},
		{NULL,		0,			NULL,	0}
	};

	rc = daos_init();
	if (rc) {
		print_message("daos_init() failed with %d\n", rc);
		return -1;
	}

	memset(tests, 0, sizeof(tests));

	while ((opt = getopt_long(argc, argv, "aE:n:pus",
				  long_options, &index)) != -1) {
		if (strchr(all_tests, opt) != NULL) {
			tests[ntests] = opt;
			ntests++;
			continue;
		}
		switch (opt) {
		case 'a':
			break;
		case 'n':
			dmg_config_file = optarg;
			break;
		case 'E':
			exclude_str = optarg;
			break;
		default:
			daos_test_print(rank, "Unknown Option\n");
			print_usage(rank);
			goto exit;
		}
	}

	if (strlen(tests) == 0)
		strncpy(tests, all_tests, sizeof(TESTS));

	if (svc_nreplicas > ARRAY_SIZE(arg->pool.ranks) && rank == 0) {
		print_message("at most %zu service replicas allowed\n",
			      ARRAY_SIZE(arg->pool.ranks));
		return -1;
	}

	/*Exclude tests mentioned in exclude list*/
	/* Example: daos_test -E mpc */
	if (exclude_str != NULL) {
		int old_idx, new_idx = 0;

		printf("\n==============");
		printf("\n Excluding tests %s", exclude_str);
		printf("\n==============");
		for (old_idx = 0; tests[old_idx] != 0; old_idx++) {
			if (!strchr(exclude_str, tests[old_idx])) {
				tests[new_idx] = tests[old_idx];
				new_idx++;
			}
		}
		tests[new_idx] = '\0';
	}

	nr_failed = run_specified_tests(tests, rank, size, NULL, 0);

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
