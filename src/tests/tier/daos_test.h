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
 * This file is part of DAOS
 * src/tests/suite/daos_test.h
 */
#ifndef __DAOS_TEST_H
#define __DAOS_TEST_H

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <time.h>
#include <cmocka.h>
#include <mpi.h>
#include <daos/common.h>
#include <daos/tests_lib.h>
#include <daos.h>

typedef struct {
	bool			multi_rank;
	d_rank_t		ranks[8];
	int			myrank;
	int			rank_size;
	d_rank_list_t	svc;
	const char	       *group;
	uuid_t			pool_uuid;
	uuid_t			co_uuid;
	unsigned int		mode;
	unsigned int		uid;
	unsigned int		gid;
	daos_handle_t		eq;
	daos_handle_t		poh;
	daos_handle_t		coh;
	daos_pool_info_t	pool_info;
	daos_cont_info_t	co_info;
	bool			async;
	bool			hdl_share;
	uint64_t		fail_loc;
	uint64_t		fail_value;
	int			expect_result;
	daos_size_t		size;
	int			nr;
} test_arg_t;

enum {
	SETUP_EQ,
	SETUP_POOL_CREATE,
	SETUP_POOL_CONNECT,
	SETUP_CONT_CREATE,
	SETUP_CONT_CONNECT,
};

int
test_teardown(void **state);
int
test_setup(void **state, unsigned int step, bool multi_rank);

static inline int
async_enable(void **state)
{
	test_arg_t	*arg = *state;

	arg->async = true;
	return 0;
}

static inline int
async_disable(void **state)
{
	test_arg_t	*arg = *state;

	arg->async = false;
	return 0;
}

static inline int
hdl_share_enable(void **state)
{
	test_arg_t	*arg = *state;

	arg->hdl_share = true;
	return 0;
}

enum {
	HANDLE_POOL,
	HANDLE_CO
};

int run_daos_mgmt_test(int rank, int size);
int run_daos_pool_test(int rank, int size);
int run_daos_cont_test(int rank, int size);
int run_daos_capa_test(int rank, int size);
int run_daos_io_test(int rank, int size);
int run_daos_array_test(int rank, int size);
int run_daos_epoch_test(int rank, int size);
int run_daos_epoch_recovery_test(int rank, int size);
int run_daos_degraded_test(int rank, int size);

static inline void
handle_share(daos_handle_t *hdl, int type, int rank, daos_handle_t poh,
	     int verbose)
{
	daos_iov_t	ghdl = { NULL, 0, 0 };
	int		rc;

	if (rank == 0) {
		/** fetch size of global handle */
		if (type == HANDLE_POOL)
			rc = daos_pool_local2global(*hdl, &ghdl);
		else
			rc = daos_cont_local2global(*hdl, &ghdl);
		assert_int_equal(rc, 0);
	}

	/** broadcast size of global handle to all peers */
	rc = MPI_Bcast(&ghdl.iov_buf_len, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

	/** allocate buffer for global pool handle */
	ghdl.iov_buf = malloc(ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	if (rank == 0) {
		/** generate actual global handle to share with peer tasks */
		if (verbose)
			print_message("rank 0 call local2global on %s handle",
				      (type == HANDLE_POOL) ?
				      "pool" : "container");
		if (type == HANDLE_POOL)
			rc = daos_pool_local2global(*hdl, &ghdl);
		else
			rc = daos_cont_local2global(*hdl, &ghdl);
		assert_int_equal(rc, 0);
		if (verbose)
			print_message("success\n");
	}

	/** broadcast global handle to all peers */
	if (rank == 0 && verbose == 1)
		print_message("rank 0 broadcast global %s handle ...",
			      (type == HANDLE_POOL) ? "pool" : "container");
	rc = MPI_Bcast(ghdl.iov_buf, ghdl.iov_len, MPI_BYTE, 0,
		       MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);
	if (rank == 0 && verbose == 1)
		print_message("success\n");

	if (rank != 0) {
		/** unpack global handle */
		if (verbose)
			print_message("rank %d call global2local on %s handle",
				      rank, type == HANDLE_POOL ?
				      "pool" : "container");
		if (type == HANDLE_POOL) {
			/* NB: Only pool_global2local are different */
			rc = daos_pool_global2local(ghdl, hdl);
		} else {
			rc = daos_cont_global2local(poh, ghdl, hdl);
		}

		assert_int_equal(rc, 0);
		if (verbose)
			print_message("rank %d global2local success\n", rank);
	}

	free(ghdl.iov_buf);

	MPI_Barrier(MPI_COMM_WORLD);
}

#endif
