/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __SIMPLE_COMMON_H__
#define __SIMPLE_COMMON_H__
#include <mpi.h>
#include <stdio.h>
#include <daos.h>

extern char			 node[128];
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

enum {
	HANDLE_POOL,
	HANDLE_CO,
};

static inline void
handle_share(daos_handle_t *hdl, int type, int rank, daos_handle_t poh,
	     int verbose)
{
	d_iov_t	ghdl = { NULL, 0, 0 };
	int		rc;

	if (rank == 0) {
		/** fetch size of global handle */
		if (type == HANDLE_POOL)
			rc = daos_pool_local2global(*hdl, &ghdl);
		else
			rc = daos_cont_local2global(*hdl, &ghdl);
		ASSERT(rc == 0);
	}

	/** broadcast size of global handle to all peers */
	rc = MPI_Bcast(&ghdl.iov_buf_len, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
	ASSERT(rc == 0);

	/** allocate buffer for global pool handle */
	D_ALLOC(ghdl.iov_buf, ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	if (rank == 0) {
		/** generate actual global handle to share with peer tasks */
		if (verbose)
			printf("rank 0 call local2global on %s handle",
			       (type == HANDLE_POOL) ?  "pool" : "container");
		if (type == HANDLE_POOL)
			rc = daos_pool_local2global(*hdl, &ghdl);
		else
			rc = daos_cont_local2global(*hdl, &ghdl);
		ASSERT(rc == 0);
		if (verbose)
			printf("success\n");
	}

	/** broadcast global handle to all peers */
	if (rank == 0 && verbose == 1)
		printf("rank 0 broadcast global %s handle ...",
		       (type == HANDLE_POOL) ? "pool" : "container");
	rc = MPI_Bcast(&ghdl.iov_buf, ghdl.iov_len, MPI_BYTE, 0, MPI_COMM_WORLD);
	ASSERT(rc == 0);
	if (rank == 0 && verbose == 1)
		printf("success\n");

	if (rank != 0) {
		/** unpack global handle */
		if (verbose)
			printf("rank %d call global2local on %s handle", rank,
			       type == HANDLE_POOL ?  "pool" : "container");
		if (type == HANDLE_POOL) {
			/* NB: Only pool_global2local are different */
			rc = daos_pool_global2local(ghdl, hdl);
		} else {
			rc = daos_cont_global2local(poh, ghdl, hdl);
		}

		ASSERT(rc == 0);
		if (verbose)
			printf("rank %d global2local success\n", rank);
	}

	D_FREE(ghdl.iov_buf);

	MPI_Barrier(MPI_COMM_WORLD);
}

#endif

