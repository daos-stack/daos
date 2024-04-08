/**
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_obj_flat.c
 */
#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"

#include "daos_iotest.h"
#include <daos_types.h>
#include <daos/checksum.h>
#include <daos/placement.h>

unsigned int flat_ec_obj_class = OC_EC_4P2G1;
unsigned int flat_ec_cell_size = 32768;

static int
flat_setup(void  **state)
{
	int		rc;
	unsigned int	orig_dt_cell_size;
	int		num_ranks = 6;

	orig_dt_cell_size = dt_cell_size;
	dt_cell_size = flat_ec_cell_size;
	rc = test_setup(state, SETUP_CONT_CONNECT, true,
			DEFAULT_POOL_SIZE, num_ranks, NULL);
	dt_cell_size = orig_dt_cell_size;
	if (rc) {
		/* Let's skip for this case, since it is possible there
		 * is not enough ranks here.
		 */
		print_message("Failed to create pool with %d ranks: "DF_RC"\n",
			      num_ranks, DP_RC(rc));
		return 0;
	}

	return 0;
}

static void
wait_cont_flat()
{
	print_message("sleep 30 S ...\n");
	sleep(30);
	print_message("sleep 30 S done\n");
}

static void
update_after_flat(void **state)
{
#define STACK_BUF_LEN	(128)
	test_arg_t		*arg0 = *state;
	test_arg_t		*arg = NULL;
	daos_obj_id_t		 oid;
	daos_handle_t		 oh;
	d_iov_t			 dkey;
	d_sg_list_t		 sgl;
	d_iov_t			 sg_iov;
	daos_iod_t		 iod;
	daos_recx_t		 recx;
	char			 stack_buf[STACK_BUF_LEN];
	int			 rc;

	rc = test_setup((void **)&arg, SETUP_CONT_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, &arg0->pool);
	assert_success(rc);

	dts_buf_render(stack_buf, STACK_BUF_LEN);
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov, stack_buf, STACK_BUF_LEN);
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 1;
	sgl.sg_iovs	= &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	recx.rx_idx = 0;
	recx.rx_nr  = STACK_BUF_LEN;
	iod.iod_size	= 1;
	iod.iod_nr	= 1;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing before flatten ...\n");
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	print_message("flatten the container should success\n");
	rc = daos_cont_set_ro(arg->coh, NULL);
	assert_rc_equal(rc, 0);

	print_message("writing after flatten should fail\n");
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, -DER_NO_PERM);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	test_teardown((void **)&arg);
}

static void
basic_cont_flatten(void **state)
{
	test_arg_t		*arg0 = *state;
	test_arg_t		*arg = NULL;
	daos_obj_id_t		 oid;
	daos_handle_t		 oh;
	d_iov_t			 dkey;
	d_sg_list_t		 sgl;
	d_iov_t			 sg_iov;
	daos_iod_t		 iod;
	daos_recx_t		 recx;
	void			*buf = NULL;
	int			 buf_len;
	int			 dkey_nr = 3;
	int			 array_per_dkey = 7;
	int			 singv_per_dkey = 3;
	daos_epoch_t		 snap_epoch;
	char			 dkey_str[32];
	char			 akey_str[32];
	void			*buf_singv;
	void			*buf_array[4];
	daos_iod_t		 iod_array[2];
	daos_recx_t		 recx_array[4];
	d_sg_list_t		 sgl_array[2];
	d_iov_t			 sg_iov_array[2];
	int			 i, j, rc;

	rc = test_setup((void **)&arg, SETUP_CONT_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, &arg0->pool);
	assert_success(rc);

	/* test 1: write an large object that cannot be flattened */
	buf_len = 4 << 20;
	D_ALLOC(buf, buf_len);
	assert_true(buf != NULL);
	dts_buf_render(buf, buf_len);
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	print_message("write large object "DF_OID"\n", DP_OID(oid));
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);
	d_iov_set(&dkey, "dkey_large", strlen("dkey_large"));
	d_iov_set(&sg_iov, buf, buf_len);
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 1;
	sgl.sg_iovs	= &sg_iov;
	d_iov_set(&iod.iod_name, "akey_large", strlen("akey_large"));
	recx.rx_idx = 0;
	recx.rx_nr  = buf_len;
	iod.iod_size	= 1;
	iod.iod_nr	= 1;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	d_iov_set(&iod.iod_name, "akey_short", strlen("akey_short"));
	recx.rx_idx = 0;
	recx.rx_nr  = 333;
	iod.iod_size	= 1;
	iod.iod_nr	= 1;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	D_FREE(buf);

	/* test 2: write an small obj cross snapshot */
	buf_len = 512;
	D_ALLOC(buf, buf_len);
	assert_true(buf != NULL);
	dts_buf_render(buf, buf_len);
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	print_message("write a small object "DF_OID" cross snapshot\n", DP_OID(oid));
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);
	d_iov_set(&dkey, "dkey_cross", strlen("dkey_cross"));
	d_iov_set(&sg_iov, buf, buf_len);
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 1;
	sgl.sg_iovs	= &sg_iov;
	d_iov_set(&iod.iod_name, "akey_1", strlen("akey_1"));
	recx.rx_idx = 0;
	recx.rx_nr  = buf_len;
	iod.iod_size	= 1;
	iod.iod_nr	= 1;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_create_snap(arg->coh, &snap_epoch, NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("created snapshot "DF_X64"\n", snap_epoch);

	d_iov_set(&iod.iod_name, "akey_2", strlen("akey_3"));
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	d_iov_set(&iod.iod_name, "akey_3", strlen("akey_3"));
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	D_FREE(buf);

	/* test 3: write an small object that can be flattened */
	buf_len = 128;
	D_ALLOC(buf, buf_len * 512);
	assert_true(buf != NULL);
	D_ALLOC(buf_singv, buf_len);
	assert_true(buf_singv != NULL);
	for (i = 0; i < 4; i++) {
		D_ALLOC(buf_array[i], buf_len * 512);
		assert_true(buf_array[i] != NULL);
	}
	dts_buf_render(buf, buf_len);
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);
	print_message("write small object, "DF_OID", dkey_nr %d, array_per_dkey %d, "
		      "singv_per_dkey %d\n", DP_OID(oid), dkey_nr, array_per_dkey, singv_per_dkey);

	d_iov_set(&sg_iov, buf, buf_len * 512);
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 1;
	sgl.sg_iovs	= &sg_iov;

	for (i = 0; i < dkey_nr; i++) {
		memset(dkey_str, 0, 32);
		sprintf(dkey_str, "dkey_small_%d", i);
		d_iov_set(&dkey, dkey_str, strlen(dkey_str));

		iod.iod_size	= buf_len;
		iod.iod_nr	= 1;
		iod.iod_recxs	= NULL;
		iod.iod_type	= DAOS_IOD_SINGLE;
		for (j = 0; j < singv_per_dkey; j++) {
			memset(akey_str, 0, 32);
			sprintf(akey_str, "akey_singv_%d", j);
			d_iov_set(&iod.iod_name, akey_str, strlen(akey_str));
			dts_buf_render(buf, buf_len);
			if (i == dkey_nr - 1 && j == 0)
				memcpy(buf_singv, buf, buf_len);
			rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
			assert_rc_equal(rc, 0);
		}

		iod.iod_size	= 1;
		iod.iod_nr	= 1;
		iod.iod_recxs	= &recx;
		iod.iod_type	= DAOS_IOD_ARRAY;
		for (j = 0; j < array_per_dkey; j++) {
			if (j == array_per_dkey - 1) {
				recx.rx_idx = 4 << 20;
				recx.rx_nr = 32 * 1024;
			} else {
				recx.rx_idx = 2 * buf_len + rand() % 1024;
				recx.rx_nr  = buf_len + rand() % buf_len;
			}
			memset(akey_str, 0, 32);
			if (j < 3)
				sprintf(akey_str, "a_%d", j);
			else
				sprintf(akey_str, "akey_array_%d", j);
			d_iov_set(&iod.iod_name, akey_str, strlen(akey_str));
			dts_buf_render(buf, buf_len);

			if (i == dkey_nr - 1 && j == 2) {
				d_iov_set(&iod_array[0].iod_name, "a_2", 3);
				recx_array[0] = recx;
				memcpy(buf_array[0], buf, recx.rx_nr);
			}
			if (i == dkey_nr - 1 && j == 3) {
				d_iov_set(&iod_array[1].iod_name, "akey_array_3", 12);
				recx_array[2] = recx;
				memcpy(buf_array[1], buf, recx.rx_nr);
			}

			rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
			assert_rc_equal(rc, 0);
		}

		for (j = 0; j < array_per_dkey; j++) {
			recx.rx_idx = 0;
			if (j == array_per_dkey - 1)
				recx.rx_nr = 32 * 1024;
			else
				recx.rx_nr  = buf_len + rand() % buf_len;
			memset(akey_str, 0, 32);
			if (j < 3)
				sprintf(akey_str, "a_%d", j);
			else
				sprintf(akey_str, "akey_array_%d", j);
			d_iov_set(&iod.iod_name, akey_str, strlen(akey_str));
			dts_buf_render(buf, buf_len);

			if (j == 0) {
				rc = daos_obj_punch_akeys(oh, DAOS_TX_NONE, 0, &dkey, 1,
							  &iod.iod_name, NULL);
				assert_rc_equal(rc, 0);
			}
			rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
			assert_rc_equal(rc, 0);

			if (j == 1) {
				rc = daos_obj_punch_akeys(oh, DAOS_TX_NONE, 0, &dkey, 1,
							  &iod.iod_name, NULL);
				assert_rc_equal(rc, 0);
			}

			if (i == dkey_nr - 1 && j == 2) {
				recx_array[1] = recx;
				memcpy(buf_array[0] + recx_array[0].rx_nr, buf, recx.rx_nr);
				/* a few more bytes to test read hole */
				recx_array[1].rx_nr += 9;
				memset(buf_array[0] + recx_array[0].rx_nr + recx.rx_nr, 0, 9);
			}
			if (i == dkey_nr - 1 && j == 3) {
				recx_array[3] = recx;
				memcpy(buf_array[1] + recx_array[2].rx_nr, buf, recx.rx_nr);
				/* a few more bytes to test read hole */
				recx_array[3].rx_nr += 17;
				memset(buf_array[1] + recx_array[2].rx_nr + recx.rx_nr, 0, 17);
			}
		}
	}

	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	print_message("flatten the container\n");
	rc = daos_cont_set_ro(arg->coh, NULL);
	assert_rc_equal(rc, 0);

	wait_cont_flat();

	/* read from flattened object */
	print_message("read from flattened object "DF_OID" ...\n", DP_OID(oid));
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);
	memset(buf, 0, buf_len * 512);
	iod.iod_recxs	= NULL;
	iod.iod_type = DAOS_IOD_SINGLE;
	iod.iod_size = 0;
	memset(akey_str, 0, 32);
	sprintf(akey_str, "akey_singv_%d", 0);
	d_iov_set(&iod.iod_name, akey_str, strlen(akey_str));
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
	print_message("read singv from flattened object "DF_OID", iod_size %d, rc %d\n",
		      DP_OID(oid), (int)iod.iod_size, rc);
	assert_rc_equal(rc, 0);
	assert_memory_equal(buf, buf_singv, iod.iod_size);
	print_message("conditional fetch non-existed singv...\n");
	sprintf(akey_str, "non_singv_%d", 9);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_AKEY_FETCH, &dkey, 1, &iod, &sgl, NULL,
			    NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	d_iov_set(&sg_iov_array[0], buf_array[2], buf_len * 512);
	sgl_array[0].sg_nr	= 1;
	sgl_array[0].sg_nr_out	= 1;
	sgl_array[0].sg_iovs	= &sg_iov_array[0];
	d_iov_set(&sg_iov_array[1], buf_array[3], buf_len * 512);
	sgl_array[1].sg_nr	= 1;
	sgl_array[1].sg_nr_out	= 1;
	sgl_array[1].sg_iovs	= &sg_iov_array[1];

	iod_array[0].iod_size	= 1;
	iod_array[0].iod_nr	= 2;
	iod_array[0].iod_recxs	= &recx_array[0];
	iod_array[0].iod_type	= DAOS_IOD_ARRAY;
	iod_array[1].iod_size	= 1;
	iod_array[1].iod_nr	= 2;
	iod_array[1].iod_recxs	= &recx_array[2];
	iod_array[1].iod_type	= DAOS_IOD_ARRAY;

	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 2, iod_array, sgl_array, NULL, NULL);
	print_message("read array from flattened object "DF_OID", iod_size %d, rc %d\n",
		      DP_OID(oid), (int)iod.iod_size, rc);
	assert_memory_equal(buf_array[0], buf_array[2], recx_array[0].rx_nr + recx_array[1].rx_nr);
	assert_memory_equal(buf_array[1], buf_array[3], recx_array[2].rx_nr + recx_array[3].rx_nr);

	print_message("conditional fetch non-existed array ...\n");
	d_iov_set(&iod_array[0].iod_name, "a_9", 3);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 2, iod_array, sgl_array, NULL, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_AKEY_FETCH, &dkey, 2, iod_array, sgl_array,
			    NULL, NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	D_FREE(buf);
	D_FREE(buf_singv);
	D_FREE(buf_array[0]);
	D_FREE(buf_array[1]);
	D_FREE(buf_array[2]);
	D_FREE(buf_array[3]);

	test_teardown((void **)&arg);
}

static void
ec_cond_fetch(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_iov_t		 non_exist_dkey;
	d_sg_list_t	 sgl[2];
	d_iov_t		 sg_iov[2];
	daos_iod_t	 iod[2];
	daos_recx_t	 recx[2];
	char		*buf[2];
	char		*akey[2];
	const char	*akey_fmt = "akey%d";
	int		 i, rc;
	daos_size_t	 size = 8192;

	if (!test_runable(arg0, 6))
		return;

	rc = test_setup((void **)&arg, SETUP_CONT_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, &arg0->pool);
	assert_success(rc);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, flat_ec_obj_class, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));
	d_iov_set(&non_exist_dkey, "non_dkey", strlen("non_dkey"));

	for (i = 0; i < 2; i++) {
		D_ALLOC(akey[i], strlen(akey_fmt) + 1);
		sprintf(akey[i], akey_fmt, i);

		D_ALLOC(buf[i], size);
		assert_non_null(buf[i]);

		dts_buf_render(buf[i], size);

		/** init scatter/gather */
		d_iov_set(&sg_iov[i], buf[i], size);
		sgl[i].sg_nr		= 1;
		sgl[i].sg_nr_out	= 0;
		sgl[i].sg_iovs		= &sg_iov[i];

		/** init I/O descriptor */
		d_iov_set(&iod[i].iod_name, akey[i], strlen(akey[i]));
		iod[i].iod_nr		= 1;
		iod[i].iod_size		= 1;
		iod[i].iod_recxs	= &recx[i];
		iod[i].iod_type		= DAOS_IOD_ARRAY;
		if (i == 0) {
			recx[i].rx_idx		= 0;
			recx[i].rx_nr		= size;
		} else {
			recx[i].rx_idx		= flat_ec_cell_size;
			recx[i].rx_nr		= size;
		}
	}

	/** update record */
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 2, iod, sgl,
			     NULL);
	assert_rc_equal(rc, 0);

	print_message("flatten the container\n");
	rc = daos_cont_set_ro(arg->coh, NULL);
	assert_rc_equal(rc, 0);

	wait_cont_flat();

	/** fetch with NULL sgl but iod_size is non-zero */
	print_message("negative test - fetch with non-zero iod_size and NULL sgl\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, iod, NULL,
			    NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	/** normal fetch */
	for (i = 0; i < 2; i++)
		iod[i].iod_size	= DAOS_REC_ANY;

	print_message("normal fetch\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 2, iod, NULL,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	for (i = 0; i < 2; i++)
		assert_int_equal(iod[i].iod_size, 1);

	for (i = 0; i < 2; i++)
		d_iov_set(&sg_iov[i], buf[i], size);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 2, iod, sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);

	print_message("cond_deky, fetch non-exist dkey\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_DKEY_FETCH, &non_exist_dkey, 2, iod, sgl,
			    NULL, NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	print_message("cond_dkey, dkey exist, akey non-exist...\n");
	recx[0].rx_idx	= flat_ec_cell_size;
	recx[0].rx_nr	= size;
	d_iov_set(&iod[0].iod_name, "non-akey", strlen("non-akey"));
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_DKEY_FETCH, &dkey, 1, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, 0);

	print_message("cond_akey fetch, akey exist on another data shard...\n");
	d_iov_set(&iod[0].iod_name, akey[0], strlen(akey[0]));
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_AKEY_FETCH, &dkey, 1, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, 0);

	recx[1].rx_idx	= 0;
	recx[1].rx_nr	= size;
	print_message("cond_akey fetch, check exist from parity shard\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_AKEY_FETCH, &dkey, 1, &iod[1], sgl, NULL,
			    NULL);
	assert_rc_equal(rc, 0);

	print_message("cond_akey fetch, check exist from all data shards\n");
	daos_fail_loc_set(DAOS_OBJ_SKIP_PARITY | DAOS_FAIL_ALWAYS);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_AKEY_FETCH, &dkey, 1, &iod[1], sgl, NULL,
			    NULL);
	assert_rc_equal(rc, 0);
	daos_fail_loc_set(0);

	print_message("cond_akey fetch, one akey exist and another akey non-exist\n");
	d_iov_set(&iod[1].iod_name, "non-akey", strlen("non-akey"));
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_AKEY_FETCH, &dkey, 2, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	print_message("cond fetch per akey, one akey exist and another akey non-exist\n");
	iod[0].iod_flags = DAOS_COND_AKEY_FETCH;
	iod[1].iod_flags = DAOS_COND_AKEY_FETCH;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_PER_AKEY, &dkey, 2, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	print_message("cond fetch per akey, two akeys both exist\n");
	recx[0].rx_idx	= 0;
	recx[0].rx_nr	= size;
	recx[1].rx_idx	= flat_ec_cell_size;
	recx[1].rx_nr	= size;
	d_iov_set(&iod[0].iod_name, akey[0], strlen(akey[0]));
	d_iov_set(&iod[1].iod_name, akey[1], strlen(akey[1]));
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_PER_AKEY, &dkey, 2, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, 0);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		D_FREE(akey[i]);
		D_FREE(buf[i]);
	}

	test_teardown((void **)&arg);
}

static void
ec_data_recov(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_iov_t		 akey;
	d_sg_list_t	 sgl[2];
	d_iov_t		 sg_iov[2];
	char		*buf[2];
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	int		 i, rc;
	daos_size_t	 size = flat_ec_cell_size * 4;
	uint16_t	 shard[2];
	uint64_t	 fail_val;

	if (!test_runable(arg0, 6))
		return;

	rc = test_setup((void **)&arg, SETUP_CONT_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, &arg0->pool);
	assert_success(rc);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, flat_ec_obj_class, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey_recov", strlen("dkey_recov"));
	d_iov_set(&akey, "akey_recov", strlen("akey_recov"));

	for (i = 0; i < 2; i++) {
		D_ALLOC(buf[i], size);
		assert_non_null(buf[i]);

		dts_buf_render(buf[i], size);

		/** init scatter/gather */
		d_iov_set(&sg_iov[i], buf[i], size);
		sgl[i].sg_nr		= 1;
		sgl[i].sg_nr_out	= 0;
		sgl[i].sg_iovs		= &sg_iov[i];
	}

	/** init I/O descriptor */
	iod.iod_name		= akey;
	iod.iod_nr		= 1;
	iod.iod_size		= 1;
	iod.iod_recxs		= &recx;
	iod.iod_type		= DAOS_IOD_ARRAY;
	recx.rx_idx		= 0;
	recx.rx_nr		= size;

	/** update record */
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl[0], NULL);
	assert_rc_equal(rc, 0);

	print_message("flatten the container\n");
	rc = daos_cont_set_ro(arg->coh, NULL);
	assert_rc_equal(rc, 0);

	wait_cont_flat();

	/** normal fetch */
	iod.iod_size	= DAOS_REC_ANY;

	print_message("normal fetch\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, NULL, NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(iod.iod_size, 1);

	d_iov_set(&sg_iov[1], buf[1], size);
	sg_iov[1].iov_buf = buf[1];
	sg_iov[1].iov_len = 0;
	sg_iov[1].iov_buf_len = size;
	memset(buf[1], 0, size);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl[1], NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_memory_equal(buf[0], buf[1], size);
	if (sg_iov[1].iov_len != size)
		fail_msg("sg_iov[1].iov_len %zu\n", sg_iov[1].iov_len);

	print_message("degraded fetch data recovery\n");
	shard[0] = 1;
	shard[1] = 3;
	fail_val = daos_shard_fail_value(shard, 2);
	daos_fail_loc_set(DAOS_FAIL_SHARD_OPEN | DAOS_FAIL_ALWAYS);
	daos_fail_value_set(fail_val);

	sg_iov[1].iov_len = 0;
	sg_iov[1].iov_buf_len = size;
	memset(buf[1], 0, size);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl[1], NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_memory_equal(buf[0], buf[1], size);
	if (sg_iov[1].iov_len != size)
		fail_msg("sg_iov[1].iov_len %zu\n", sg_iov[1].iov_len);

	daos_fail_loc_set(0);
	daos_fail_value_set(0);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		D_FREE(buf[i]);
	}

	test_teardown((void **)&arg);
}

static const struct CMUnitTest flat_tests[] = {
    {"FLAT0: update after flatten", update_after_flat, async_disable, test_case_teardown},
    {"FLAT1: basic container flatten", basic_cont_flatten, async_disable, test_case_teardown},
    {"FLAT2: ec conditional fetch after flatten", ec_cond_fetch, async_disable, test_case_teardown},
    {"FLAT3: ec data recovery after flatten", ec_data_recov, async_disable, test_case_teardown},
};

int
run_daos_flat_io_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(flat_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests("DAOS_OBJ_FLAT", flat_tests, ARRAY_SIZE(flat_tests),
				sub_tests, sub_tests_size, flat_setup,
				test_teardown);

	par_barrier(PAR_COMM_WORLD);

	return rc;
}
