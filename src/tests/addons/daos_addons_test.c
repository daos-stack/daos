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
 * This file is part of daos_m
 *
 * src/tests/addons/
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_test.h"
#include "daos_addons_test.h"

static void
print_usage(int rank)
{
	if (rank)
		return;

	print_message("\n\nDAOS Addins TESTS\n====================\n");
	print_message("daos_test -g|--group GROUP\n");
	print_message("daos_test -s|--svcn NSVCREPLICAS\n");
	print_message("daos_test -h|--help\n");
	print_message("n==========================================\n");
}

int
main(int argc, char **argv)
{
	test_arg_t	*arg;
	int		nr_failed = 0;
	int		nr_total_failed = 0;
	int		opt = 0, index = 0;
	int		rank;
	int		size;
	int		rc;

	MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	static struct option long_options[] = {
		{"group",	required_argument,	NULL,	'g'},
		{"svcn",	required_argument,	NULL,	's'},
		{"help",	no_argument,		NULL,	'h'}
	};

	rc = daos_init();
	if (rc) {
		print_message("daos_init() failed with %d\n", rc);
		return -1;
	}

	while ((opt = getopt_long(argc, argv, "g:s:h",
				  long_options, &index)) != -1) {
		switch (opt) {
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

	if (svc_nreplicas > ARRAY_SIZE(arg->pool.ranks) && rank == 0) {
		print_message("at most %zu service replicas allowed\n",
			      ARRAY_SIZE(arg->pool.ranks));
		return -1;
	}

	daos_test_print(rank, "\n\n=================");
	daos_test_print(rank, "DAOS ADDONS Array tests..");
	daos_test_print(rank, "=====================");
	nr_failed = run_array_test(rank, size);

	daos_test_print(rank, "\n\n=================");
	daos_test_print(rank, "DAOS ADDONS HL tests..");
	daos_test_print(rank, "=====================");
	nr_failed += run_hl_test(rank, size);

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
