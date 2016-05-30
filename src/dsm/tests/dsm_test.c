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
 * This file is part of dsm
 *
 * dsm/tests/dsm_test
 */

#include "dsm_test.h"

int
main(int argc, char **argv)
{
	int	nr_failed = 0;
	int	rank;
	int	size;
	int	rc;

	MPI_Init(&argc, &argv);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	rc = dmg_init();
	if (rc) {
		print_message("dmg_init() failed with %d\n", rc);
		return -1;
	}

	rc = dsm_init();
	if (rc) {
		print_message("dmg_init() failed with %d\n", rc);
		return -1;
	}

	nr_failed = run_pool_test(rank, size);
	nr_failed += run_co_test(rank, size);
	nr_failed += run_io_test(rank, size);

	rc = dsm_fini();
	if (rc)
		print_message("dsm_fini() failed with %d\n", rc);

	rc = dmg_fini();
	if (rc)
		print_message("dmg_fini() failed with %d\n", rc);

	print_message("\n============ Summary %s\n", __FILE__);
	if (nr_failed == 0)
		print_message("OK - NO TEST FAILURES\n");
	else
		print_message("ERROR, %i TEST(S) FAILED\n", nr_failed);

	/**
	 * XXX: don't call MPI_Finalize() for now since dtp already calls
	 * pmix_finalize() through mcl_finalize()
	 * MPI_Finalize();
	 */

	return nr_failed;
}
