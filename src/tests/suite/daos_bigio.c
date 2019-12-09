/**
 * (C) Copyright 2019 Intel Corporation.
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
 * tests/suite/daos_bigio.c
 */

#include <daos/checksum.h>
#include "daos_test.h"

#define	POOL_SIZE_50G	((1024 * 1024 * 1024) * 50)

static void
bigio_contig(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t	 dkey;
	d_sg_list_t	 sgl;
	d_iov_t	 sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	int		*buf;
	int		*buf_out;
	daos_size_t	 i;
	uuid_t		 co_uuid;
	daos_cont_info_t co_info;
	daos_handle_t	 coh;
	int		 rc;

	if (arg->myrank == 0) {
		uuid_generate(co_uuid);
		print_message("setup: creating container "DF_UUIDF"\n",
			      DP_UUID(co_uuid));
		rc = daos_cont_create(arg->pool.poh, co_uuid, NULL, NULL);
		if (rc) {
			print_message("daos_cont_create failed, rc: %d\n", rc);
			goto bcast;
		}

		rc = daos_cont_open(arg->pool.poh, co_uuid, DAOS_COO_RW,
				    &coh, &co_info, NULL);
		if (rc)
			print_message("daos_cont_open failed, rc: %d\n", rc);
	}

bcast:
	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		assert_int_equal(rc, 0);

	if (arg->multi_rank)
		handle_share(&coh, HANDLE_CO, arg->myrank, arg->pool.poh, 0);

	/*
	 * Segfault in pmdk with 20GB I/O - 30 GB pool size.
	 * This should be resolved though with breaking IOD at server side.
	 */
	arg->size = 4;
	arg->nr = (daos_size_t)(1024*1024*1024)*20 / arg->size;

	buf = malloc(arg->size * arg->nr);
	assert_non_null(buf);

	for (i = 0; i < arg->nr; i++)
		buf[i] = i+1;

	/** open object */
	oid = dts_oid_gen(OC_S1, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov, buf, arg->size * arg->nr);
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	dcb_set_null(&iod.iod_kcsum);
	iod.iod_nr	= 1;
	iod.iod_size	= arg->size;
	recx.rx_idx	= 0;
	recx.rx_nr	= arg->nr;
	iod.iod_recxs	= &recx;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %lu records of %lu bytes each at offset %lu\n",
		      recx.rx_nr, iod.iod_size, recx.rx_idx);
	rc = daos_obj_update(oh, DAOS_TX_NONE, &dkey, 1, &iod, &sgl, NULL);
	print_message("daos_obj_update() returns %d\n", rc);
	assert_int_equal(rc, 0);

	/** fetch the records back */
	buf_out = calloc(arg->size, arg->nr);
	assert_non_null(buf_out);

	d_iov_set(&sg_iov, buf_out, arg->size * arg->nr);
	sgl.sg_iovs = &sg_iov;

	iod.iod_size = DAOS_REC_ANY;

	print_message("reading data back\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, &dkey, 1, &iod, &sgl, NULL, NULL);
	print_message("daos_obj_fetch() returns %d\n", rc);
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

	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);

	while (arg->myrank == 0) {
		rc = daos_cont_destroy(arg->pool.poh, co_uuid, 1, NULL);
		assert_int_equal(rc, 0);
	}

	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc) {
		print_message("failed to destroy container "DF_UUIDF
			      ": %d\n", DP_UUID(co_uuid), rc);
		assert_int_equal(rc, 0);
	}

	free(buf_out);
	free(buf);
	print_message("all good\n");
}

static void
bigio_noncontig(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t	 dkey;
	d_sg_list_t	 sgl;
	d_iov_t	 sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	*recxs;
	int		*buf;
	int		*buf_out;
	daos_size_t	 i;
	uuid_t		 co_uuid;
	daos_cont_info_t co_info;
	daos_handle_t	 coh;
	int		 rc;

	if (arg->myrank == 0) {
		uuid_generate(co_uuid);
		print_message("setup: creating container "DF_UUIDF"\n",
			      DP_UUID(co_uuid));
		rc = daos_cont_create(arg->pool.poh, co_uuid, NULL, NULL);
		if (rc) {
			print_message("daos_cont_create failed, rc: %d\n", rc);
			goto bcast;
		}

		rc = daos_cont_open(arg->pool.poh, co_uuid, DAOS_COO_RW,
				    &coh, &co_info, NULL);
		if (rc)
			print_message("daos_cont_open failed, rc: %d\n", rc);
	}

bcast:
	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, 0);

	if (arg->multi_rank)
		handle_share(&coh, HANDLE_CO, arg->myrank, arg->pool.poh, 0);

	arg->size = 4;
	/*
	 * 50 GB P0ol size
	 *
	 * at 30MB:
	 * CRIT src/vos/vos_tree.c:282 kb_rec_alloc() assertion failure
	 * kb_rec_alloc: Assertion `ta != ((void *)0)' failed.
	 *
	 * > 30MB:
	 * -1007 - NO SPACE
	 */
	arg->nr = (daos_size_t)(1024*1024) * 50 / arg->size;

	buf = malloc(arg->size * arg->nr);
	assert_non_null(buf);

	recxs = malloc(sizeof(daos_recx_t) * arg->nr);
	for (i = 0; i < arg->nr; i++) {
		buf[i] = i+1;
		recxs[i].rx_nr = 1;
		recxs[i].rx_idx = i*2;
	}

	/** open object */
	oid = dts_oid_gen(OC_S1, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov, buf, arg->size * arg->nr);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	dcb_set_null(&iod.iod_kcsum);
	iod.iod_nr	= arg->nr;
	iod.iod_size	= arg->size;
	iod.iod_recxs	= recxs;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %d records %lu bytes each non contig offsets\n",
		arg->nr, iod.iod_size);
	rc = daos_obj_update(oh, DAOS_TX_NONE, &dkey, 1, &iod, &sgl, NULL);
	print_message("daos_obj_update() returns %d\n", rc);
	assert_int_equal(rc, 0);

	/** fetch the records back */
	buf_out = calloc(arg->size, arg->nr);
	assert_non_null(buf_out);

	d_iov_set(&sg_iov, buf_out, arg->size * arg->nr);
	sgl.sg_iovs = &sg_iov;

	iod.iod_size = DAOS_REC_ANY;

	print_message("reading data back\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, &dkey, 1, &iod, &sgl, NULL, NULL);
	print_message("daos_obj_fetch() returns %d\n", rc);
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

	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);

	while (arg->myrank == 0) {
		rc = daos_cont_destroy(arg->pool.poh, co_uuid, 1, NULL);
		if (rc == -DER_BUSY) {
			print_message("Container is busy, wait\n");
			sleep(1);
			continue;
		}
		break;
	}

	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc) {
		print_message("failed to destroy container "DF_UUIDF
			      ": %d\n", DP_UUID(co_uuid), rc);
		assert_int_equal(rc, 0);
	}

	free(buf_out);
	free(buf);
	free(recxs);

	print_message("all good\n");
}

static void
bigio_noncontig_mem(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t	 dkey;
	d_sg_list_t	 sgl;
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	char		*buf;
	char		*buf_out;
	daos_size_t	 i;
	uuid_t		 co_uuid;
	daos_cont_info_t co_info;
	daos_handle_t	 coh;
	int		 rc;

	if (arg->myrank == 0) {
		uuid_generate(co_uuid);
		print_message("setup: creating container "DF_UUIDF"\n",
			      DP_UUID(co_uuid));
		rc = daos_cont_create(arg->pool.poh, co_uuid, NULL, NULL);
		if (rc) {
			print_message("daos_cont_create failed, rc: %d\n", rc);
			goto bcast;
		}

		rc = daos_cont_open(arg->pool.poh, co_uuid, DAOS_COO_RW,
				    &coh, &co_info, NULL);
		if (rc)
			print_message("daos_cont_open failed, rc: %d\n", rc);
	}

bcast:
	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		assert_int_equal(rc, 0);

	if (arg->multi_rank)
		handle_share(&coh, HANDLE_CO, arg->myrank, arg->pool.poh, 0);

	/** hang or just takes forever. needs more investigation */
	arg->size = 4;
	arg->nr = (daos_size_t)(1024*1024) * 1000 / arg->size;
	buf = malloc(arg->nr * 2 * arg->size);
	assert_non_null(buf);

	dts_buf_render(buf, arg->nr * 2 * arg->size);

	/** open object */
	oid = dts_oid_gen(OC_S1, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** set memory location */
	sgl.sg_nr = arg->nr;
	sgl.sg_iovs = (d_iov_t *)malloc(sizeof(d_iov_t) * arg->nr);
	assert_non_null(sgl.sg_iovs);

	char *p = buf;

	for (i = 0; i < arg->nr; i++) {
		d_iov_set(&sgl.sg_iovs[i], p, arg->size);
		p += arg->size * 2;
	}

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	dcb_set_null(&iod.iod_kcsum);
	iod.iod_nr	= 1;
	iod.iod_size	= arg->size;
	recx.rx_idx	= 0;
	recx.rx_nr	= arg->nr;
	iod.iod_recxs	= &recx;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %lu records of %lu bytes each at offset %lu\n",
		      recx.rx_nr, iod.iod_size, recx.rx_idx);
	rc = daos_obj_update(oh, DAOS_TX_NONE, &dkey, 1, &iod, &sgl, NULL);
	print_message("daos_obj_update() returns %d\n", rc);
	assert_int_equal(rc, 0);

	/** fetch the records back */
	buf_out = calloc(arg->nr * 2, arg->size);
	assert_non_null(buf_out);

	p = buf_out;
	for (i = 0; i < arg->nr; i++) {
		d_iov_set(&sgl.sg_iovs[i], p, arg->size);
		p += arg->size * 2;
	}

	iod.iod_size = DAOS_REC_ANY;

	print_message("reading data back\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, &dkey, 1, &iod, &sgl, NULL, NULL);
	print_message("daos_obj_fetch() returns %d\n", rc);
	assert_int_equal(rc, 0);

	/** verify record size */
	print_message("validating record size ...\n");
	assert_int_equal(iod.iod_size, arg->size);

	/** Verify data consistency */
	print_message("validating data ...\n");

	char *tmp1 = buf;
	char *tmp2 = buf_out;

	for (i = 0; i < arg->nr; i++) {
		assert_memory_equal(tmp1, tmp2, arg->size);
		tmp1 += arg->size * 2;
		tmp2 += arg->size * 2;
	}

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);

	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);

	while (arg->myrank == 0) {
		rc = daos_cont_destroy(arg->pool.poh, co_uuid, 1, NULL);
		if (rc == -DER_BUSY) {
			print_message("Container is busy, wait\n");
			sleep(1);
			continue;
		}
		break;
	}

	if (arg->multi_rank)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc) {
		print_message("failed to destroy container "DF_UUIDF
			      ": %d\n", DP_UUID(co_uuid), rc);
		assert_int_equal(rc, 0);
	}

	free(buf_out);
	free(buf);
	print_message("all good\n");
}

static const struct CMUnitTest bigio_tests[] = {
	{ "BIG1: big array of contig records",
	  bigio_contig, NULL, NULL},
	{ "BIG2: big array of non-contig records",
	  bigio_noncontig, NULL, NULL},
	{ "BIG3: big array of non-contig records in memory",
	  bigio_noncontig_mem, NULL, NULL},
};

int
bigio_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, false,
		POOL_SIZE_50G, NULL);
}

int
run_daos_bigio_test(int rank, int size)
{
	int rc = 0;

	if (rank == 0)
		rc = cmocka_run_group_tests_name("DAOS Big IO tests",
						 bigio_tests, bigio_setup,
						 test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
