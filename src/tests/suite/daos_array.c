/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/tests/suite/daos_array.c
 */

#include <daos.h>
#include "daos_test.h"

/** number of elements to write to array */
#define NUM_ELEMS	64
/** num of mem segments for strided access - Must evenly divide NUM_ELEMS */
#define NUM_SEGS	4

static daos_size_t chunk_size = 16;
static enum daos_otype_t type = DAOS_OT_ARRAY;
static enum daos_otype_t typeb = DAOS_OT_ARRAY_BYTE;

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
		assert_rc_equal(rc, 0);
	}

	/** broadcast size of global handle to all peers */
	rc = par_bcast(PAR_COMM_WORLD, &ghdl.iov_buf_len, 1, PAR_UINT64, 0);
	assert_int_equal(rc, 0);

	/** allocate buffer for global pool handle */
	D_ALLOC(ghdl.iov_buf, ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	if (rank == 0) {
		/** generate actual global handle to share with peer tasks */
		rc = daos_array_local2global(*oh, &ghdl);
		assert_rc_equal(rc, 0);
	}

	/** broadcast global handle to all peers */
	rc = par_bcast(PAR_COMM_WORLD, ghdl.iov_buf, ghdl.iov_len, PAR_BYTE, 0);
	assert_int_equal(rc, 0);

	if (rank != 0) {
		/** unpack global handle */
		rc = daos_array_global2local(coh, ghdl, 0, oh);
		assert_rc_equal(rc, 0);
	}

	D_FREE(ghdl.iov_buf);

	par_barrier(PAR_COMM_WORLD);
}

static void
simple_array_mgmt(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh, oh2;
	daos_size_t	cell_size = 0, csize = 0;
	daos_size_t	size;
	int		rc;

	/** create the array with HASHED DKEY, should FAIL */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 4, chunk_size,
			       &oh, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	/** create the array with LEXICAL DKEY, should FAIL */
	oid = daos_test_oid_gen(arg->coh, OC_SX, DAOS_OT_DKEY_LEXICAL, 0,
			  arg->myrank);
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 4, chunk_size,
			       &oh, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	oid = daos_test_oid_gen(arg->coh, OC_SX, type, 0, arg->myrank);

	/** create the array */
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 4, chunk_size,
			       &oh, NULL);
	assert_rc_equal(rc, 0);

	/** create the same array again, should fail */
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 4, chunk_size,
			       &oh2, NULL);
	assert_rc_equal(rc, -DER_EXIST);

	rc = daos_array_get_attr(oh, &csize, &cell_size);
	assert_rc_equal(rc, 0);
	assert_int_equal(4, cell_size);
	assert_int_equal(chunk_size, csize);

	rc = daos_array_set_size(oh, DAOS_TX_NONE, 265, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &size, NULL);
	assert_rc_equal(rc, 0);
	if (size != 265) {
		print_error("Size = %zu, expected: 265\n", size);
		assert_int_equal(size, 265);
	}

	rc = daos_array_close(oh, NULL);
	assert_rc_equal(rc, 0);

	/** open the array */
	rc = daos_array_open(arg->coh, oid, DAOS_TX_NONE, DAOS_OO_RW,
			     &cell_size, &csize, &oh, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(4, cell_size);
	assert_int_equal(chunk_size, csize);

	rc = daos_array_set_size(oh, DAOS_TX_NONE, 112, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &size, NULL);
	assert_rc_equal(rc, 0);
	if (size != 112) {
		print_error("Size = %zu, expected: 112\n", size);
		assert_int_equal(size, 112);
	}

	rc = daos_array_set_size(oh, DAOS_TX_NONE, 0, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &size, NULL);
	assert_rc_equal(rc, 0);
	if (size != 0) {
		print_error("Size = %zu, expected: 0\n", size);
		assert_int_equal(size, 0);
	}

	rc = daos_array_set_size(oh, DAOS_TX_NONE, 1048576, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &size, NULL);
	assert_rc_equal(rc, 0);
	if (size != 1048576) {
		print_error("Size = %zu, expected: 1048576\n", size);
		assert_int_equal(size, 1048576);
	}

	rc = daos_array_destroy(oh, DAOS_TX_NONE, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_array_open(arg->coh, oid, DAOS_TX_NONE, DAOS_OO_RW,
			     &cell_size, &csize, &oh2, NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	rc = daos_array_close(oh, NULL);
	assert_rc_equal(rc, 0);

	/** Test the open_with_attr interface */

	/** Open_with_attr with DAOS_OT_ARRAY, should fail */
	oid = daos_test_oid_gen(arg->coh, OC_SX, type, 0, arg->myrank);
	rc = daos_array_open_with_attr(arg->coh, oid, DAOS_TX_NONE, DAOS_OO_RW,
				       4, chunk_size, &oh, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	oid = daos_test_oid_gen(arg->coh, OC_SX, DAOS_OT_ARRAY_ATTR,
				0, arg->myrank);
	rc = daos_array_open_with_attr(arg->coh, oid, DAOS_TX_NONE, DAOS_OO_RW,
				       4, chunk_size, &oh, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_array_set_size(oh, DAOS_TX_NONE, 265, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &size, NULL);
	assert_rc_equal(rc, 0);
	if (size != 265) {
		print_error("Size = %zu, expected: 265\n", size);
		assert_int_equal(size, 265);
	}

	rc = daos_array_destroy(oh, DAOS_TX_NONE, NULL);
	assert_rc_equal(rc, 0);

	/*
	 * even with array destroyed, array should be accessible since no
	 * metadata is stored.
	 */
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &size, NULL);
	assert_rc_equal(rc, 0);
	if (size != 0) {
		print_error("Size = %zu, expected: 0\n", size);
		assert_int_equal(size, 0);
	}

	rc = daos_array_close(oh, NULL);
	assert_rc_equal(rc, 0);

	par_barrier(PAR_COMM_WORLD);
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
	d_iov_t		iov;
	char		buf[BUFLEN], rbuf[BUFLEN];
	daos_size_t	array_size;
	int		rc;

	par_barrier(PAR_COMM_WORLD);
	oid = daos_test_oid_gen(arg->coh, OC_SX, typeb, 0, arg->myrank);

	/** create the array */
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 1, 1048576, &oh,
			       NULL);
	assert_rc_equal(rc, 0);

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
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(array_size, BUFLEN);

	d_iov_set(&iov, rbuf, BUFLEN);
	sgl.sg_iovs = &iov;
	iod.arr_nr_short_read = 1;
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(iod.arr_nr_short_read, 0);

	rc = memcmp(buf, rbuf, BUFLEN);
	assert_int_equal(rc, 0);

	rc = daos_array_close(oh, NULL);
	assert_rc_equal(rc, 0);
	par_barrier(PAR_COMM_WORLD);
} /* End small_io */

static int
change_array_size(test_arg_t *arg, daos_handle_t oh, daos_size_t array_size)
{
	daos_size_t expected_size;
	daos_size_t new_size, i;
	int rc;

	par_barrier(PAR_COMM_WORLD);

	if (arg->myrank != 0)
		goto out;

	expected_size = array_size*2;
	rc = daos_array_set_size(oh, DAOS_TX_NONE, expected_size, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &new_size, NULL);
	assert_rc_equal(rc, 0);
	if (new_size != expected_size) {
		print_error("(%d) Size = %zu, expected: %zu\n",
			    arg->myrank, new_size, expected_size);
		rc = -1;
		goto out;
	}

	expected_size = array_size/2;
	rc = daos_array_set_size(oh, DAOS_TX_NONE, expected_size, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &new_size, NULL);
	assert_rc_equal(rc, 0);
	if (new_size != expected_size) {
		print_error("(%d) Size = %zu, expected: %zu\n",
			    arg->myrank, new_size, expected_size);
		rc = -1;
		goto out;
	}

	for (i = 0; i < 5; i++) {
		rc = daos_array_set_size(oh, DAOS_TX_NONE, 0, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
		assert_rc_equal(rc, 0);
		if (array_size != 0) {
			print_error("Size = %zu, expected: 0\n", array_size);
			assert_int_equal(array_size, 0);
		}

		rc = daos_array_set_size(oh, DAOS_TX_NONE, 265 + i, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
		assert_rc_equal(rc, 0);
		if (array_size != 265 + i) {
			print_error("Size = %zu, expected: %zu\n",
				    array_size, 265 + i);
			assert_int_equal(array_size, 265 + i);
		}

		rc = daos_array_set_size(oh, DAOS_TX_NONE, 0, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
		assert_rc_equal(rc, 0);
		if (array_size != 0) {
			print_error("Size = %zu, expected: 0\n", array_size);
			assert_int_equal(array_size, 0);
		}
	}

out:
	par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);
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
	d_iov_t		iov;
	int		*wbuf = NULL, *rbuf = NULL;
	daos_size_t	i;
	daos_event_t	ev, *evp;
	int		rc;

	par_barrier(PAR_COMM_WORLD);
	/** create the array on rank 0 and share the oh. */
	if (arg->myrank == 0) {
		oid = daos_test_oid_gen(arg->coh, OC_SX,
					(cell_size == 1) ? typeb : type, 0, 0);
		rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, cell_size,
				       chunk_size, &oh, NULL);
		assert_rc_equal(rc, 0);
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
		assert_rc_equal(rc, 0);
	}
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl,
			      arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_rc_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}

	/** Read */
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}
	d_iov_set(&iov, rbuf, NUM_ELEMS * sizeof(int));
	sgl.sg_iovs = &iov;
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl,
			     arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_rc_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}

	/** Verify data */
	if (cell_size == 1)
		assert_int_equal(iod.arr_nr_short_read, 0);
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

	par_barrier(PAR_COMM_WORLD);

	daos_size_t array_size;
	daos_size_t expected_size;

	expected_size = arg->rank_size * (NUM_ELEMS * sizeof(int) / cell_size);

	rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
	assert_rc_equal(rc, 0);

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
	assert_rc_equal(rc, 0);

	par_barrier(PAR_COMM_WORLD);

	/** Verify size is still the same */
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
	assert_rc_equal(rc, 0);

	if (array_size != expected_size)
		fprintf(stderr, "(%d) Size = %zu, expected: %zu\n",
			arg->myrank, array_size, expected_size);
	assert_int_equal(array_size, expected_size);

	/** TODO - punch at the end to shrink the size and verify. */

	rc = change_array_size(arg, oh, array_size);
	assert_int_equal(rc, 0);

	rc = daos_array_close(oh, NULL);
	assert_rc_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}
	par_barrier(PAR_COMM_WORLD);
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
	d_iov_t		iov;
	int		*wbuf = NULL, *rbuf = NULL;
	daos_size_t	len, i;
	daos_event_t	ev, *evp;
	int		rc;

	par_barrier(PAR_COMM_WORLD);

	/** create the array on rank 0 and share the oh. */
	if (arg->myrank == 0) {
		oid = daos_test_oid_gen(arg->coh, OC_SX,
					(cell_size == 1) ? typeb : type, 0, 0);
		rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, cell_size,
				       chunk_size, &oh, NULL);
		assert_rc_equal(rc, 0);
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
		assert_rc_equal(rc, 0);
	}
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl,
			      arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_rc_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}

	/** Read */
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}
	d_iov_set(&iov, rbuf, NUM_ELEMS * sizeof(int));
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl,
			     arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_rc_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}

	/** Verify data */
	if (cell_size == 1)
		assert_int_equal(iod.arr_nr_short_read, 0);
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

	par_barrier(PAR_COMM_WORLD);

	daos_size_t expected_size;
	daos_size_t array_size = 0;

	expected_size = NUM_ELEMS * arg->rank_size * len;
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
	assert_rc_equal(rc, 0);

	if (array_size != expected_size) {
		print_error("(%d) Size = %zu, expected: %zu\n",
			arg->myrank, array_size, expected_size);
	}
	assert_int_equal(array_size, expected_size);

	/** punch holes in the array, but do not change the size */
	iod.arr_nr = NUM_ELEMS / 2;
	rc = daos_array_punch(oh, DAOS_TX_NONE, &iod, NULL);
	assert_rc_equal(rc, 0);

	/** Verify size is still the same */
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
	assert_rc_equal(rc, 0);

	if (array_size != expected_size)
		fprintf(stderr, "(%d) Size = %zu, expected: %zu\n",
			arg->myrank, array_size, expected_size);
	assert_int_equal(array_size, expected_size);

	/** TODO - punch at the end to shrink the size and verify. */

	rc = change_array_size(arg, oh, array_size);
	assert_int_equal(rc, 0);

	rc = daos_array_close(oh, NULL);
	assert_rc_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}

	D_FREE(iod.arr_rgs);
	par_barrier(PAR_COMM_WORLD);
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

	par_barrier(PAR_COMM_WORLD);
	/** create the array on rank 0 and share the oh. */
	if (arg->myrank == 0) {
		oid = daos_test_oid_gen(arg->coh, OC_SX,
					(cell_size == 1) ? typeb : type, 0, 0);
		rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, cell_size,
				       chunk_size, &oh, NULL);
		assert_rc_equal(rc, 0);
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
		assert_rc_equal(rc, 0);
	}
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl,
			      arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_rc_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}

	/** Read */
	for (i = 0; i < NUM_SEGS; i++) {
		d_iov_set(&sgl.sg_iovs[i], rbuf[i],
			     (NUM_ELEMS/NUM_SEGS) * sizeof(int));
	}
	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl,
			     arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	if (arg->async) {
		/** Wait for completion */
		rc = daos_eq_poll(arg->eq, 0, DAOS_EQ_WAIT, 1, &evp);
		assert_rc_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(evp->ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}

	/** Verify data */
	if (cell_size == 1)
		assert_int_equal(iod.arr_nr_short_read, 0);
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

	par_barrier(PAR_COMM_WORLD);

	daos_size_t array_size;
	daos_size_t expected_size;

	expected_size = NUM_ELEMS * arg->rank_size * len;
	rc = daos_array_get_size(oh, DAOS_TX_NONE, &array_size, NULL);
	assert_rc_equal(rc, 0);

	if (array_size != expected_size) {
		print_error("(%d) Size = %zu, expected: %zu\n",
			    arg->myrank, array_size, expected_size);
	}
	assert_int_equal(array_size, expected_size);

	rc = change_array_size(arg, oh, array_size);
	assert_int_equal(rc, 0);

	rc = daos_array_close(oh, NULL);
	assert_rc_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}
	par_barrier(PAR_COMM_WORLD);
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
	d_iov_t		iov;
	int		*wbuf = NULL, *rbuf = NULL;
	daos_size_t	i;
	daos_event_t	ev;
	int		rc;

	par_barrier(PAR_COMM_WORLD);
	oid = daos_test_oid_gen(arg->coh, OC_SX, typeb, 0, arg->myrank);

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}

	/** create the array */
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 1, chunk_size,
			       &oh, NULL);
	assert_rc_equal(rc, 0);

	/** Allocate and set buffer */
	D_ALLOC_ARRAY(wbuf, NUM_ELEMS);
	assert_non_null(wbuf);
	D_ALLOC_ARRAY(rbuf, NUM_ELEMS);
	assert_non_null(rbuf);
	for (i = 0; i < NUM_ELEMS; i++) {
		wbuf[i] = i+1;
		rbuf[i] = -1;
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
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(iod.arr_nr_short_read, NUM_ELEMS * sizeof(int));

	par_barrier(PAR_COMM_WORLD);

	/** Verify data - rbuf should not be touched */
	for (i = 0; i < NUM_ELEMS; i++) {
		if (rbuf[i] != -1) {
			printf("Data verification failed\n");
			printf("%zu: expected %d != read %d\n",
			       i, -1, rbuf[i]);
		}
		assert_int_equal(rbuf[i], -1);
	}

	/** Write segmented */
	for (i = 0; i < NUM_ELEMS; i++) {
		iod.arr_rgs[i].rg_idx = i * arg->rank_size * sizeof(int) +
			arg->myrank * sizeof(int) +
			i * NUM_ELEMS * sizeof(int);
	}
	d_iov_set(&iov, wbuf, NUM_ELEMS * sizeof(int));
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	par_barrier(PAR_COMM_WORLD);

	/** Read from empty records */
	for (i = 0; i < NUM_ELEMS; i++) {
		iod.arr_rgs[i].rg_idx = i * sizeof(int) +
			arg->myrank * sizeof(int);
	}
	d_iov_set(&iov, rbuf, NUM_ELEMS * sizeof(int));
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	assert_int_equal(iod.arr_nr_short_read, 0);
	assert_int_equal(iod.arr_nr_read, sizeof(int) * NUM_ELEMS);

	/** Verify data */
	assert_int_equal(wbuf[0], rbuf[0]);
	for (i = 1; i < NUM_ELEMS; i++)
		assert_int_equal(rbuf[i], 0);

	D_FREE(rbuf);
	D_FREE(wbuf);
	D_FREE(iod.arr_rgs);

	rc = daos_array_close(oh, NULL);
	assert_rc_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}
	par_barrier(PAR_COMM_WORLD);
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

	par_barrier(PAR_COMM_WORLD);
	oid = daos_test_oid_gen(arg->coh, OC_SX, typeb, 0, arg->myrank);

	/** create the array */
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 1, 1048576, &oh,
			       NULL);
	assert_rc_equal(rc, 0);

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
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < NUM * 2; i++)
		buf[i] = -1;

	/** Read */
	rc = daos_array_read(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(iod.arr_nr_short_read, 0);

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
	D_FREE(sgl.sg_iovs);

	rc = daos_array_close(oh, NULL);
	assert_rc_equal(rc, 0);

	assert_int_equal(nerrors, 0);
	par_barrier(PAR_COMM_WORLD);
} /* End str_mem_str_arr_io */

static void
truncate_array(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	daos_handle_t		oh;
	daos_array_iod_t	iod = {};
	daos_range_t		rg = {};
	d_iov_t			iov = {};
	d_sg_list_t		sgl = {};
	void			*buf;
	daos_array_stbuf_t	stbuf;
	uint64_t		prev;
	int			rc;

	par_barrier(PAR_COMM_WORLD);
	oid = daos_test_oid_gen(arg->coh, OC_SX, typeb, 0, arg->myrank);

	/** create the array */
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 1, 1048576, &oh,
			       NULL);
	assert_rc_equal(rc, 0);

	rc = daos_array_stat(oh, DAOS_TX_NONE, &stbuf, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 0);
	prev = stbuf.st_max_epoch;
	print_message("Size = %zu, EPOCH = %"PRIu64"\n", stbuf.st_size, stbuf.st_max_epoch);

	char *time_str;
	struct timespec ts;

	d_hlc2timespec(stbuf.st_max_epoch, &ts);
	time_str = ctime(&ts.tv_sec);
	print_message("EPOCH time is %s", time_str);

	/* Set array size to be large */
	rc = daos_array_set_size(oh, DAOS_TX_NONE, 1024 * 1024, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_array_stat(oh, DAOS_TX_NONE, &stbuf, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 1024 * 1024);
	assert_true(prev < stbuf.st_max_epoch);
	prev = stbuf.st_max_epoch;
	print_message("Size = %zu, EPOCH = %"PRIu64"\n", stbuf.st_size, stbuf.st_max_epoch);

	/* Set array size to zero */
	rc = daos_array_set_size(oh, DAOS_TX_NONE, 0, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_array_stat(oh, DAOS_TX_NONE, &stbuf, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 0);
	assert_true(prev < stbuf.st_max_epoch);
	prev = stbuf.st_max_epoch;
	print_message("Size = %zu, EPOCH = %"PRIu64"\n", stbuf.st_size, stbuf.st_max_epoch);

	D_ALLOC(buf, 1024);
	assert_non_null(buf);

	iod.arr_nr = 1;
	iod.arr_rgs = &rg;
	rg.rg_len = 6;

	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;
	d_iov_set(&iov, buf, 6);

	/* perform small write */
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	/* check array size */
	rc = daos_array_stat(oh, DAOS_TX_NONE, &stbuf, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 6);
	assert_true(prev < stbuf.st_max_epoch);
	prev = stbuf.st_max_epoch;
	print_message("Size = %zu, EPOCH = %"PRIu64"\n", stbuf.st_size, stbuf.st_max_epoch);

	rc = daos_array_close(oh, NULL);
	assert_rc_equal(rc, 0);

	D_FREE(buf);

	par_barrier(PAR_COMM_WORLD);
} /* End truncate_array */

#define DFS_ITER_NR		128
#define DFS_ITER_DKEY_BUF	(DFS_ITER_NR * sizeof(uint64_t))

static void
ec_array_key_query(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		oid;
	daos_handle_t		oh;
	daos_array_iod_t	iod = {};
	daos_range_t		rg = {};
	d_iov_t			iov = {};
	d_sg_list_t		sgl = {};
	void			*buf;
	daos_array_stbuf_t	stbuf;
	daos_anchor_t           anchor = {0};
	char			*enum_buf;
	daos_key_desc_t		*kds = NULL;
	int			rc;

	par_barrier(PAR_COMM_WORLD);
	if (!test_runable(arg, 6))
		skip();

	oid = daos_test_oid_gen(arg->coh, OC_EC_4P1G1, typeb, 0, arg->myrank);

	/** create the array */
	rc = daos_array_create(arg->coh, oid, DAOS_TX_NONE, 1, 1048576, &oh, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_array_stat(oh, DAOS_TX_NONE, &stbuf, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 0);

	/** write */
	D_ALLOC(buf, 165509);
	assert_non_null(buf);

	iod.arr_nr = 1;
	iod.arr_rgs = &rg;
	rg.rg_idx = 0;
	rg.rg_len = 165509;

	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;
	d_iov_set(&iov, buf, 165509);

	/* perform small write */
	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	/* check array size */
	rc = daos_array_stat(oh, DAOS_TX_NONE, &stbuf, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(stbuf.st_size, 165509);

	rc = daos_array_close(oh, NULL);
	assert_rc_equal(rc, 0);

	D_FREE(buf);

	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	D_ALLOC_ARRAY(enum_buf, DFS_ITER_DKEY_BUF);
	assert_non_null(enum_buf);
	D_ALLOC_ARRAY(kds, DFS_ITER_NR);
	assert_non_null(kds);

	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, DFS_ITER_DKEY_BUF);
	sgl.sg_iovs = &iov;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t	nr = DFS_ITER_NR;
		char		*ptr = &enum_buf[0];
		uint32_t	i;

		rc = daos_obj_list_dkey(oh, DAOS_TX_NONE, &nr, kds, &sgl, &anchor, NULL);
		assert_rc_equal(rc, 0);

		if (nr == 0)
			continue;

		for (i = 0; i < nr; i++) {
			daos_key_t	dkey, akey;
			uint64_t	dkey_val;
			char		akey_val = '0';
			daos_recx_t	recx;

			memcpy(&dkey_val, ptr, kds[i].kd_key_len);
			ptr += kds[i].kd_key_len;
			d_iov_set(&dkey, &dkey_val, sizeof(uint64_t));
			d_iov_set(&akey, &akey_val, 1);
			rc = daos_obj_query_key(oh, DAOS_TX_NONE, DAOS_GET_RECX | DAOS_GET_MAX,
						&dkey, &akey, &recx, NULL);
			assert_rc_equal(rc, 0);
		}
	}
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	D_FREE(enum_buf);
	D_FREE(kds);
	par_barrier(PAR_COMM_WORLD);
} /* End ec_array_key_query */

static const struct CMUnitTest array_api_tests[] = {
	{"Array 0 API: create/open/close (blocking)",
	 simple_array_mgmt, async_disable, NULL},
	{"Array 1 API: small/simple array IO (blocking)",
	 small_io, async_disable, NULL},
	{"Array 2 API: Contiguous memory and array (blocking)",
	 contig_mem_contig_arr_io, async_disable, NULL},
	{"Array 3 API: Contiguous memory and array (non-blocking)",
	 contig_mem_contig_arr_io, async_enable, NULL},
	{"Array 4 API: Contiguous memory Strided array (blocking)",
	 contig_mem_str_arr_io, async_disable, NULL},
	{"Array 5 API: Contiguous memory Strided array (non-blocking)",
	 contig_mem_str_arr_io, async_enable, NULL},
	{"Array 6 API: Strided memory and array (blocking)",
	 str_mem_str_arr_io, async_disable, NULL},
	{"Array 7 API: Strided memory and array (non-blocking)",
	 str_mem_str_arr_io, async_enable, NULL},
	{"Array 8 API: Read from Empty array and records (blocking)",
	 read_empty_records, async_disable, NULL},
	{"Array 9 API: strided_array (blocking)",
	 strided_array, async_disable, NULL},
	{"Array 10 API: write after truncate",
	 truncate_array, async_disable, NULL},
	{"Array 11: EC Array Key Query",
	 ec_array_key_query, async_disable, NULL},
};

static int
daos_array_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			  0, NULL);
}

int
run_daos_array_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(array_api_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests("DAOS_Array_API", array_api_tests, ARRAY_SIZE(array_api_tests),
				sub_tests, sub_tests_size, daos_array_setup, test_teardown);

	par_barrier(PAR_COMM_WORLD);

	return rc;
}
