/**
 * (C) Copyright 2017 Intel Corporation.
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

/* generic */
#include <stdio.h>
#include <unistd.h>
#include <mpi.h>
#include <uuid/uuid.h>

/* daos specific */
#include <daos.h>
#include <daos_api.h>
#include <daos/common.h>

/* test specific */
#include "test_types.h"

int
setup(int argc, char **argv)
{
	int daos_rc;
	int test_rc = TEST_SUCCESS;
	int my_client_rank = 0;
	int rank_size = 1;

	/* setup the MPI stuff */
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &my_client_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &rank_size);
	MPI_Barrier(MPI_COMM_WORLD);

	daos_rc = daos_init();
	if (daos_rc)
		test_rc = TEST_FAILED;

	return test_rc;
}
