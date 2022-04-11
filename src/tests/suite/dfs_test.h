/**
 * (C) Copyright 2019-2022 Intel Corporation.
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
#include <daos_fs_sys.h>

int run_dfs_unit_test(int rank, int size);
int run_dfs_par_test(int rank, int size);
int run_dfs_sys_unit_test(int rank, int size);

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
	rc = par_bcast(PAR_COMM_WORLD, &ghdl.iov_buf_len, 1, PAR_UINT64, 0);
	assert_int_equal(rc, 0);

	/** allocate buffer for global pool handle */
	D_ALLOC(ghdl.iov_buf, ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	if (rank == 0) {
		/** generate actual global handle to share with peer tasks */
		rc = dfs_local2global(*dfs, &ghdl);
		assert_int_equal(rc, 0);
	}

	/** broadcast global handle to all peers */
	rc = par_bcast(PAR_COMM_WORLD, ghdl.iov_buf, ghdl.iov_len, PAR_BYTE, 0);
	assert_int_equal(rc, 0);

	if (rank != 0) {
		/** unpack global handle */
		rc = dfs_global2local(poh, coh, 0, ghdl, dfs);
		assert_int_equal(rc, 0);
	}

	D_FREE(ghdl.iov_buf);

	par_barrier(PAR_COMM_WORLD);
}

static inline void
dfs_sys_test_share(daos_handle_t poh, daos_handle_t coh, int rank, int sflags,
		   dfs_sys_t **dfs_sys)
{
	d_iov_t	ghdl = { NULL, 0, 0 };
	int	rc;

	if (rank == 0) {
		/** fetch size of global handle */
		rc = dfs_sys_local2global(*dfs_sys, &ghdl);
		assert_int_equal(rc, 0);
	}

	/** broadcast size of global handle to all peers */
	rc = par_bcast(PAR_COMM_WORLD, &ghdl.iov_buf_len, 1, PAR_UINT64, 0);
	assert_int_equal(rc, 0);

	/** allocate buffer for global pool handle */
	D_ALLOC(ghdl.iov_buf, ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	if (rank == 0) {
		/** generate actual global handle to share with peer tasks */
		rc = dfs_sys_local2global(*dfs_sys, &ghdl);
		assert_int_equal(rc, 0);
	}

	/** broadcast global handle to all peers */
	rc = par_bcast(PAR_COMM_WORLD, ghdl.iov_buf, ghdl.iov_len, PAR_BYTE, 0);
	assert_int_equal(rc, 0);

	if (rank != 0) {
		/** unpack global handle */
		rc = dfs_sys_global2local(poh, coh, 0, sflags, ghdl, dfs_sys);
		assert_int_equal(rc, 0);
	}

	D_FREE(ghdl.iov_buf);

	par_barrier(PAR_COMM_WORLD);
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
	rc = par_bcast(PAR_COMM_WORLD, &ghdl.iov_buf_len, 1, PAR_UINT64, 0);
	assert_int_equal(rc, 0);

	/** allocate buffer for global pool handle */
	D_ALLOC(ghdl.iov_buf, ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	if (rank == 0) {
		/** generate actual global handle to share with peer tasks */
		rc = dfs_obj_local2global(dfs, *obj, &ghdl);
		assert_int_equal(rc, 0);
	}

	/** broadcast global handle to all peers */
	rc = par_bcast(PAR_COMM_WORLD, ghdl.iov_buf, ghdl.iov_len, PAR_BYTE, 0);
	assert_int_equal(rc, 0);

	if (rank != 0) {
		/** unpack global handle */
		rc = dfs_obj_global2local(dfs, flags, ghdl, obj);
		assert_int_equal(rc, 0);
	}

	D_FREE(ghdl.iov_buf);
	par_barrier(PAR_COMM_WORLD);
}

#endif
