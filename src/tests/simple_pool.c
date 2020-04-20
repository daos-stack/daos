/**
 * (C) Copyright 2016-2018 Intel Corporation.
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

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <daos.h>
#include "suite/daos_test.h"
#include <mpi.h>

/** local task information */
int			 rank = -1;
int			 rankn = -1;
char			 node[128] = "unknown";

#define FAIL(fmt, ...)						\
do {								\
	fprintf(stderr, "Process %d(%s): " fmt " aborting\n",	\
		rank, node, ## __VA_ARGS__);			\
	MPI_Abort(MPI_COMM_WORLD, 1);				\
} while (0)

#define	ASSERT(cond, ...)					\
do {								\
	if (!(cond))						\
		FAIL(__VA_ARGS__);				\
} while (0)

int
main(int argc, char **argv)
{
	uuid_t		pool_uuid;
	d_rank_list_t	*svcl = NULL;
	daos_handle_t	poh;
	daos_pool_info_t pinfo;
	int		rc;

	rc = gethostname(node, sizeof(node));
	ASSERT(rc == 0, "buffer for hostname too small");

	rc = MPI_Init(&argc, &argv);
	ASSERT(rc == MPI_SUCCESS, "MPI_Init failed with %d", rc);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &rankn);

	/** initialize the local DAOS stack */
	rc = daos_init();
	ASSERT(rc == 0, "daos_init failed with %d", rc);

	rc = uuid_parse(argv[1], pool_uuid);
	ASSERT(rc == 0, "Failed to parse 'Pool uuid': %s", argv[1]);

	svcl = daos_rank_list_parse(argv[2], ":");
	if (svcl == NULL)
		ASSERT(svcl != NULL, "Failed to allocate svcl");

	if (rank == 0)
		printf("Connecting to pool %s\n", argv[1]);

	rc = daos_pool_connect(pool_uuid, NULL, svcl,
			       DAOS_PC_RW /* exclusive access */,
			       &poh /* returned pool handle */,
			       &pinfo /* returned pool info */,
			       NULL /* event */);
	ASSERT(rc == 0, "pool connect failed with %d", rc);
	MPI_Barrier(MPI_COMM_WORLD);

	d_rank_list_free(svcl);
	/** disconnect from pool & destroy it */
	rc = daos_pool_disconnect(poh, NULL);
	ASSERT(rc == 0, "disconnect failed");

	usleep(20000*rank);
	/** shutdown the local DAOS stack */
	rc = daos_fini();
	ASSERT(rc == 0, "daos_fini failed with %d", rc);

	MPI_Finalize();
	return rc;
}
