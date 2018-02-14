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
 * This file is part of daos
 *
 * tests/suite/daos_array.c
 */

#include "daos_test.h"

#define STACK_BUF_LEN	24

static void
byte_array_simple_stack(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	daos_epoch_t	 epoch = time(NULL);
	daos_iov_t	 dkey;
	daos_sg_list_t	 sgl;
	daos_iov_t	 sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	char		 buf_out[STACK_BUF_LEN];
	char		 buf[STACK_BUF_LEN];
	int		 rc;

	dts_buf_render(buf, STACK_BUF_LEN);

	/** open object */
	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	daos_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	daos_iov_set(&sg_iov, buf, sizeof(buf));
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	daos_iov_set(&iod.iod_name, "akey", strlen("akey"));
	daos_csum_set(&iod.iod_kcsum, NULL, 0);
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= 0;
	recx.rx_nr	= sizeof(buf);
	iod.iod_recxs	= &recx;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %d bytes in a single recx\n", STACK_BUF_LEN);
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, 0);

	/** fetch record size & verify */
	print_message("fetching record size\n");
	iod.iod_size	= DAOS_REC_ANY;
	rc = daos_obj_fetch(oh, epoch, &dkey, 1, &iod, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(iod.iod_size, 1);

	/** fetch */
	print_message("reading data back ...\n");
	memset(buf_out, 0, sizeof(buf_out));
	daos_iov_set(&sg_iov, buf_out, sizeof(buf_out));
	rc = daos_obj_fetch(oh, epoch, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);
	/** Verify data consistency */
	print_message("validating data ...\n");
	assert_memory_equal(buf, buf_out, sizeof(buf));

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);
	print_message("all good\n");
}

static void
array_simple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	daos_epoch_t	 epoch = 4;
	daos_iov_t	 dkey;
	daos_sg_list_t	 sgl;
	daos_iov_t	 sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	char		*buf;
	char		*buf_out;
	int		 rc;

	buf = malloc(arg->size * arg->nr);
	assert_non_null(buf);

	dts_buf_render(buf, arg->size * arg->nr);

	/** open object */
	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	daos_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	daos_iov_set(&sg_iov, buf, arg->size * arg->nr);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	daos_iov_set(&iod.iod_name, "akey", strlen("akey"));
	daos_csum_set(&iod.iod_kcsum, NULL, 0);
	iod.iod_nr	= 1;
	iod.iod_size	= arg->size;
	srand(time(NULL) + arg->size);
	recx.rx_idx	= rand();
	recx.rx_nr	= arg->nr;
	iod.iod_recxs	= &recx;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %lu records of %lu bytes each at offset %lu\n",
		      recx.rx_nr, iod.iod_size, recx.rx_idx);
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, 0);

	/** fetch data back */
	print_message("reading data back ...\n");
	buf_out = malloc(arg->size * arg->nr);
	assert_non_null(buf_out);
	memset(buf_out, 0, arg->size * arg->nr);
	daos_iov_set(&sg_iov, buf_out, arg->size * arg->nr);
	iod.iod_size	= DAOS_REC_ANY;
	rc = daos_obj_fetch(oh, epoch, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);
	/** verify record size */
	print_message("validating record size ...\n");
	assert_int_equal(iod.iod_size, arg->size);
	/** Verify data consistency */
	print_message("validating data ...\n");
	assert_memory_equal(buf, buf_out, arg->size * arg->nr);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);

	free(buf_out);
	free(buf);
	print_message("all good\n");
}

#define NUM_RECORDS 24

static void
array_partial(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	daos_epoch_t	 epoch = 4;
	daos_iov_t	 dkey;
	daos_sg_list_t	 sgl;
	daos_iov_t	 sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	daos_recx_t	 recxs[4];
	char		*buf;
	char		*buf_out;
	int		 rc, i;

	arg->size = 4;

	buf = malloc(arg->size * NUM_RECORDS);
	assert_non_null(buf);

	dts_buf_render(buf, arg->size * NUM_RECORDS);

	/** open object */
	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	daos_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	daos_iov_set(&sg_iov, buf, arg->size * NUM_RECORDS);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	daos_iov_set(&iod.iod_name, "akey", strlen("akey"));
	daos_csum_set(&iod.iod_kcsum, NULL, 0);
	iod.iod_nr	= 1;
	iod.iod_size	= arg->size;
	recx.rx_idx	= 0;
	recx.rx_nr	= NUM_RECORDS;
	iod.iod_recxs	= &recx;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %lu records of %lu bytes each at offset %lu\n",
		      recx.rx_nr, iod.iod_size, recx.rx_idx);
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, 0);

	/** fetch 1/2 of the records back */
	print_message("reading 1/2 of the records back ...\n");
	buf_out = malloc(arg->size * NUM_RECORDS/2);
	assert_non_null(buf_out);
	memset(buf_out, 0, arg->size * NUM_RECORDS/2);
	daos_iov_set(&sg_iov, buf_out, arg->size * NUM_RECORDS/2);
	iod.iod_size	= arg->size;
	iod.iod_nr	= 4;
	for (i = 0; i < 4; i++) {
		recxs[i].rx_idx	= i*6;
		recxs[i].rx_nr	= 3;
	}

	iod.iod_recxs = recxs;
	rc = daos_obj_fetch(oh, epoch, &dkey, 1, &iod, &sgl, NULL, NULL);
	print_message("fetch returns %d\n", rc);
	assert_int_equal(rc, 0);
	/** verify record size */
	print_message("validating record size ...\n");
	assert_int_equal(iod.iod_size, arg->size);
	/** Verify data consistency */
	print_message("validating data ...\n");

	for (i = 0; i < 4; i++) {
		char *tmp1 = buf + i * 6 * arg->size;
		char *tmp2 = buf_out + i * 3 * arg->size;

		assert_memory_equal(tmp1, tmp2, arg->size * 3);
	}

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);

	free(buf_out);
	free(buf);
	print_message("all good\n");
}

static int
set_size_uint8(void **state)
{
	test_arg_t	*arg = *state;

	arg->size = sizeof(uint8_t);
	arg->nr   = 131071;

	return 0;
}

static int
set_size_uint16(void **state)
{
	test_arg_t	*arg = *state;

	arg->size = sizeof(uint16_t);
	arg->nr   = 1 << 9;

	return 0;
}

static int
set_size_uint32(void **state)
{
	test_arg_t	*arg = *state;

	arg->size = sizeof(uint32_t);
	arg->nr   = 1 << 8;

	return 0;
}

static int
set_size_uint64(void **state)
{
	test_arg_t	*arg = *state;

	arg->size = sizeof(uint64_t);
	arg->nr   = 1 << 7;

	return 0;
}

static int
set_size_131071(void **state)
{
	test_arg_t	*arg = *state;

	arg->size = 131071;
	arg->nr   = 1 << 3;

	return 0;
}

static int
set_size_1mb(void **state)
{
	test_arg_t	*arg = *state;

	arg->size = 1 << 20;
	arg->nr   = 10;

	return 0;
}

static void
replicator(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	daos_epoch_t	 epoch = time(NULL);
	daos_iov_t	 dkey;
	daos_sg_list_t	 sgl;
	daos_iov_t	 sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	char		 buf_out[4608];
	char		 buf[192];
	int		 rc;

	dts_buf_render(buf, 192);

	/** open object */
	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	daos_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	daos_iov_set(&sg_iov, buf, sizeof(buf));
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	daos_iov_set(&iod.iod_name, "akey", strlen("akey"));
	daos_csum_set(&iod.iod_kcsum, NULL, 0);
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= 27136;
	recx.rx_nr	= sizeof(buf);
	iod.iod_recxs	= &recx;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %d bytes in a single recx\n", 192);
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, 0);

	recx.rx_idx     = 30208;
	iod.iod_recxs	= &recx;
	print_message("writing %d bytes in a single recx\n", 192);
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, 0);

	recx.rx_idx     = 28672;
	iod.iod_recxs	= &recx;
	print_message("writing %d bytes in a single recx\n", 192);
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, 0);

	/** fetch */
	print_message("reading data back ...\n");
	memset(buf_out, 0, sizeof(buf_out));
	daos_iov_set(&sg_iov, buf_out, sizeof(buf_out));
	recx.rx_idx     = 27136;
	recx.rx_nr      = sizeof(buf_out);
	iod.iod_recxs	= &recx;
	rc = daos_obj_fetch(oh, epoch, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);
	print_message("all good\n");
}

static const struct CMUnitTest array_tests[] = {
	{ "ARRAY1: byte array with buffer on stack",
	  byte_array_simple_stack, NULL, test_case_teardown},
	{ "ARRAY2: array of uint8_t",
	  array_simple, set_size_uint8, test_case_teardown},
	{ "ARRAY3: array of uint16_t",
	  array_simple, set_size_uint16, test_case_teardown},
	{ "ARRAY4: array of uint32_t",
	  array_simple, set_size_uint32, test_case_teardown},
	{ "ARRAY5: array of uint64_t",
	  array_simple, set_size_uint64, test_case_teardown},
	{ "ARRAY6: array of 131071-byte records",
	  array_simple, set_size_131071, test_case_teardown},
	{ "ARRAY7: array of 1MB records",
	  array_simple, set_size_1mb, test_case_teardown},
	{ "ARRAY8: partial I/O on array",
	  array_partial, NULL, test_case_teardown},
	{ "ARRAY9: segfault replicator",
	  replicator, NULL, test_case_teardown},
};

int
array_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, false);
}

int
run_daos_array_test(int rank, int size)
{
	int rc = 0;

	if (rank == 0)
		rc = cmocka_run_group_tests_name("DAOS Array tests",
						 array_tests, array_setup,
						 test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
