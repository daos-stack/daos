/**
 * (C) Copyright 2016-2022 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_obj_array.c
 */

#include <daos/checksum.h>
#include "daos_test.h"

#define STACK_BUF_LEN	24

static void
byte_array_simple_stack(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t	 dkey;
	d_sg_list_t	 sgl;
	d_iov_t	 sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	char		 buf_out[STACK_BUF_LEN];
	char		 buf[STACK_BUF_LEN];
	int		 rc;

	dts_buf_render(buf, STACK_BUF_LEN);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov, buf, sizeof(buf));
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= 0;
	recx.rx_nr	= sizeof(buf);
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %d bytes in a single recx\n", STACK_BUF_LEN);
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	/** fetch record size & verify */
	print_message("fetching record size\n");
	iod.iod_size	= DAOS_REC_ANY;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod,
			    NULL, NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(iod.iod_size, 1);

	/** fetch */
	print_message("reading data back ...\n");
	memset(buf_out, 0, sizeof(buf_out));
	d_iov_set(&sg_iov, buf_out, sizeof(buf_out));
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	/** Verify data consistency */
	print_message("validating data ...\n");
	assert_memory_equal(buf, buf_out, sizeof(buf));

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	print_message("all good\n");
}

static void
array_simple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	d_iov_t		dkey;
	d_sg_list_t	sgl;
	d_iov_t		sg_iov;
	daos_iod_t	iod;
	daos_recx_t	recx;
	char		*buf;
	char		*buf_out;
	int		rc;

	D_ALLOC(buf, arg->size * arg->nr);
	assert_non_null(buf);

	dts_buf_render(buf, arg->size * arg->nr);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov, buf, arg->size * arg->nr);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_nr	= 1;
	iod.iod_size	= arg->size;
	srand(time(NULL) + arg->size);
	recx.rx_idx	= rand();
	recx.rx_nr	= arg->nr;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %lu records of %lu bytes each at offset %lu\n",
		      recx.rx_nr, iod.iod_size, recx.rx_idx);
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	/** fetch data back */
	print_message("reading data back ...\n");
	D_ALLOC(buf_out, arg->size * arg->nr);
	assert_non_null(buf_out);
	memset(buf_out, 0, arg->size * arg->nr);
	d_iov_set(&sg_iov, buf_out, arg->size * arg->nr);
	iod.iod_size	= DAOS_REC_ANY;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	/** verify record size */
	print_message("validating record size ...\n");
	assert_int_equal(iod.iod_size, arg->size);
	/** Verify data consistency */
	print_message("validating data ...\n");
	assert_memory_equal(buf, buf_out, arg->size * arg->nr);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	D_FREE(buf_out);
	D_FREE(buf);
	print_message("all good\n");
}

#define NUM_RECORDS 24

static void
array_partial(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t	 dkey;
	d_sg_list_t	 sgl;
	d_iov_t	 sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	daos_recx_t	 recxs[4];
	char		*buf;
	char		*buf_out;
	int		 rc, i;

	arg->size = 4;

	D_ALLOC(buf, arg->size * NUM_RECORDS);
	assert_non_null(buf);

	dts_buf_render(buf, arg->size * NUM_RECORDS);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov, buf, arg->size * NUM_RECORDS);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_nr	= 1;
	iod.iod_size	= arg->size;
	recx.rx_idx	= 0;
	recx.rx_nr	= NUM_RECORDS;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %lu records of %lu bytes each at offset %lu\n",
		      recx.rx_nr, iod.iod_size, recx.rx_idx);
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	/** fetch 1/2 of the records back */
	print_message("reading 1/2 of the records back ...\n");
	D_ALLOC(buf_out, arg->size * NUM_RECORDS/2);
	assert_non_null(buf_out);
	memset(buf_out, 0, arg->size * NUM_RECORDS/2);
	d_iov_set(&sg_iov, buf_out, arg->size * NUM_RECORDS/2);
	iod.iod_size	= arg->size;
	iod.iod_nr	= 4;
	for (i = 0; i < 4; i++) {
		recxs[i].rx_idx	= i*6;
		recxs[i].rx_nr	= 3;
	}

	iod.iod_recxs = recxs;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	print_message("fetch returns %d\n", rc);
	assert_rc_equal(rc, 0);
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
	assert_rc_equal(rc, 0);

	D_FREE(buf_out);
	D_FREE(buf);
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
	d_iov_t	 dkey;
	d_sg_list_t	 sgl;
	d_iov_t	 sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	char		 buf_out[4608];
	char		 buf[192];
	int		 rc;

	dts_buf_render(buf, 192);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov, buf, sizeof(buf));
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= 27136;
	recx.rx_nr	= sizeof(buf);
	iod.iod_recxs	= &recx;
iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %d bytes in a single recx\n", 192);
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	recx.rx_idx     = 30208;
	iod.iod_recxs	= &recx;
	print_message("writing %d bytes in a single recx\n", 192);
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	recx.rx_idx     = 28672;
	iod.iod_recxs	= &recx;
	print_message("writing %d bytes in a single recx\n", 192);
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	/** fetch */
	print_message("reading data back ...\n");
	memset(buf_out, 0, sizeof(buf_out));
	d_iov_set(&sg_iov, buf_out, sizeof(buf_out));
	recx.rx_idx     = 27136;
	recx.rx_nr      = sizeof(buf_out);
	iod.iod_recxs	= &recx;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	print_message("all good\n");
}

static void
read_empty(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t	 dkey;
	d_sg_list_t	 sgl;
	d_iov_t	 sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	char		 *buf;
	daos_size_t	 buf_len;
	int		 rc;

	buf_len = 4194304;
	D_ALLOC(buf, buf_len);
	D_ASSERT(buf != NULL);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov, buf, buf_len);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= 0;
	recx.rx_nr	= buf_len;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** fetch */
	print_message("reading empty object ...\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	print_message("all good\n");
	D_FREE(buf);
}

#define ENUM_DESC_BUF	512
#define ENUM_DESC_NR	5

enum {
	OBJ_DKEY,
	OBJ_AKEY
};

static void
enumerate_key(daos_handle_t oh, int *total_nr, daos_key_t *dkey, int key_type)
{
	char		*buf;
	daos_key_desc_t  kds[ENUM_DESC_NR];
	daos_anchor_t	 anchor = {0};
	d_sg_list_t	 sgl;
	d_iov_t		 sg_iov;
	int		 key_nr = 0;
	int		 rc;

	buf = malloc(ENUM_DESC_BUF);
	d_iov_set(&sg_iov, buf, ENUM_DESC_BUF);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t nr = ENUM_DESC_NR;

		memset(buf, 0, ENUM_DESC_BUF);
		if (key_type == OBJ_DKEY)
			rc = daos_obj_list_dkey(oh, DAOS_TX_NONE, &nr, kds,
						&sgl, &anchor, NULL);
		else
			rc = daos_obj_list_akey(oh, DAOS_TX_NONE, dkey, &nr,
						kds, &sgl, &anchor, NULL);
		assert_rc_equal(rc, 0);
		if (nr == 0)
			continue;
		key_nr += nr;
	}

	*total_nr = key_nr;
}

#define SM_BUF_LEN 10
/** Total number of keys to insert */
#define KEYS 100
/** Number of existing keys to punch */
#define E_KEYS2PUNCH 10
/** Number of non-existing keys to punch */
#define NE_KEYS2PUNCH 10

static void
array_dkey_punch_enumerate(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	d_iov_t		dkey;
	d_sg_list_t	sgl;
	d_iov_t		sg_iov;
	daos_iod_t	iod;
	daos_recx_t	recx;
	char		buf[SM_BUF_LEN];
	int		total_nr;
	int		i;
	int		rc;

	dts_buf_render(buf, SM_BUF_LEN);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init scatter/gather */
	d_iov_set(&sg_iov, buf, sizeof(buf));
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_nr	= SM_BUF_LEN;
	recx.rx_idx	= 0;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	print_message("Inserting %d dkeys...\n", KEYS);
	for (i = 0; i < KEYS; i++) {
		char dkey_str[32] = {0};

		/** init dkey */
		sprintf(dkey_str, "dkey_%d", i);
		d_iov_set(&dkey, dkey_str, strlen(dkey_str));
		rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
				     NULL);
		assert_rc_equal(rc, 0);
	}

	total_nr = 0;
	print_message("Enumerating dkeys before punch...\n");
	enumerate_key(oh, &total_nr, NULL, OBJ_DKEY);
	print_message("DONE DKEY Enumeration (%d extents) -------\n", total_nr);
	assert_int_equal(total_nr, KEYS);

	/** punch last 10 dkeys, and another 10 non-existent dkeys */
	print_message("Punching %d dkeys, and %d dkeys that don't exist.\n",
		      E_KEYS2PUNCH, NE_KEYS2PUNCH);
	for (i = KEYS - E_KEYS2PUNCH; i < KEYS + NE_KEYS2PUNCH; i++) {
		char dkey_str[32] = {0};

		/** init dkey */
		sprintf(dkey_str, "dkey_%d", i);
		d_iov_set(&dkey, dkey_str, strlen(dkey_str));
		rc = daos_obj_punch_dkeys(oh, DAOS_TX_NONE, 0, 1, &dkey, NULL);
		assert_rc_equal(rc, 0);
	}

	total_nr = 0;
	print_message("Enumerating dkeys after punch...\n");
	enumerate_key(oh, &total_nr, NULL, OBJ_DKEY);
	print_message("DONE DKEY Enumeration (%d extents) -------\n", total_nr);
	assert_int_equal(total_nr, KEYS - E_KEYS2PUNCH);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	print_message("all good\n");
}

static void
array_akey_punch_enumerate(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	d_iov_t		dkey;
	d_sg_list_t	sgl;
	d_iov_t		sg_iov;
	daos_iod_t	iod;
	daos_recx_t	recx;
	char		buf[SM_BUF_LEN];
	int		total_nr;
	int		i;
	int		rc;

	dts_buf_render(buf, SM_BUF_LEN);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init scatter/gather */
	d_iov_set(&sg_iov, buf, sizeof(buf));
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init I/O descriptor */
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_nr	= SM_BUF_LEN;
	recx.rx_idx	= 0;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	print_message("Inserting %d akeys...\n", KEYS);
	for (i = 0; i < KEYS; i++) {
		char akey_str[32] = {0};

		sprintf(akey_str, "akey_%d", i);
		d_iov_set(&iod.iod_name, akey_str, strlen(akey_str));
		rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
				     NULL);
		assert_rc_equal(rc, 0);
	}

	total_nr = 0;
	print_message("Enumerating akeys before punch...\n");
	enumerate_key(oh, &total_nr, &dkey, OBJ_AKEY);
	print_message("DONE AKEY Enumeration (%d extents) -------\n", total_nr);
	assert_int_equal(total_nr, KEYS);

	/** punch last 10 akeys, and another 10 non-existent akeys */
	print_message("Punching %d akeys, and %d akeys that don't exist.\n",
		      E_KEYS2PUNCH, NE_KEYS2PUNCH);
	for (i = KEYS - E_KEYS2PUNCH; i < KEYS + NE_KEYS2PUNCH; i++) {
		char akey_str[32] = {0};
		daos_key_t akey;

		sprintf(akey_str, "akey_%d", i);
		d_iov_set(&akey, akey_str, strlen(akey_str));
		rc = daos_obj_punch_akeys(oh, DAOS_TX_NONE, 0, &dkey, 1, &akey,
					  NULL);
		assert_rc_equal(rc, 0);
	}

	total_nr = 0;
	print_message("Enumerating akeys after punch...\n");
	enumerate_key(oh, &total_nr, &dkey, OBJ_AKEY);
	print_message("DONE AKEY Enumeration (%d extents) -------\n", total_nr);
	assert_int_equal(total_nr, KEYS - E_KEYS2PUNCH);

	print_message("Fetch akeys after punch and verify size...\n");
	for (i = 0; i < KEYS; i++) {
		char akey_str[32] = {0};

		sprintf(akey_str, "akey_%d", i);
		d_iov_set(&iod.iod_name, akey_str, strlen(akey_str));

		iod.iod_size = DAOS_REC_ANY;
		rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod,
				    NULL, NULL, NULL);
		assert_rc_equal(rc, 0);
		if (i >= KEYS - E_KEYS2PUNCH)
			assert_int_equal(iod.iod_size, 0);
		else
			assert_int_equal(iod.iod_size, 1);
	}

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	print_message("all good\n");
}

static void
array_recx_punch_enumerate(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	d_iov_t		dkey;
	d_sg_list_t	sgl;
	d_iov_t		sg_iov;
	daos_iod_t	iod;
	daos_recx_t	recx;
	char		buf[SM_BUF_LEN];
	daos_anchor_t	anchor;
	int		total_nr = 0;
	int		i;
	int		rc;

	dts_buf_render(buf, SM_BUF_LEN);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov, buf, sizeof(buf));
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_nr	= SM_BUF_LEN;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** insert 100 extents */
	for (i = 0; i < 100; i++) {
		recx.rx_idx = i * SM_BUF_LEN;
		rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
				     NULL);
		assert_rc_equal(rc, 0);
	}

	/** enumerate extents before punch */
	print_message("Enumerating extents before punch...\n");
	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		daos_size_t size = 0;
		uint32_t nr = 5;
		daos_recx_t recxs[5];
		daos_epoch_range_t eprs[5];

		rc = daos_obj_list_recx(oh, DAOS_TX_NONE, &dkey, &iod.iod_name,
					&size, &nr, recxs, eprs, &anchor, true,
					NULL);
		assert_rc_equal(rc, 0);
		total_nr += nr;
	}
	print_message("DONE recx Enumeration (%d extents) -------\n", total_nr);
	assert_int_equal(total_nr, 100);

	/** punch first 10 records */
	iod.iod_size	= 0;
	recx.rx_nr	= SM_BUF_LEN;
	iod.iod_recxs	= &recx;
	for (i = 0; i < 10; i++) {
		recx.rx_idx = i * SM_BUF_LEN;
		print_message("punching idx: %"PRIu64" len %"PRIu64"\n",
			      recx.rx_idx, recx.rx_nr);
		rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, NULL,
				     NULL);
		assert_rc_equal(rc, 0);
	}

	/** enumerate records again */
	print_message("Enumerating extents after punch...\n");
	memset(&anchor, 0, sizeof(anchor));
	total_nr = 0;
	while (!daos_anchor_is_eof(&anchor)) {
		daos_size_t size = 0;
		uint32_t nr = 5;
		daos_recx_t recxs[5];
		daos_epoch_range_t eprs[5];

		rc = daos_obj_list_recx(oh, DAOS_TX_NONE, &dkey, &iod.iod_name,
					&size, &nr, recxs, eprs, &anchor, true,
					NULL);
		assert_rc_equal(rc, 0);
		total_nr += nr;
	}
	print_message("DONE recx Enumeration (%d extents) -------\n", total_nr);
	assert_int_equal(total_nr, 90);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	print_message("all good\n");
}

static void
array_recx_read_incomplete(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl;
	d_iov_t		 sg_iov[SM_BUF_LEN];
	daos_iod_t	 iod;
	daos_recx_t	 recx[SM_BUF_LEN];
	char		 wbuf[SM_BUF_LEN];
	char		 rbuf[SM_BUF_LEN];
	char		 rbuf_orig[SM_BUF_LEN];
	int		 i;
	int		 rc;

	dts_buf_render(wbuf, SM_BUF_LEN);
	dts_buf_render(rbuf_orig, SM_BUF_LEN);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov[0], wbuf, 1);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx[0].rx_nr	= 1;
	iod.iod_recxs	= recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** insert 1 extent at location 2 */
	recx[0].rx_idx = 2;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	/** fetch all records */
	print_message("Fetching all records...\n");
	memcpy(rbuf, rbuf_orig, sizeof(rbuf));
	d_iov_set(&sg_iov[0], rbuf, sizeof(rbuf));
	recx[0].rx_idx = 0;
	recx[0].rx_nr	= 10;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("DONE record fetch --------\n");
	for (i = 0; i < SM_BUF_LEN; i++) {
		if (i == 2)
			assert_int_equal((int)rbuf[i], (int)wbuf[0]);
		else
			assert_int_equal((int)rbuf[i], (int)rbuf_orig[i]);
	}

	/** fetch every other record to contiguous buffer */
	print_message("Fetching every other record to contiguous buffer...\n");
	memcpy(rbuf, rbuf_orig, sizeof(rbuf));
	d_iov_set(&sg_iov[0], rbuf, SM_BUF_LEN/2);
	iod.iod_nr = SM_BUF_LEN/2;
	for (i = 0; i < SM_BUF_LEN/2; i++) {
		recx[i].rx_idx = i * 2;
		recx[i].rx_nr	= 1;
	}
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("DONE record fetch --------\n");
	for (i = 0; i < SM_BUF_LEN / 2; i++) {
		if (i == 1)
			assert_int_equal((int)rbuf[i], (int)wbuf[0]);
		else
			assert_int_equal((int)rbuf[i], (int)rbuf_orig[i]);
	}

	/** fetch every other record to noncontiguous buffer */
	print_message("Fetching every other record to noncontiguous buffer\n");
	memcpy(rbuf, rbuf_orig, sizeof(rbuf));
	sgl.sg_nr = SM_BUF_LEN/2;
	for (i = 0; i < SM_BUF_LEN/2; i++) {
		/* set so it matches the original index */
		d_iov_set(&sg_iov[i], &rbuf[i * 2], 1);
	}
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("DONE record fetch --------\n");
	for (i = 0; i < SM_BUF_LEN/2; i++) {
		if (i == 2)
			assert_int_equal((int)rbuf[i], (int)wbuf[0]);
		else
			assert_int_equal((int)rbuf[i], (int)rbuf_orig[i]);
	}

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	print_message("all good\n");
}

#define assert_recx_equal(a, b) do {\
	assert_int_equal(a.rx_nr, b.rx_nr); \
	assert_int_equal(a.rx_idx, b.rx_idx); } \
	while (0)

static void
fetch_array_with_map(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl;
	d_iov_t		 sg_iov[SM_BUF_LEN];
	daos_iod_t	 iod;
	daos_iom_t	 map = {0};
	daos_recx_t	 map_recxs[SM_BUF_LEN];
	daos_recx_t	 update_recxs[SM_BUF_LEN];
	daos_recx_t	 fetch_recx;
	daos_recx_t	 zero_recx = {0};
	char		 buf[SM_BUF_LEN];
	int		 rc;

	dts_buf_render(buf, SM_BUF_LEN);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov[0], buf, SM_BUF_LEN);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = sg_iov;

	/** init map */
	map.iom_recxs = map_recxs;
	map.iom_nr = SM_BUF_LEN;
	map.iom_flags = DAOS_IOMF_DETAIL;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_size = 1;
	iod.iod_recxs = update_recxs;
	iod.iod_type = DAOS_IOD_ARRAY;

	/** insert 3 extents at location 2, 4 & 6*/
	iod.iod_nr = 3;
	update_recxs[0].rx_idx = 2;
	update_recxs[0].rx_nr = 1;
	update_recxs[1].rx_idx = 4;
	update_recxs[1].rx_nr = 1;
	update_recxs[2].rx_idx = 6;
	update_recxs[2].rx_nr = 1;

	/** Update */
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(0, rc);

	/** setup for fetch */
	fetch_recx.rx_idx = 0;
	fetch_recx.rx_nr = 10;
	iod.iod_nr = 1;
	iod.iod_recxs = &fetch_recx;

	/** Sanity check without map it still works */
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_rc_equal(0, rc);

	/** get map */
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    &map, NULL);
	assert_rc_equal(0, rc);

	assert_int_equal(3, map.iom_nr_out);
	assert_int_equal(SM_BUF_LEN, map.iom_nr);
	assert_int_equal(iod.iod_size, map.iom_size);
	assert_int_equal(iod.iod_type, map.iom_type);
	assert_recx_equal(update_recxs[0], map.iom_recx_lo);
	assert_recx_equal(update_recxs[2], map.iom_recx_hi);
	assert_recx_equal(update_recxs[0], map.iom_recxs[0]);
	assert_recx_equal(update_recxs[1], map.iom_recxs[1]);
	assert_recx_equal(update_recxs[2], map.iom_recxs[2]);

	/** get map with too small recx buffer */
	memset(map_recxs, 0, sizeof(map_recxs));
	map.iom_nr = 1;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    &map, NULL);
	assert_rc_equal(0, rc);
	assert_int_equal(3, map.iom_nr_out);
	assert_int_equal(1, map.iom_nr);
	assert_recx_equal(update_recxs[0], map.iom_recx_lo);
	assert_recx_equal(update_recxs[2], map.iom_recx_hi);
	assert_recx_equal(update_recxs[0], map.iom_recxs[0]);
	assert_recx_equal(zero_recx, map.iom_recxs[1]);
	assert_recx_equal(zero_recx, map.iom_recxs[2]);

	/** get map with no recx buffer */
	memset(map_recxs, 0, sizeof(map_recxs));
	map.iom_nr = 0;
	map.iom_recxs = NULL;
	map.iom_flags = 0;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    &map, NULL);
	assert_rc_equal(0, rc);
	assert_int_equal(0, map.iom_nr);
	/** still get hi/lo */
	assert_recx_equal(update_recxs[0], map.iom_recx_lo);
	assert_recx_equal(update_recxs[2], map.iom_recx_hi);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
}

static void
fetch_array_with_map_2(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl;
	d_iov_t		 sg_iov[SM_BUF_LEN];
	daos_iod_t	 iod;
	daos_iom_t	 map = {0};
	daos_recx_t	 map_recxs[SM_BUF_LEN];
	daos_recx_t	 update_recxs[SM_BUF_LEN];
	daos_recx_t	 fetch_recx;
	char		 buf[SM_BUF_LEN];
	int		 rc;

	dts_buf_render(buf, SM_BUF_LEN);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov[0], buf, SM_BUF_LEN);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = sg_iov;

	/** init map */
	map.iom_recxs = map_recxs;
	map.iom_nr = SM_BUF_LEN;
	map.iom_flags = DAOS_IOMF_DETAIL;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_size = 1;
	iod.iod_recxs = update_recxs;
	iod.iod_type = DAOS_IOD_ARRAY;

	/** insert 3 extents at location 2, 4 & 6*/
	iod.iod_nr = 1;
	update_recxs[0].rx_idx = 10;
	update_recxs[0].rx_nr = SM_BUF_LEN;

	/** Update */
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(0, rc);

	/** setup for fetch */
	fetch_recx.rx_idx = 10;
	fetch_recx.rx_nr = SM_BUF_LEN;
	iod.iod_nr = 1;
	iod.iod_recxs = &fetch_recx;

	/** get map */
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    &map, NULL);
	assert_rc_equal(0, rc);

	assert_int_equal(1, map.iom_nr_out);
	assert_int_equal(SM_BUF_LEN, map.iom_nr);
	assert_int_equal(iod.iod_size, map.iom_size);
	assert_int_equal(iod.iod_type, map.iom_type);
	assert_recx_equal(fetch_recx, map.iom_recx_lo);
	assert_recx_equal(fetch_recx, map.iom_recx_hi);
	assert_recx_equal(fetch_recx, map.iom_recxs[0]);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
}

static void
fetch_array_with_map_3(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl;
	d_iov_t		 sg_iov[SM_BUF_LEN];
	daos_iod_t	 iod;
	daos_iom_t	 map = {0};
	daos_recx_t	 map_recxs[SM_BUF_LEN];
	daos_recx_t	 update_recxs[SM_BUF_LEN];
	daos_recx_t	 fetch_recx;
	char		 buf[SM_BUF_LEN];
	int		 rc;

	dts_buf_render(buf, SM_BUF_LEN);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov[0], buf, SM_BUF_LEN);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = sg_iov;

	/** init map */
	map.iom_recxs = map_recxs;
	map.iom_nr = SM_BUF_LEN;
	map.iom_flags = DAOS_IOMF_DETAIL;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_size = 1;
	iod.iod_recxs = update_recxs;
	iod.iod_type = DAOS_IOD_ARRAY;

	/** insert 3 extents at location 2, 4 & 6*/
	iod.iod_nr = 1;
	update_recxs[0].rx_idx = 10;
	update_recxs[0].rx_nr = SM_BUF_LEN;

	/** Update */
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(0, rc);

	/** setup for fetch */
	fetch_recx.rx_idx = 12;
	fetch_recx.rx_nr = SM_BUF_LEN - 2;
	iod.iod_nr = 1;
	iod.iod_recxs = &fetch_recx;

	/** get map */
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    &map, NULL);
	assert_rc_equal(0, rc);

	assert_int_equal(1, map.iom_nr_out);
	assert_int_equal(SM_BUF_LEN, map.iom_nr);
	assert_int_equal(iod.iod_size, map.iom_size);
	assert_int_equal(iod.iod_type, map.iom_type);
	assert_recx_equal(fetch_recx, map.iom_recx_lo);
	assert_recx_equal(fetch_recx, map.iom_recx_hi);
	assert_recx_equal(fetch_recx, map.iom_recxs[0]);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
}

static void
fetch_array_with_map_4(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl;
	d_iov_t		 sg_iov[SM_BUF_LEN];
	daos_iod_t	 iod;
	daos_iom_t	 map = {0};
	daos_recx_t	 map_recxs[SM_BUF_LEN];
	daos_recx_t	 update_recxs[SM_BUF_LEN];
	daos_recx_t	 fetch_recxs[SM_BUF_LEN];
	char		 buf[1024];
	int		 rc;

	dts_buf_render(buf, SM_BUF_LEN);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov[0], buf, 1024);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = sg_iov;

	/** init map */
	map.iom_recxs = map_recxs;
	map.iom_nr = SM_BUF_LEN;
	map.iom_flags = DAOS_IOMF_DETAIL;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_size = 1;
	iod.iod_recxs = update_recxs;
	iod.iod_type = DAOS_IOD_ARRAY;

	iod.iod_nr = 3;
	update_recxs[0].rx_idx = 2;
	update_recxs[0].rx_nr = 8;
	update_recxs[1].rx_idx = 40;
	update_recxs[1].rx_nr = 8;
	update_recxs[2].rx_idx = 60;
	update_recxs[2].rx_nr = 8;

	/** Update */
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(0, rc);

	/** setup for fetch */
	fetch_recxs[0].rx_idx = 2;
	fetch_recxs[0].rx_nr = 8;
	fetch_recxs[1].rx_idx = 40;
	fetch_recxs[1].rx_nr = 8;
	fetch_recxs[2].rx_idx = 60;
	fetch_recxs[2].rx_nr = 8;
	iod.iod_recxs = fetch_recxs;

	/** get map */
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    &map, NULL);
	assert_rc_equal(0, rc);

	assert_int_equal(3, map.iom_nr_out);
	assert_recx_equal(fetch_recxs[0], map.iom_recxs[0]);
	assert_recx_equal(fetch_recxs[1], map.iom_recxs[1]);
	assert_recx_equal(fetch_recxs[2], map.iom_recxs[2]);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
}

static void
small_sgl(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl[3];
	d_iov_t		 sg_iov[3];
	daos_iod_t	 iod[3];
	char		 buf1[24];
	char		 buf2[24];
	char		 buf3[24];
	int		 i, rc;

	dts_buf_render(buf1, 24);
	dts_buf_render(buf2, 24);
	dts_buf_render(buf3, 24);

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov[0], buf1, 4);
	d_iov_set(&sg_iov[1], buf2, 8);
	d_iov_set(&sg_iov[2], buf3, 4);

	for (i = 0; i < 3; i++) {
		sgl[i].sg_nr = 1;
		sgl[i].sg_nr_out = 0;
		sgl[i].sg_iovs = &sg_iov[i];
		iod[i].iod_nr = 1;
		iod[i].iod_recxs = NULL;
		iod[i].iod_type = DAOS_IOD_SINGLE;
	}

	d_iov_set(&iod[0].iod_name, "akey1", strlen("akey1"));
	d_iov_set(&iod[1].iod_name, "akey2", strlen("akey2"));
	d_iov_set(&iod[2].iod_name, "akey3", strlen("akey2"));
	iod[0].iod_size = 4;
	iod[1].iod_size = 8;
	iod[2].iod_size = 4;

	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 3, iod, sgl, NULL);
	assert_rc_equal(rc, 0);

	/** setup for fetch */
	d_iov_set(&sg_iov[0], buf1, 4);
	d_iov_set(&sg_iov[1], buf2, 2);
	d_iov_set(&sg_iov[2], buf3, 10);
	for (i = 0; i < 3; i++)
		iod[i].iod_size = DAOS_REC_ANY;

	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 3, iod, sgl,
			    NULL, NULL);
	assert_rc_equal(rc, -DER_REC2BIG);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
}

static void
cond_ops(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	d_iov_t		dkey;
	d_sg_list_t	sgl[2];
	d_iov_t		sg_iov[2];
	daos_iod_t	iod[2];
	daos_recx_t	recx[2];
	char		akey_str[2][10];
	uint64_t	flags;
	char		buf[2][STACK_BUF_LEN];
	int		i, rc;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	for (i = 0; i < 2; i++) {
		dts_buf_render(buf[i], STACK_BUF_LEN);

		/** init scatter/gather */
		d_iov_set(&sg_iov[i], buf[i], sizeof(buf[i]));
		sgl[i].sg_nr		= 1;
		sgl[i].sg_nr_out	= 0;
		sgl[i].sg_iovs		= &sg_iov[i];

		sprintf(akey_str[i], "akey_%d", i);
		/** init I/O descriptor */
		d_iov_set(&iod[i].iod_name, akey_str[i], strlen(akey_str[i]));
		iod[i].iod_nr	= 1;
		iod[i].iod_size	= 1;
		recx[i].rx_idx	= 0;
		recx[i].rx_nr	= sizeof(buf[i]);
		iod[i].iod_recxs = &recx[i];
		iod[i].iod_type	= DAOS_IOD_ARRAY;
	}

	flags = DAOS_COND_DKEY_UPDATE | DAOS_COND_AKEY_INSERT;
	/** Cond update dkey should fail */
	rc = daos_obj_update(oh, DAOS_TX_NONE, flags, &dkey, 2, iod, sgl, NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	flags = DAOS_COND_DKEY_INSERT | DAOS_COND_AKEY_UPDATE;
	/** Cond update akey should fail */
	rc = daos_obj_update(oh, DAOS_TX_NONE, flags, &dkey, 2, iod, sgl, NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	flags = DAOS_COND_DKEY_INSERT | DAOS_COND_PER_AKEY;
	iod[0].iod_flags = DAOS_COND_AKEY_INSERT;
	/** akey doesn't exist so update should fail */
	iod[1].iod_flags = DAOS_COND_AKEY_UPDATE;
	rc = daos_obj_update(oh, DAOS_TX_NONE, flags, &dkey, 2, iod, sgl, NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	/** should succeed */
	iod[1].iod_flags = DAOS_COND_AKEY_INSERT;
	rc = daos_obj_update(oh, DAOS_TX_NONE, flags, &dkey, 2, iod, sgl, NULL);
	assert_rc_equal(rc, 0);

	/** both exist now, insert should fail */
	flags = DAOS_COND_DKEY_UPDATE | DAOS_COND_PER_AKEY;
	iod[0].iod_flags = DAOS_COND_AKEY_INSERT;
	iod[1].iod_flags = DAOS_COND_AKEY_UPDATE;
	rc = daos_obj_update(oh, DAOS_TX_NONE, flags, &dkey, 2, iod, sgl, NULL);
	assert_rc_equal(rc, -DER_EXIST);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	print_message("all good\n");
}


#define NUM_IOS 256
#define IO_SIZE 24
#define NUM_TH	8

struct test_async_arg {
	int			thread_idx;
	pthread_barrier_t	*barrier;
	daos_handle_t		oh;
	test_arg_t		*arg;
};

struct test_async_arg th_arg[NUM_TH];
pthread_t test_tid[NUM_TH];

static void *
test_async_io(void *arg)
{
	struct test_async_arg	*targ = arg;
	d_iov_t			dkey[NUM_IOS];
	d_sg_list_t		sgls[NUM_IOS];
	d_iov_t			sg_iovs[NUM_IOS];
	daos_iod_t		iods[NUM_IOS];
	daos_recx_t		recx[NUM_IOS];
	char			akey_str[NUM_IOS][16];
	char			dkey_str[NUM_IOS][16];
	char			buf[NUM_IOS][IO_SIZE];
	daos_handle_t		eqh;
	int			i, rc;

	print_message("test_async_io thread: %d\n", targ->thread_idx);

	rc = daos_eq_create(&eqh);
	D_ASSERT(rc == 0);
	print_message("dfs_test_read_thread %d create eqh\n", targ->thread_idx);

	for (i = 0; i < NUM_IOS; i++) {
		daos_event_t	*ev;

		D_ALLOC_PTR_NZ(ev);
		D_ASSERT(ev != NULL);

		rc = daos_event_init(ev, eqh, NULL);
		assert_rc_equal(rc, 0);

		/** init dkey */
		sprintf(dkey_str[i], "dkey_%d_%d", targ->thread_idx, i);
		d_iov_set(&dkey[i], dkey_str[i], strlen(dkey_str[i]));
		dts_buf_render(buf[i], IO_SIZE);

		d_iov_set(&sg_iovs[i], buf[i], sizeof(buf[i]));
		sgls[i].sg_nr		= 1;
		sgls[i].sg_nr_out	= 0;
		sgls[i].sg_iovs		= &sg_iovs[i];

		sprintf(akey_str[i], "akey_%d", i);
		d_iov_set(&iods[i].iod_name, akey_str[i], strlen(akey_str[i]));
		iods[i].iod_nr		= 1;
		iods[i].iod_size	= 1;
		recx[i].rx_idx		= i * IO_SIZE;
		recx[i].rx_nr		= IO_SIZE;
		iods[i].iod_recxs	= &recx[i];
		iods[i].iod_type	= DAOS_IOD_ARRAY;

		rc = daos_obj_update(targ->oh, DAOS_TX_NONE, 0, &dkey[i], 1, &iods[i], &sgls[i],
				     ev);
		assert_rc_equal(rc, 0);
	}

	pthread_barrier_wait(targ->barrier);

	while (1) {
		struct daos_event	*eps[NUM_IOS] = { 0 };

		rc = daos_eq_poll(eqh, 0, DAOS_EQ_NOWAIT, NUM_IOS, eps);
		if (rc < 0) {
			print_error("EQ poll failed: %d\n", rc);
			pthread_exit(NULL);
		}

		for (i = 0; i < rc; i++)
			daos_event_fini(eps[i]);

		/** exit when nothing left in EQ */
		rc = daos_eq_query(eqh, DAOS_EQR_ALL, 0, NULL);
		if (rc < 0) {
			print_error("EQ poll failed: %d\n", rc);
			pthread_exit(NULL);
		}
		if (rc == 0) {
			rc = daos_eq_destroy(eqh, 0);
			assert_rc_equal(rc, 0);
			pthread_exit(NULL);
		}
	}
}

static void
mth_async_io(void **state)
{
	test_arg_t		*arg = *state;
	pthread_barrier_t	barrier;
	daos_obj_id_t		oid;
	daos_handle_t		oh;
	int			i, rc;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	pthread_barrier_init(&barrier, NULL, NUM_TH + 1);

	for (i = 0; i < NUM_TH; i++) {
		th_arg[i].thread_idx	= i;
		th_arg[i].oh		= oh;
		th_arg[i].arg		= arg;
		th_arg[i].barrier	= &barrier;
		rc = pthread_create(&test_tid[i], NULL, test_async_io, &th_arg[i]);
		assert_int_equal(rc, 0);
	}

	pthread_barrier_wait(&barrier);

	for (i = 0; i < NUM_TH; i++) {
		rc = pthread_join(test_tid[i], NULL);
		assert_int_equal(rc, 0);
	}

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	print_message("all good\n");
}

#define ONE_MB  (1024 * 1024)
#define FOUR_KB (4 * 1024)

/**
 * array_recx_read_mixed_iov - Test mixed IOV read operations
 * @state: Test arguments
 *
 * Validates DAOS array read functionality with fragmented IOVs:
 * - Writes 1MB data using single IOV
 * - Reads back using 1091 mixed IOVs (64x4KB + 1024x4B + 2x0B + 1xremaining)
 * - Verifies data integrity through segmented checks
 */
static void
array_recx_read_mixed_iov(void **state)
{
	test_arg_t   *arg = *state;
	daos_obj_id_t oid;
	daos_handle_t oh;
	d_iov_t       dkey;
	d_sg_list_t   sgl;
	daos_iod_t    iod;
	daos_recx_t   recx;
	char         *wbuf, *rbuf;
	d_iov_t      *sg_iovs     = NULL;
	int           total_iovs  = 64 + 1024 + 2 + 1;
	int           data_offset = 0;
	int           rc, i, remain_size;

	/* Object initialization */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc  = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/* Buffer allocation with guard pages */
	wbuf = malloc(ONE_MB);
	assert_true(wbuf != NULL);
	dts_buf_render(wbuf, ONE_MB);
	rbuf = calloc(1, ONE_MB + FOUR_KB);
	assert_true(rbuf != NULL);

	/* Build fragmented read IOV structure */
	sg_iovs = calloc(total_iovs, sizeof(d_iov_t));
	assert_true(sg_iovs != NULL);

	/* 64x4KB IOVs for first 256KB */
	for (i = 0; i < 64; i++) {
		d_iov_set(&sg_iovs[data_offset], rbuf + data_offset * FOUR_KB, FOUR_KB);
		data_offset++;
	}

	/* 1024x4B IOVs for next 4KB */
	for (i = 0; i < 1024; i++) {
		d_iov_set(&sg_iovs[data_offset], rbuf + 256 * 1024 + i * 4, 4);
		data_offset++;
	}

	/* 2x zero-length IOVs */
	for (i = 0; i < 2; i++) {
		d_iov_set(&sg_iovs[data_offset], NULL, 0);
		data_offset++;
	}

	/* Single IOV for remaining data (260KB+) */
	remain_size = ONE_MB - (256 * 1024 + 4 * 1024);
	d_iov_set(&sg_iovs[data_offset], rbuf + (256 + 4) * 1024, remain_size);
	data_offset++;

	/* Configure scatter-gather list */
	sgl = (d_sg_list_t){.sg_nr = total_iovs, .sg_nr_out = 0, .sg_iovs = sg_iovs};

	/* Prepare I/O descriptor */
	d_iov_set(&dkey, "megakey", 7);
	d_iov_set(&iod.iod_name, "big_akey", 8);
	iod.iod_nr    = 1;
	iod.iod_size  = 1;
	iod.iod_type  = DAOS_IOD_ARRAY;
	iod.iod_recxs = &recx;
	recx.rx_idx   = 0;
	recx.rx_nr    = ONE_MB;

	/* Write initial data with single IOV */
	d_sg_list_t write_sgl = {
	    .sg_nr   = 1,
	    .sg_iovs = &(d_iov_t){.iov_buf = wbuf, .iov_len = ONE_MB, .iov_buf_len = ONE_MB}};
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &write_sgl, NULL);
	assert_rc_equal(rc, 0);

	/* Read back using fragmented IOVs */
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_rc_equal(rc, 0);

	assert_memory_equal(wbuf, rbuf, ONE_MB);

	free(wbuf);
	free(rbuf);
	free(sg_iovs);
	daos_obj_close(oh, NULL);
}

/**
 * array_recx_write_mixed_iov_read_single - Test mixed IOV writes with single read
 * @state: Test arguments
 *
 * Validates DAOS object I/O with complex scatter-gather patterns:
 * - Writes 1MB data using 1091 mixed IOVs (4KB/4B/zero-length chunks)
 * - Reads back with single IOV and verifies data integrity
 * - Stress-tests IOV fragmentation handling in DAOS stack
 */
static void
array_recx_write_mixed_iov_read_single(void **state)
{
	test_arg_t   *arg = *state;
	daos_obj_id_t oid;
	daos_handle_t oh;
	d_iov_t       dkey;
	daos_iod_t    iod;
	daos_recx_t   recx;
	char         *wbuf, *rbuf;
	d_iov_t      *sg_iovs_write = NULL;
	int           total_iovs    = 64 + 1024 + 2 + 1;
	int           data_offset   = 0;
	int           rc, i, remain_size;

	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc  = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/* Buffer allocation and initialization */
	wbuf = malloc(ONE_MB);
	assert_true(wbuf != NULL);
	dts_buf_render(wbuf, ONE_MB);

	/* Extra 4KB guard page for overflow detection */
	rbuf = calloc(1, ONE_MB + FOUR_KB);
	assert_true(rbuf != NULL);

	/* Build fragmented write IOV structure */
	sg_iovs_write = calloc(total_iovs, sizeof(d_iov_t));
	assert_true(sg_iovs_write != NULL);

	/* Stage 1: 64x4KB sequential IOVs */
	for (i = 0; i < 64; i++) {
		d_iov_set(&sg_iovs_write[data_offset], wbuf + data_offset * FOUR_KB, FOUR_KB);
		data_offset++;
	}

	/* Stage 2: 1024x4B IOVs at 256KB offset */
	for (i = 0; i < 1024; i++) {
		d_iov_set(&sg_iovs_write[data_offset], wbuf + 256 * 1024 + i * 4, 4);
		data_offset++;
	}

	/* Stage 3: 2x zero-length IOVs */
	for (i = 0; i < 2; i++) {
		d_iov_set(&sg_iovs_write[data_offset], NULL, 0);
		data_offset++;
	}

	/* Stage 4: Remaining space as single IOV */
	remain_size = ONE_MB - (256 * 1024 + 4 * 1024);
	d_iov_set(&sg_iovs_write[data_offset], wbuf + (256 + 4) * 1024, remain_size);
	data_offset++;

	/* Configure write scatter-gather list */
	d_sg_list_t write_sgl = {.sg_nr = total_iovs, .sg_nr_out = 0, .sg_iovs = sg_iovs_write};

	/* Prepare data write operation */
	d_iov_set(&dkey, "megakey", 7);
	d_iov_set(&iod.iod_name, "big_akey", 8);
	iod.iod_nr    = 1;
	iod.iod_size  = 1;
	iod.iod_recxs = &recx;
	iod.iod_type  = DAOS_IOD_ARRAY;
	recx.rx_idx   = 0;
	recx.rx_nr    = ONE_MB;

	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &write_sgl, NULL);
	assert_rc_equal(rc, 0);

	/* Single IOV read verification */
	d_iov_t read_iov;
	d_iov_set(&read_iov, rbuf, ONE_MB);

	d_sg_list_t read_sgl = {.sg_nr = 1, .sg_nr_out = 0, .sg_iovs = &read_iov};

	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &read_sgl, NULL, NULL);
	assert_rc_equal(rc, 0);

	assert_memory_equal(wbuf, rbuf, ONE_MB);

	free(wbuf);
	free(rbuf);
	free(sg_iovs_write);
	daos_obj_close(oh, NULL);
}

static const struct CMUnitTest array_tests[] = {
    {"ARRAY0: small_sgl", small_sgl, NULL, test_case_teardown},
    {"ARRAY1: byte array with buffer on stack", byte_array_simple_stack, NULL, test_case_teardown},
    {"ARRAY2: array of uint8_t", array_simple, set_size_uint8, test_case_teardown},
    {"ARRAY3: array of uint16_t", array_simple, set_size_uint16, test_case_teardown},
    {"ARRAY4: array of uint32_t", array_simple, set_size_uint32, test_case_teardown},
    {"ARRAY5: array of uint64_t", array_simple, set_size_uint64, test_case_teardown},
    {"ARRAY6: array of 131071-byte records", array_simple, set_size_131071, test_case_teardown},
    {"ARRAY7: array of 1MB records", array_simple, set_size_1mb, test_case_teardown},
    {"ARRAY8: partial I/O on array", array_partial, NULL, test_case_teardown},
    {"ARRAY9: segfault replicator", replicator, NULL, test_case_teardown},
    {"ARRAY10: read from empty object", read_empty, NULL, test_case_teardown},
    {"ARRAY11: Array DKEY punch/enumerate", array_dkey_punch_enumerate, NULL, test_case_teardown},
    {"ARRAY12: Array AKEY punch/enumerate", array_akey_punch_enumerate, NULL, test_case_teardown},
    {"ARRAY13: Array RECX punch/enumerate", array_recx_punch_enumerate, NULL, test_case_teardown},
    {"ARRAY14: Reading from incomplete array", array_recx_read_incomplete, NULL,
     test_case_teardown},
    {"ARRAY15: Reading from array with holes", fetch_array_with_map, NULL, test_case_teardown},
    {"ARRAY16: Reading from array with holes not starting at idx 0", fetch_array_with_map_2, NULL,
     test_case_teardown},
    {"ARRAY16: Reading from array with holes not starting at idx 0, fetch "
     "idx doesn't align with extent",
     fetch_array_with_map_3, NULL, test_case_teardown},
    {"ARRAY17: Reading from array without holes, but many recxs", fetch_array_with_map_4, NULL,
     test_case_teardown},
    {"ARRAY18: Simple Conditional Operations", cond_ops, NULL, test_case_teardown},
    {"ARRAY19: multi-threaded EQ IO", mth_async_io, NULL, test_case_teardown},
    {"ARRAY20: recx read mixed iov", array_recx_read_mixed_iov, NULL, test_case_teardown},
    {"ARRAY21: recx write mixed iov", array_recx_write_mixed_iov_read_single, NULL,
     test_case_teardown},
};

static int
obj_array_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, false, DEFAULT_POOL_SIZE,
			  0, NULL);
}

int
run_daos_obj_array_test(int rank, int size)
{
	int rc = 0;

	if (rank == 0)
		rc = cmocka_run_group_tests_name("DAOS_Obj_Array",
						 array_tests, obj_array_setup,
						 test_teardown);
	par_barrier(PAR_COMM_WORLD);
	return rc;
}
