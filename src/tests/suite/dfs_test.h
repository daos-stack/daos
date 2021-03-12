/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of DAOS
 * src/tests/suite/dfs_test.h
 */
#ifndef __DFS_TEST_H
#define __DFS_TEST_H

#include <sys/types.h>
#include <fcntl.h>
#include "daos_test.h"
#include <daos_fs.h>

int run_dfs_unit_test(int rank, int size);
int run_dfs_par_test(int rank, int size);

static inline void
dfs_test_share(daos_handle_t poh, daos_handle_t coh, int rank, dfs_t **dfs)
{
	d_iov_t	ghdl = { NULL, 0, 0 };
	int	rc;

	if (rank == 0) {
		/** fetch size of global handle */
		rc = dfs_local2global(*dfs, &ghdl);
		assert_int_equal(rc, 0);
	}

	/** broadcast size of global handle to all peers */
	rc = MPI_Bcast(&ghdl.iov_buf_len, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

	/** allocate buffer for global pool handle */
	D_ALLOC(ghdl.iov_buf, ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	if (rank == 0) {
		/** generate actual global handle to share with peer tasks */
		rc = dfs_local2global(*dfs, &ghdl);
		assert_int_equal(rc, 0);
	}

	/** broadcast global handle to all peers */
	rc = MPI_Bcast(ghdl.iov_buf, ghdl.iov_len, MPI_BYTE, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

	if (rank != 0) {
		/** unpack global handle */
		rc = dfs_global2local(poh, coh, 0, ghdl, dfs);
		assert_int_equal(rc, 0);
	}

	D_FREE(ghdl.iov_buf);

	MPI_Barrier(MPI_COMM_WORLD);
}

static inline void
dfs_test_obj_share(dfs_t *dfs, int flags, int rank, dfs_obj_t **obj)
{
	d_iov_t	ghdl = { NULL, 0, 0 };
	int	rc;

	if (rank == 0) {
		/** fetch size of global handle */
		rc = dfs_obj_local2global(dfs, *obj, &ghdl);
		assert_int_equal(rc, 0);
	}

	/** broadcast size of global handle to all peers */
	rc = MPI_Bcast(&ghdl.iov_buf_len, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

	/** allocate buffer for global pool handle */
	D_ALLOC(ghdl.iov_buf, ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	if (rank == 0) {
		/** generate actual global handle to share with peer tasks */
		rc = dfs_obj_local2global(dfs, *obj, &ghdl);
		assert_int_equal(rc, 0);
	}

	/** broadcast global handle to all peers */
	rc = MPI_Bcast(ghdl.iov_buf, ghdl.iov_len, MPI_BYTE, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

	if (rank != 0) {
		/** unpack global handle */
		rc = dfs_obj_global2local(dfs, flags, ghdl, obj);
		assert_int_equal(rc, 0);
	}

	D_FREE(ghdl.iov_buf);
	MPI_Barrier(MPI_COMM_WORLD);
}

#endif
