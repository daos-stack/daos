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
 * src/addons/tests/array_test.c
 */

#include <daos_test.h>
#include <daos_array.h>

#include "daos_addons_test.h"

/** number of elements to write to array */
#define NUM_ELEMS 64
/** num of mem segments for strided access - Must evenly divide NUM_ELEMS */
#define NUM_SEGS 4

#define DTS_OCLASS_DEF		DAOS_OC_REPL_MAX_RW

daos_size_t block_size = 16;

static void simple_array_mgmt(void **state);
static void contig_mem_contig_arr_io(void **state);
static void contig_mem_str_arr_io(void **state);
static void str_mem_str_arr_io(void **state);
static void read_empty_records(void **state);

static void
simple_array_mgmt(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_size_t	cell_size = 0, block_size = 0;
	daos_size_t	size;
	int		rc;

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	/** create the array */
	rc = daos_array_create(arg->coh, oid, 0, 4, 16, &oh, NULL);
	assert_int_equal(rc, 0);

	rc = daos_array_set_size(oh, 0, 265, NULL);
	assert_int_equal(rc, 0);

	rc = daos_array_get_size(oh, 0, &size, NULL);
	assert_int_equal(rc, 0);

	rc = daos_array_close(oh, NULL);
	assert_int_equal(rc, 0);

	/** open the array */
	rc = daos_array_open(arg->coh, oid, 0, DAOS_OO_RO, &cell_size,
			     &block_size, &oh, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(4, cell_size);
	assert_int_equal(16, block_size);

	rc = daos_array_set_size(oh, 0, 693, NULL);
	assert_int_equal(rc, 0);

	rc = daos_array_get_size(oh, 0, &size, NULL);
	assert_int_equal(rc, 0);

	rc = daos_array_close(oh, NULL);
	assert_int_equal(rc, 0);

} /* End simple_array_mgmt */

static void
contig_mem_contig_arr_io_helper(void **state, daos_size_t cell_size)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_array_ranges_t ranges;
	daos_range_t	rg;
	daos_sg_list_t	sgl;
	daos_iov_t	iov;
	int		*wbuf = NULL, *rbuf = NULL;
	daos_size_t	i;
	daos_event_t	ev, *evp;
	int		rc;

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);

	/** create the array */
	rc = daos_array_create(arg->coh, oid, 0, cell_size, block_size, &oh,
			       NULL);
	assert_int_equal(rc, 0);

	/** Allocate and set buffer */
	wbuf = malloc(NUM_ELEMS*sizeof(int));
	assert_non_null(wbuf);
	rbuf = malloc(NUM_ELEMS*sizeof(int));
	assert_non_null(rbuf);
	for (i = 0; i < NUM_ELEMS; i++)
		wbuf[i] = i+1;

	/** set array location */
	ranges.ranges_nr = 1;
	rg.len = NUM_ELEMS * sizeof(int) / cell_size;
	rg.index = arg->myrank * rg.len;
	ranges.ranges = &rg;

	/** set memory location */
	sgl.sg_nr.num = 1;
	daos_iov_set(&iov, wbuf, NUM_ELEMS * sizeof(int));
	sgl.sg_iovs = &iov;

	/** Write */
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}
	rc = daos_array_write(oh, 0, &ranges, &sgl, NULL,
			      arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}

	/** Read */
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}
	daos_iov_set(&iov, rbuf, NUM_ELEMS * sizeof(int));
	sgl.sg_iovs = &iov;
	rc = daos_array_read(oh, 0, &ranges, &sgl, NULL,
			     arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}

	/** Verify data */
	for (i = 0; i < NUM_ELEMS; i++) {
		if (wbuf[i] != rbuf[i]) {
			printf("Data verification failed\n");
			printf("%zu: written %d != read %d\n",
				i, wbuf[i], rbuf[i]);
		}
		assert_int_equal(wbuf[i], rbuf[i]);
	}

	free(rbuf);
	free(wbuf);

	{
		daos_size_t array_size;

		rc = daos_array_get_size(oh, 0, &array_size, NULL);
		assert_int_equal(rc, 0);
	}

	rc = daos_array_close(oh, NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}
} /* End contig_mem_contig_arr_io_helper */

static void
contig_mem_contig_arr_io(void **state) {
	print_message("Testing with cell size = 1B\n");
	contig_mem_contig_arr_io_helper(state, 1);
	print_message("Testing with cell size = 4B\n");
	contig_mem_contig_arr_io_helper(state, 4);
}

static void
contig_mem_str_arr_io_helper(void **state, daos_size_t cell_size)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_array_ranges_t ranges;
	daos_sg_list_t	sgl;
	daos_iov_t	iov;
	int		*wbuf = NULL, *rbuf = NULL;
	daos_size_t	i;
	daos_event_t	ev, *evp;
	int		rc;

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);

	/** create the array */
	rc = daos_array_create(arg->coh, oid, 0, cell_size, block_size, &oh,
			       NULL);
	assert_int_equal(rc, 0);

	/** Allocate and set buffer */
	wbuf = malloc(NUM_ELEMS * sizeof(int));
	assert_non_null(wbuf);
	rbuf = malloc(NUM_ELEMS * sizeof(int));
	assert_non_null(rbuf);
	for (i = 0; i < NUM_ELEMS; i++)
		wbuf[i] = i+1;

	/** set array location */
	ranges.ranges_nr = NUM_ELEMS;
	ranges.ranges = (daos_range_t *)malloc(sizeof(daos_range_t) *
					       NUM_ELEMS);
	assert_non_null(ranges.ranges);

	for (i = 0; i < NUM_ELEMS; i++) {
		ranges.ranges[i].len = sizeof(int) / cell_size;
		ranges.ranges[i].index = i * arg->rank_size * 4 +
			arg->myrank * 4 + i * block_size;
	}

	/** set memory location */
	sgl.sg_nr.num = 1;
	daos_iov_set(&iov, wbuf, NUM_ELEMS * sizeof(int));
	sgl.sg_iovs = &iov;

	/** Write */
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}
	rc = daos_array_write(oh, 0, &ranges, &sgl, NULL,
			      arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}

	/** Read */
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}
	daos_iov_set(&iov, rbuf, NUM_ELEMS * sizeof(int));
	rc = daos_array_read(oh, 0, &ranges, &sgl, NULL,
			     arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}

	/** Verify data */
	for (i = 0; i < NUM_ELEMS; i++) {
		if (wbuf[i] != rbuf[i]) {
			printf("Data verification failed\n");
			printf("%zu: written %d != read %d\n",
				i, wbuf[i], rbuf[i]);
		}
		assert_int_equal(wbuf[i], rbuf[i]);
	}

	free(rbuf);
	free(wbuf);
	free(ranges.ranges);

	{
		daos_size_t array_size;

		rc = daos_array_get_size(oh, 0, &array_size, NULL);
		assert_int_equal(rc, 0);
	}

	rc = daos_array_close(oh, NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}
} /* End contig_mem_str_arr_io_helper */

static void
contig_mem_str_arr_io(void **state) {
	print_message("Testing with cell size = 1B\n");
	contig_mem_str_arr_io_helper(state, 1);
	print_message("Testing with cell size = 4B\n");
	contig_mem_str_arr_io_helper(state, 4);
}

static void
str_mem_str_arr_io_helper(void **state, daos_size_t cell_size)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_array_ranges_t ranges;
	daos_sg_list_t	sgl;
	int		*wbuf[NUM_SEGS], *rbuf[NUM_SEGS];
	daos_size_t	i, j;
	daos_event_t	ev, *evp;
	int		rc;

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);

	/** create the array */
	rc = daos_array_create(arg->coh, oid, 0, cell_size, block_size, &oh,
			       NULL);
	assert_int_equal(rc, 0);

	/** Allocate and set buffer */
	for (i = 0; i < NUM_SEGS; i++) {
		wbuf[i] = malloc((NUM_ELEMS/NUM_SEGS) * sizeof(int));
		assert_non_null(wbuf[i]);
		for (j = 0; j < NUM_ELEMS/NUM_SEGS; j++)
			wbuf[i][j] = (i * NUM_ELEMS) + j;
		rbuf[i] = malloc((NUM_ELEMS/NUM_SEGS) * sizeof(int));
		assert_non_null(rbuf[i]);
	}

	/** set array location */
	ranges.ranges_nr = NUM_ELEMS;
	ranges.ranges = (daos_range_t *)malloc(sizeof(daos_range_t) *
					       NUM_ELEMS);
	assert_non_null(ranges.ranges);

	for (i = 0; i < NUM_ELEMS; i++) {
		ranges.ranges[i].len = sizeof(int) / cell_size;
		ranges.ranges[i].index = i * arg->rank_size * 4 +
			arg->myrank * 4 + i * block_size;
	}

	/** set memory location */
	sgl.sg_nr.num = NUM_SEGS;
	sgl.sg_iovs = (daos_iov_t *)malloc(sizeof(daos_iov_t) * NUM_SEGS);
	assert_non_null(sgl.sg_iovs);

	for (i = 0; i < NUM_SEGS; i++) {
		daos_iov_set(&sgl.sg_iovs[i], wbuf[i],
			     (NUM_ELEMS/NUM_SEGS) * sizeof(int));
	}

	/** Write */
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}
	rc = daos_array_write(oh, 0, &ranges, &sgl, NULL,
			      arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}

	/** Read */
	for (i = 0; i < NUM_SEGS; i++) {
		daos_iov_set(&sgl.sg_iovs[i], rbuf[i],
			     (NUM_ELEMS/NUM_SEGS) * sizeof(int));
	}
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}
	rc = daos_array_read(oh, 0, &ranges, &sgl, NULL,
			     arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}

	/** Verify data */
	for (i = 0; i < NUM_SEGS; i++) {
		for (j = 0; j < NUM_ELEMS/NUM_SEGS; j++) {
			if (wbuf[i][j] != rbuf[i][j]) {
				printf("Data verification failed\n");
				printf("%zu: written %d != read %d\n",
				       i, wbuf[i][j], rbuf[i][j]);
			}
			assert_int_equal(wbuf[i][j], rbuf[i][j]);
		}
	}

	for (i = 0; i < NUM_SEGS; i++) {
		free(rbuf[i]);
		free(wbuf[i]);
	}
	free(ranges.ranges);
	free(sgl.sg_iovs);

	{
		daos_size_t array_size;

		rc = daos_array_get_size(oh, 0, &array_size, NULL);
		assert_int_equal(rc, 0);
	}

	rc = daos_array_close(oh, NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}
} /* End str_mem_str_arr_io */

static void
str_mem_str_arr_io(void **state) {
	print_message("Testing with cell size = 1B\n");
	str_mem_str_arr_io_helper(state, 1);
	print_message("Testing with cell size = 4B\n");
	str_mem_str_arr_io_helper(state, 4);
}

static void
read_empty_records(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_array_ranges_t ranges;
	daos_sg_list_t	sgl;
	daos_iov_t	iov;
	int		*wbuf = NULL, *rbuf = NULL;
	daos_size_t	i;
	daos_event_t	ev;
	int		rc;

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	/** create the array */
	rc = daos_array_create(arg->coh, oid, 0, 1, block_size, &oh,
			       NULL);
	assert_int_equal(rc, 0);

	/** Allocate and set buffer */
	wbuf = malloc(NUM_ELEMS * sizeof(int));
	assert_non_null(wbuf);
	rbuf = malloc(NUM_ELEMS * sizeof(int));
	assert_non_null(rbuf);
	for (i = 0; i < NUM_ELEMS; i++) {
		wbuf[i] = i+1;
		rbuf[i] = wbuf[i];
	}

	/** set memory location */
	sgl.sg_nr.num = 1;
	daos_iov_set(&iov, wbuf, NUM_ELEMS * sizeof(int));
	sgl.sg_iovs = &iov;

	/** set array location */
	ranges.ranges_nr = NUM_ELEMS;
	ranges.ranges = (daos_range_t *)malloc(sizeof(daos_range_t) *
					       NUM_ELEMS);
	assert_non_null(ranges.ranges);

	/** Read from empty array */
	for (i = 0; i < NUM_ELEMS; i++) {
		ranges.ranges[i].len = sizeof(int);
		ranges.ranges[i].index = i * arg->rank_size * sizeof(int) +
			arg->myrank * sizeof(int);
	}
	daos_iov_set(&iov, rbuf, NUM_ELEMS * sizeof(int));
	rc = daos_array_read(oh, 0, &ranges, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);

	MPI_Barrier(MPI_COMM_WORLD);

	/** Verify data */
	for (i = 0; i < NUM_ELEMS; i++) {
		if (wbuf[i] != rbuf[i]) {
			printf("Data verification failed\n");
			printf("%zu: written %d != read %d\n",
				i, wbuf[i], rbuf[i]);
		}
		assert_int_equal(wbuf[i], rbuf[i]);
	}

	/** Write segmented */
	for (i = 0; i < NUM_ELEMS; i++) {
		ranges.ranges[i].len = sizeof(int);
		ranges.ranges[i].index = i * arg->rank_size * sizeof(int) +
			arg->myrank * sizeof(int) +
			i * NUM_ELEMS * sizeof(int);
	}
	rc = daos_array_write(oh, 0, &ranges, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);

	MPI_Barrier(MPI_COMM_WORLD);

	/** Read from empty records */
	for (i = 0; i < NUM_ELEMS; i++) {
		ranges.ranges[i].len = sizeof(int);
		ranges.ranges[i].index = i * sizeof(int) +
			arg->myrank * sizeof(int);
	}
	daos_iov_set(&iov, rbuf, NUM_ELEMS * sizeof(int));
	/* rc = daos_array_read(oh, 0, &ranges, &sgl, NULL, NULL); */
	assert_int_equal(rc, 0);

	/** Verify data */
	assert_int_equal(wbuf[0], rbuf[0]);
	for (i = 1; i < NUM_ELEMS; i++) {
		/**
		 * MSC - Bug DAOS-187
		 * assert_int_equal(rbuf[i], wbuf[i]);
		 */
	}

	free(rbuf);
	free(wbuf);
	free(ranges.ranges);

	{
		daos_size_t array_size;

		rc = daos_array_get_size(oh, 0, &array_size, NULL);
		assert_int_equal(rc, 0);
	}

	rc = daos_array_close(oh, NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}
} /* End read_empty_records */

static const struct CMUnitTest array_io_tests[] = {
	{"Array I/O: create/open/close (blocking)",
	 simple_array_mgmt, async_disable, NULL},
	{"Array I/O: Contiguous memory and array (blocking)",
	 contig_mem_contig_arr_io, async_disable, NULL},
	{"Array I/O: Contiguous memory and array (non-blocking)",
	contig_mem_contig_arr_io, async_enable, NULL},
	{"Array I/O: Contiguous memory Strided array (blocking)",
	 contig_mem_str_arr_io, async_disable, NULL},
	{"Array I/O: Contiguous memory Strided array (non-blocking)",
	contig_mem_str_arr_io, async_enable, NULL},
	{"Array I/O: Strided memory and array (blocking)",
	 str_mem_str_arr_io, async_disable, NULL},
	{"Array I/O: Strided memory and array (non-blocking)",
	str_mem_str_arr_io, async_enable, NULL},
	{"Array I/O: Read from Empty array & records (blocking)",
	 read_empty_records, async_disable, NULL},
};

static int
setup(void **state)
{
	test_arg_t	*arg;
	int		 rc;

	arg = malloc(sizeof(test_arg_t));
	if (arg == NULL)
		return -1;

	rc = daos_eq_create(&arg->eq);
	if (rc)
		return rc;

	arg->svc.rl_nr.num = 3;
	arg->svc.rl_nr.num_out = 0;
	arg->svc.rl_ranks = arg->ranks;

	arg->hdl_share = false;
	uuid_clear(arg->pool_uuid);
	MPI_Comm_rank(MPI_COMM_WORLD, &arg->myrank);
	MPI_Comm_size(MPI_COMM_WORLD, &arg->rank_size);

	if (arg->myrank == 0) {
		/** create pool with minimal size */
		rc = daos_pool_create(0731, geteuid(), getegid(), NULL,
				      NULL, "pmem", 256 << 20, &arg->svc,
				      arg->pool_uuid, NULL);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	if (arg->myrank == 0) {
		/** connect to pool */
		arg->svc.rl_nr.num = arg->svc.rl_nr.num_out;
		rc = daos_pool_connect(arg->pool_uuid, NULL,
				       &arg->svc, DAOS_PC_RW, &arg->poh,
				       &arg->pool_info, NULL /* ev */);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;
	MPI_Bcast(&arg->pool_info, sizeof(arg->pool_info), MPI_CHAR, 0,
		  MPI_COMM_WORLD);

	/** l2g and g2l the pool handle */
	handle_share(&arg->poh, HANDLE_POOL, arg->myrank, arg->poh, 1);
	if (arg->myrank == 0) {
		/** create container */
		uuid_generate(arg->co_uuid);
		rc = daos_cont_create(arg->poh, arg->co_uuid, NULL);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	if (arg->myrank == 0) {
		/** open container */
		rc = daos_cont_open(arg->poh, arg->co_uuid, DAOS_COO_RW,
				    &arg->coh, NULL, NULL);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** l2g and g2l the container handle */
	handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->poh, 1);

	*state = arg;
	return 0;
}

static int
teardown(void **state) {
	test_arg_t	*arg = *state;
	int		 rc, rc_reduce = 0;

	MPI_Barrier(MPI_COMM_WORLD);

	rc = daos_cont_close(arg->coh, NULL);
	MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
	if (rc_reduce)
		return rc_reduce;

	if (arg->myrank == 0)
		rc = daos_cont_destroy(arg->poh, arg->co_uuid, 1, NULL);
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	rc = daos_pool_disconnect(arg->poh, NULL /* ev */);
	MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
	if (rc_reduce)
		return rc_reduce;

	if (arg->myrank == 0)
		rc = daos_pool_destroy(arg->pool_uuid, NULL, 1, NULL);

	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	rc = daos_eq_destroy(arg->eq, 0);
	if (rc)
		return rc;

	free(arg);
	return 0;
}

int
run_array_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("Array io tests", array_io_tests,
					 setup, teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
