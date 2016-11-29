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

#include <getopt.h>
#include "daos_test.h"

static inline int
run_all_tests(int rank, int size)
{
	int nr_failed = 0;

	nr_failed  = run_daos_mgmt_test(rank, size);
	nr_failed += run_daos_pool_test(rank, size);
	nr_failed += run_daos_cont_test(rank, size);
	nr_failed += run_daos_epoch_test(rank, size);
	nr_failed += run_daos_io_test(rank, size);
	nr_failed += run_daos_capa_test(rank, size);
	nr_failed += run_daos_epoch_recovery_test(rank, size);

	return nr_failed;
}

static void
print_usage(int rank)
{
	if (rank)
		return;

	print_message("\n\nDAOS TESTS\n=============================\n");
	print_message("Use one of these options(s) for specific test\n");
	print_message("( NOTE: daos_repl_tests ");
	print_message("cannot be triggered with all tests)\n\n");
	print_message("daos_test -m|--mgmt\n");
	print_message("daos_test -p|--daos_pool_tests\n");
	print_message("daos_test -c|--daos_container_tests\n");
	print_message("daos_test -y|--capa\n");
	print_message("daos_test -i|--daos_io_tests\n");
	print_message("daos_test -e|--daos_epoch_tests\n");
	print_message("daos_test -o|--daos_epoch_recovery_tests\n");
	print_message("daos_test -r|--daos_repl_tests\n");
	print_message("daos_test -a|--daos_all_tests\n");
	print_message("daos_test -h|--help\n");
	print_message("Default <daos_tests> runs all tests\n=============\n");
}

static inline void
daos_test_print(int rank, char *message)
{
	if (!rank)
		print_message("%s\n", message);
}

int
main(int argc, char **argv)
{
	int	nr_failed = 0;
	int	nr_total_failed = 0;
	int	opt = 0, index = 0;
	int	rank;
	int	size;
	int	rc;

	MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	static struct option long_options[] = {
		{"daos_all_tests", no_argument, 0, 'a'},
		{"mgmt", no_argument, 0, 'm'},
		{"daos_pool_tests", no_argument, 0, 'p'},
		{"daos_container_tests", no_argument, 0, 'c'},
		{"capa", no_argument, 0, 'y'},
		{"daos_io_tests", no_argument, 0, 'i'},
		{"daos_epoch_tests", no_argument, 0, 'e'},
		{"daos_epoch_recovery_tests", no_argument, 0, 'o'},
		{"daos_degraded_demo", 1, NULL, 'd'},
		{"daos_degraded_tests", no_argument, 0, 'r'},
		{"help", no_argument, 0, 'h'},
	};

	enum  {
		NUM_OF_KEYS = 0,
		WAIT_SECONDS,
		THE_END
	};

	char *const repl_opts[] = {
		[NUM_OF_KEYS]	= "nkeys",
		[WAIT_SECONDS]	= "sleep",
		[THE_END]	= NULL
	};

	rc = daos_init();
	if (rc) {
		print_message("daos_init() failed with %d\n", rc);
		return -1;
	}

	if (argc < 2) {
		nr_failed = run_all_tests(rank, size);
		goto exit;
	}

	char	*subopts = NULL, *value = NULL;
	int	dkeys = 0, ssec = 0;

	while ((opt = getopt_long(argc, argv, "ampcyieod:rh",
				  long_options, &index)) != -1) {
		switch (opt) {

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
		case 'y':
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
		case 'a':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "All tests w/o replication");
			daos_test_print(rank, "=================");
			nr_failed = run_all_tests(rank, size);
			break;
		case 'd':
			daos_test_print(rank, "\n\n=================");
			daos_test_print(rank, "DAOS Degraded Mode Demo test");
			daos_test_print(rank, "=================");

			if (optarg != NULL) {
				subopts = optarg;
				while (*subopts != '\0') {
					switch (getsubopt(&subopts, repl_opts,
							  &value)) {
					case NUM_OF_KEYS:
						dkeys = (value != NULL) ?
						atoi(value) : 1;
						break;
					case WAIT_SECONDS:
						ssec = (value != NULL) ?
						atoi(value) : 1;
						break;
					default:
						daos_test_print(rank,
								"Unknown Sopt");
						break;
					}
				}
			} else {
				print_message("optarg is empty\n");
			}

			if (!rank)
				print_message("akey %d, secs: %d\n",
					      dkeys, ssec);

			nr_failed += run_daos_repl_test(rank, size, dkeys,
							ssec);
			break;
		case 'r':
			dkeys = 1000;
			ssec  = 0;

			nr_failed += run_daos_repl_test(rank, size, dkeys,
							ssec);
			break;
		case 'h':
			print_usage(rank);
			goto exit;
		default:
			daos_test_print(rank, "Unknown Option\n");
			print_usage(rank);
			goto exit;
		}
	}

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
