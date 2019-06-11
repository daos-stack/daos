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
/**
 * This file is part of daos_m
 *
 * src/tests/addons/
 */

#include <daos_types.h>
#include <daos_addons.h>
#include "daos_test.h"
#include "daos_addons_test.h"

/** number of elements to write to array */
#define NUM_ELEMS	64
/** num of mem segments for strided access - Must evenly divide NUM_ELEMS */
#define NUM_SEGS	4
/** Object class to use in tests */
#define DTS_OCLASS_DEF	DAOS_OC_REPL_MAX_RW

static daos_size_t chunk_size = 16;
static daos_ofeat_t feat = DAOS_OF_DKEY_UINT64 | DAOS_OF_AKEY_HASHED;

static void simple_array_mgmt(void **state);
static void contig_mem_contig_arr_io(void **state);
static void contig_mem_str_arr_io(void **state);
static void str_mem_str_arr_io(void **state);
static void read_empty_records(void **state);

static void
array_oh_share(daos_handle_t coh, int rank, daos_handle_t *oh)
{
	d_iov_t	ghdl = { NULL, 0, 0 };
	int		rc;

	if (rank == 0) {
		/** fetch size of global handle */
		rc = daos_array_local2global(*oh, &ghdl);
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
		rc = daos_array_local2global(*oh, &ghdl);
		assert_int_equal(rc, 0);
	}

	/** broadcast global handle to all peers */
	rc = MPI_Bcast(ghdl.iov_buf, ghdl.iov_len, MPI_BYTE, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

	if (rank != 0) {
		/** unpack global handle */
		rc = daos_array_global2local(coh, ghdl, 0, oh);
		assert_int_equal(rc, 0);
	}

	D_FREE(ghdl.iov_buf);

	MPI_Barrier(MPI_COMM_WORLD);
}

static void
simple_array_mgmt(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_size_t	cell_size = 0, csize = 0;
	daos_size_t	size;
	int		rc;

	/** create the array with HASHED DKEY, should FAIL */
	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, 0, arg->myrank);
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 4, chunk_size,
			       &oh, NULL);
	assert_int_equal(rc, -DER_INVAL);

	/** create the array with LEXICAL DKEY, should FAIL */
	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, DAOS_OF_DKEY_LEXICAL,
			  arg->myrank);
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 4, chunk_size,
			       &oh, NULL);
	assert_int_equal(rc, -DER_INVAL);

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, feat, arg->myrank);

	/** create the array */
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 4, chunk_size,
			       &oh, NULL);
	assert_int_equal(rc, 0);

	rc = daos_array_set_size(oh, DAOS_TX_NONE, 265, NULL);
	assert_int_equal(rc, 0);
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &size, NULL);
	assert_int_equal(rc, 0);
	if (size != 265) {
		print_error("Size = %zu, expected: 265\n", size);
		assert_int_equal(size, 265);
	}

	rc = daos_array_close(oh, NULL);
	assert_int_equal(rc, 0);

	/** open the array */
	rc = daos_array_open(arg->coh, oid, DAOS_TX_NONE, DAOS_OO_RW,
			     &cell_size, &csize, &oh, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(4, cell_size);
	assert_int_equal(chunk_size, csize);

	rc = daos_array_set_size(oh, DAOS_TX_NONE, 112, NULL);
	assert_int_equal(rc, 0);
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &size, NULL);
	assert_int_equal(rc, 0);
	if (size != 112) {
		print_error("Size = %zu, expected: 112\n", size);
		assert_int_equal(size, 112);
	}

	rc = daos_array_set_size(oh, DAOS_TX_NONE, 0, NULL);
	assert_int_equal(rc, 0);
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &size, NULL);
	assert_int_equal(rc, 0);
	if (size != 0) {
		print_error("Size = %zu, expected: 0\n", size);
		assert_int_equal(size, 0);
	}

	rc = daos_array_set_size(oh, DAOS_TX_NONE, 1048576, NULL);
	assert_int_equal(rc, 0);
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &size, NULL);
	assert_int_equal(rc, 0);
	if (size != 1048576) {
		print_error("Size = %zu, expected: 1048576\n", size);
		assert_int_equal(size, 1048576);
	}

	rc = daos_array_destroy(oh, DAOS_TX_NONE, NULL);
	assert_int_equal(rc, 0);

	daos_handle_t temp_oh;

	rc = daos_array_open(arg->coh, oid, DAOS_TX_NONE, DAOS_OO_RW,
			     &cell_size, &csize, &temp_oh, NULL);
	assert_int_equal(rc, -DER_NO_PERM);

	rc = daos_array_close(oh, NULL);
	assert_int_equal(rc, 0);
	MPI_Barrier(MPI_COMM_WORLD);
} /* End simple_array_mgmt */

#define BUFLEN 80

static void
small_io(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_array_iod_t iod;
	d_sg_list_t	sgl;
	daos_range_t	rg;
	d_iov_t	iov;
	char		buf[BUFLEN], rbuf[BUFLEN];
	daos_size_t	array_size;
	int		rc;

	MPI_Barrier(MPI_COMM_WORLD);
	oid = dts_oid_gen(DAOS_OC_LARGE_RW, feat, arg->myrank);

	/** create the array */
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 1, 1048576, &oh,
			       NULL);
	assert_int_equal(rc, 0);

	memset(buf, 'A', BUFLEN);

	/** set array location */
	iod.arr_nr = 1;
	rg.rg_len = BUFLEN;
	rg.rg_idx = 0;
	iod.arr_rgs = &rg;

	/** set memory location */
	sgl.sg_nr = 1;
	d_iov_set(&iov, buf, BUFLEN);
	sgl.sg_iovs = &iov;

	/** Write */
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(array_size, BUFLEN);

	d_iov_set(&iov, rbuf, BUFLEN);
	sgl.sg_iovs = &iov;
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);

	rc = memcmp(buf, rbuf, BUFLEN);
	assert_int_equal(rc, 0);

	rc = daos_array_close(oh, NULL);
	assert_int_equal(rc, 0);
	MPI_Barrier(MPI_COMM_WORLD);
} /* End str_mem_str_arr_io */

static int
change_array_size(test_arg_t *arg, daos_handle_t oh, daos_size_t array_size)
{
	daos_size_t expected_size;
	daos_size_t new_size, i;
	int rc;

	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank != 0)
		goto out;

	expected_size = array_size*2;
	rc = daos_array_set_size(oh, DAOS_TX_NONE, expected_size, NULL);
	assert_int_equal(rc, 0);
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &new_size, NULL);
	assert_int_equal(rc, 0);
	if (new_size != expected_size) {
		print_error("(%d) Size = %zu, expected: %zu\n",
			    arg->myrank, new_size, expected_size);
		rc = -1;
		goto out;
	}

	expected_size = array_size/2;
	rc = daos_array_set_size(oh, DAOS_TX_NONE, expected_size, NULL);
	assert_int_equal(rc, 0);
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &new_size, NULL);
	assert_int_equal(rc, 0);
	if (new_size != expected_size) {
		print_error("(%d) Size = %zu, expected: %zu\n",
			    arg->myrank, new_size, expected_size);
		rc = -1;
		goto out;
	}

	for (i = 0; i < 5; i++) {
		rc = daos_array_set_size(oh, DAOS_TX_NONE, 0, NULL);
		assert_int_equal(rc, 0);
		rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
		assert_int_equal(rc, 0);
		if (array_size != 0) {
			print_error("Size = %zu, expected: 0\n", array_size);
			assert_int_equal(array_size, 0);
		}

		rc = daos_array_set_size(oh, DAOS_TX_NONE, 265 + i, NULL);
		assert_int_equal(rc, 0);
		rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
		assert_int_equal(rc, 0);
		if (array_size != 265 + i) {
			print_error("Size = %zu, expected: %zu\n",
				    array_size, 265 + i);
			assert_int_equal(array_size, 265 + i);
		}

		rc = daos_array_set_size(oh, DAOS_TX_NONE, 0, NULL);
		assert_int_equal(rc, 0);
		rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
		assert_int_equal(rc, 0);
		if (array_size != 0) {
			print_error("Size = %zu, expected: 0\n", array_size);
			assert_int_equal(array_size, 0);
		}
	}

out:
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	return rc;
}

static void
contig_mem_contig_arr_io_helper(void **state, daos_size_t cell_size)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_array_iod_t iod;
	daos_range_t	rg;
	d_sg_list_t	sgl;
	d_iov_t	iov;
	int		*wbuf = NULL, *rbuf = NULL;
	daos_size_t	i;
	daos_event_t	ev, *evp;
	int		rc;

	MPI_Barrier(MPI_COMM_WORLD);
	/** create the array on rank 0 and share the oh. */
	if (arg->myrank == 0) {
		oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, feat, 0);
		rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, cell_size,
				       chunk_size, &oh, NULL);
		assert_int_equal(rc, 0);
	}
	array_oh_share(arg->coh, arg->myrank, &oh);

	/** Allocate and set buffer */
	D_ALLOC_ARRAY(wbuf, NUM_ELEMS);
	assert_non_null(wbuf);
	D_ALLOC_ARRAY(rbuf, NUM_ELEMS);
	assert_non_null(rbuf);
	for (i = 0; i < NUM_ELEMS; i++)
		wbuf[i] = i+1;

	/** set array location */
	iod.arr_nr = 1;
	rg.rg_len = NUM_ELEMS * sizeof(int) / cell_size;
	rg.rg_idx = arg->myrank * rg.rg_len;
	iod.arr_rgs = &rg;

	/** set memory location */
	sgl.sg_nr = 1;
	d_iov_set(&iov, wbuf, NUM_ELEMS * sizeof(int));
	sgl.sg_iovs = &iov;

	/** Write */
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl, NULL,
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
	d_iov_set(&iov, rbuf, NUM_ELEMS * sizeof(int));
	sgl.sg_iovs = &iov;
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL,
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

	D_FREE(rbuf);
	D_FREE(wbuf);

	MPI_Barrier(MPI_COMM_WORLD);

	daos_size_t array_size;
	daos_size_t expected_size;

	expected_size = arg->rank_size * (NUM_ELEMS * sizeof(int) / cell_size);

	rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
	assert_int_equal(rc, 0);

	if (array_size != expected_size)
		print_error("(%d) Size = %zu, expected: %zu\n",
			arg->myrank, array_size, expected_size);
	assert_int_equal(array_size, expected_size);

	/** punch holes in the array, but do not change the size */
	iod.arr_nr = 1;
	rg.rg_len = (NUM_ELEMS / 2) * (sizeof(int) / cell_size);
	rg.rg_idx = arg->myrank * rg.rg_len;
	iod.arr_rgs = &rg;

	rc = daos_array_punch(oh, DAOS_TX_NONE, &iod, NULL);
	assert_int_equal(rc, 0);

	MPI_Barrier(MPI_COMM_WORLD);

	/** Verify size is still the same */
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
	assert_int_equal(rc, 0);

	if (array_size != expected_size)
		fprintf(stderr, "(%d) Size = %zu, expected: %zu\n",
			arg->myrank, array_size, expected_size);
	assert_int_equal(array_size, expected_size);

	/** TODO - punch at the end to shrink the size and verify. */

	rc = change_array_size(arg, oh, array_size);
	assert_int_equal(rc, 0);

	rc = daos_array_close(oh, NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);
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
	daos_array_iod_t iod;
	d_sg_list_t	sgl;
	d_iov_t	iov;
	int		*wbuf = NULL, *rbuf = NULL;
	daos_size_t	len, i;
	daos_event_t	ev, *evp;
	int		rc;

	MPI_Barrier(MPI_COMM_WORLD);

	/** create the array on rank 0 and share the oh. */
	if (arg->myrank == 0) {
		oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, feat, 0);
		rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, cell_size,
				       chunk_size, &oh, NULL);
		assert_int_equal(rc, 0);
	}
	array_oh_share(arg->coh, arg->myrank, &oh);

	/** Allocate and set buffer */
	D_ALLOC_ARRAY(wbuf, NUM_ELEMS);
	assert_non_null(wbuf);
	D_ALLOC_ARRAY(rbuf, NUM_ELEMS);
	assert_non_null(rbuf);
	for (i = 0; i < NUM_ELEMS; i++)
		wbuf[i] = i+1;

	/** set array location */
	iod.arr_nr = NUM_ELEMS;
	D_ALLOC_ARRAY(iod.arr_rgs, NUM_ELEMS);
	assert_non_null(iod.arr_rgs);

	len = sizeof(int) / cell_size;
	for (i = 0; i < NUM_ELEMS; i++) {
		iod.arr_rgs[i].rg_len = len;
		iod.arr_rgs[i].rg_idx = i * arg->rank_size * len +
			arg->myrank * len;
	}

	/** set memory location */
	sgl.sg_nr = 1;
	d_iov_set(&iov, wbuf, NUM_ELEMS * sizeof(int));
	sgl.sg_iovs = &iov;

	/** Write */
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl, NULL,
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
	d_iov_set(&iov, rbuf, NUM_ELEMS * sizeof(int));
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL,
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

	D_FREE(rbuf);
	D_FREE(wbuf);

	MPI_Barrier(MPI_COMM_WORLD);

	daos_size_t expected_size;
	daos_size_t array_size = 0;

	expected_size = NUM_ELEMS * arg->rank_size * len;
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
	assert_int_equal(rc, 0);

	if (array_size != expected_size) {
		print_error("(%d) Size = %zu, expected: %zu\n",
			arg->myrank, array_size, expected_size);
	}
	assert_int_equal(array_size, expected_size);

	/** punch holes in the array, but do not change the size */
	iod.arr_nr = NUM_ELEMS / 2;
	rc = daos_array_punch(oh, DAOS_TX_NONE, &iod, NULL);
	assert_int_equal(rc, 0);

	/** Verify size is still the same */
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
	assert_int_equal(rc, 0);

	if (array_size != expected_size)
		fprintf(stderr, "(%d) Size = %zu, expected: %zu\n",
			arg->myrank, array_size, expected_size);
	assert_int_equal(array_size, expected_size);

	/** TODO - punch at the end to shrink the size and verify. */

	rc = change_array_size(arg, oh, array_size);
	assert_int_equal(rc, 0);

	rc = daos_array_close(oh, NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}

	D_FREE(iod.arr_rgs);
	MPI_Barrier(MPI_COMM_WORLD);
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
	daos_array_iod_t iod;
	d_sg_list_t	sgl;
	int		*wbuf[NUM_SEGS], *rbuf[NUM_SEGS];
	daos_size_t	i, j, len;
	daos_event_t	ev, *evp;
	int		rc;

	MPI_Barrier(MPI_COMM_WORLD);
	/** create the array on rank 0 and share the oh. */
	if (arg->myrank == 0) {
		oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, feat, 0);
		rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, cell_size,
				       chunk_size, &oh, NULL);
		assert_int_equal(rc, 0);
	}
	array_oh_share(arg->coh, arg->myrank, &oh);

	/** Allocate and set buffer */
	for (i = 0; i < NUM_SEGS; i++) {
		D_ALLOC_ARRAY(wbuf[i], (NUM_ELEMS/NUM_SEGS));
		assert_non_null(wbuf[i]);
		for (j = 0; j < NUM_ELEMS/NUM_SEGS; j++)
			wbuf[i][j] = (i * NUM_ELEMS) + j;
		D_ALLOC_ARRAY(rbuf[i], (NUM_ELEMS/NUM_SEGS));
		assert_non_null(rbuf[i]);
	}

	/** set array location */
	iod.arr_nr = NUM_ELEMS;
	D_ALLOC_ARRAY(iod.arr_rgs, NUM_ELEMS);
	assert_non_null(iod.arr_rgs);

	len = sizeof(int) / cell_size;
	for (i = 0; i < NUM_ELEMS; i++) {
		iod.arr_rgs[i].rg_len = len;
		iod.arr_rgs[i].rg_idx = i * arg->rank_size * len +
			arg->myrank * len;
	}

	/** set memory location */
	sgl.sg_nr = NUM_SEGS;
	D_ALLOC_ARRAY(sgl.sg_iovs, NUM_SEGS);
	assert_non_null(sgl.sg_iovs);

	for (i = 0; i < NUM_SEGS; i++) {
		d_iov_set(&sgl.sg_iovs[i], wbuf[i],
			     (NUM_ELEMS/NUM_SEGS) * sizeof(int));
	}

	/** Write */
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl, NULL,
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
		d_iov_set(&sgl.sg_iovs[i], rbuf[i],
			     (NUM_ELEMS/NUM_SEGS) * sizeof(int));
	}
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL,
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
		D_FREE(rbuf[i]);
		D_FREE(wbuf[i]);
	}
	D_FREE(iod.arr_rgs);
	D_FREE(sgl.sg_iovs);

	MPI_Barrier(MPI_COMM_WORLD);

	daos_size_t array_size;
	daos_size_t expected_size;

	expected_size = NUM_ELEMS * arg->rank_size * len;
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
	assert_int_equal(rc, 0);

	if (array_size != expected_size) {
		print_error("(%d) Size = %zu, expected: %zu\n",
			    arg->myrank, array_size, expected_size);
	}
	assert_int_equal(array_size, expected_size);

	rc = change_array_size(arg, oh, array_size);
	assert_int_equal(rc, 0);

	rc = daos_array_close(oh, NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);
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
	daos_array_iod_t iod;
	d_sg_list_t	sgl;
	d_iov_t	iov;
	int		*wbuf = NULL, *rbuf = NULL;
	daos_size_t	i;
	daos_event_t	ev;
	int		rc;

	MPI_Barrier(MPI_COMM_WORLD);
	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, feat, arg->myrank);

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	/** create the array */
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 1, chunk_size,
			       &oh, NULL);
	assert_int_equal(rc, 0);

	/** Allocate and set buffer */
	D_ALLOC_ARRAY(wbuf, NUM_ELEMS);
	assert_non_null(wbuf);
	D_ALLOC_ARRAY(rbuf, NUM_ELEMS);
	assert_non_null(rbuf);
	for (i = 0; i < NUM_ELEMS; i++) {
		wbuf[i] = i+1;
		rbuf[i] = wbuf[i];
	}

	/** set memory location */
	sgl.sg_nr = 1;
	d_iov_set(&iov, wbuf, NUM_ELEMS * sizeof(int));
	sgl.sg_iovs = &iov;

	/** set array location */
	iod.arr_nr = NUM_ELEMS;
	D_ALLOC_ARRAY(iod.arr_rgs, NUM_ELEMS);
	assert_non_null(iod.arr_rgs);

	/** Read from empty array */
	for (i = 0; i < NUM_ELEMS; i++) {
		iod.arr_rgs[i].rg_len = sizeof(int);
		iod.arr_rgs[i].rg_idx = i * arg->rank_size * sizeof(int) +
			arg->myrank * sizeof(int);
	}
	d_iov_set(&iov, rbuf, NUM_ELEMS * sizeof(int));
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL, NULL);
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
		iod.arr_rgs[i].rg_len = sizeof(int);
		iod.arr_rgs[i].rg_idx = i * arg->rank_size * sizeof(int) +
			arg->myrank * sizeof(int) +
			i * NUM_ELEMS * sizeof(int);
	}
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);

	MPI_Barrier(MPI_COMM_WORLD);

	/** Read from empty records */
	for (i = 0; i < NUM_ELEMS; i++) {
		iod.arr_rgs[i].rg_len = sizeof(int);
		iod.arr_rgs[i].rg_idx = i * sizeof(int) +
			arg->myrank * sizeof(int);
	}
	d_iov_set(&iov, rbuf, NUM_ELEMS * sizeof(int));
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);

	/** Verify data */
	assert_int_equal(wbuf[0], rbuf[0]);
	for (i = 1; i < NUM_ELEMS; i++)
		assert_int_equal(rbuf[i], wbuf[i]);

	D_FREE(rbuf);
	D_FREE(wbuf);
	D_FREE(iod.arr_rgs);

	rc = daos_array_close(oh, NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);
} /* End read_empty_records */

#define NUM 5000

static void
strided_array(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_array_iod_t iod;
	d_sg_list_t	sgl;
	int		*buf;
	daos_size_t	i, j, nerrors = 0;
	int		rc;

	MPI_Barrier(MPI_COMM_WORLD);
	oid = dts_oid_gen(DAOS_OC_LARGE_RW, feat, arg->myrank);

	/** create the array */
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 1, 1048576, &oh,
			       NULL);
	assert_int_equal(rc, 0);

	/** Allocate and set buffer */
	D_ALLOC_ARRAY(buf, (NUM * 2));
	assert_non_null(buf);

	for (i = 0; i < NUM * 2; i++)
		buf[i] = i+1;

	/** set array location */
	iod.arr_nr = NUM;
	D_ALLOC_ARRAY(iod.arr_rgs, NUM);
	assert_non_null(iod.arr_rgs);

	j = 0;
	for (i = 0; i < NUM; i++) {
		j = 2 * sizeof(int) * i;
		iod.arr_rgs[i].rg_len = sizeof(int);
		iod.arr_rgs[i].rg_idx = j;
	}

	/** set memory location */
	sgl.sg_nr = NUM;
	D_ALLOC_ARRAY(sgl.sg_iovs, NUM);
	j = 0;
	for (i = 0 ; i < NUM; i++) {
		d_iov_set(&sgl.sg_iovs[i], &buf[j], sizeof(int));
		j += 2;
	}

	/** Write */
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);

	for (i = 0; i < NUM * 2; i++)
		buf[i] = -1;

	/** Read */
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);

	/** Verify data */
	for (i = 0; i < NUM * 2; i++) {
		if (i%2 == 0) {
			if (buf[i] != i+1) {
				printf("Data verification failed\n");
				printf("%zu: written %zu != read %d\n",
				       i, i+1, buf[i]);
				nerrors++;
			}
		} else {
			if (buf[i] != -1)
				nerrors++;
		}
	}

	if (nerrors)
		print_message("Data verification found %zu errors\n", nerrors);

	D_FREE(buf);
	D_FREE(iod.arr_rgs);

	assert_int_equal(nerrors, 0);
	MPI_Barrier(MPI_COMM_WORLD);
} /* End str_mem_str_arr_io */

static const struct CMUnitTest array_io_tests[] = {
	{"Array I/O: create/open/close (blocking)",
	 simple_array_mgmt, async_disable, NULL},
	{"Array I/O: small/simple array IO (blocking)",
	 small_io, async_disable, NULL},
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
	{"Array I/O: strided_array (blocking)",
	 strided_array, async_disable, NULL},
};

int
array_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			  NULL);
}

int
run_array_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("Array io tests", array_io_tests,
					 array_setup, test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
