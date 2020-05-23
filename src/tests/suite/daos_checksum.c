/**
 * (C) Copyright 2019-2020 Intel Corporation.
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

#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"

#include <daos/checksum.h>
#include <gurt/types.h>
#include <daos_prop.h>

/** fault injection helpers */
static void
set_update_csum_fi()
{
	daos_fail_loc_set(DAOS_CHECKSUM_UPDATE_FAIL | DAOS_FAIL_ALWAYS);
}
static void
set_fetch_csum_fi()
{
	daos_fail_loc_set(DAOS_CHECKSUM_FETCH_FAIL | DAOS_FAIL_ONCE);
}
static void
set_client_data_corrupt_fi()
{
	daos_fail_loc_set(DAOS_CHECKSUM_CDATA_CORRUPT | DAOS_FAIL_ALWAYS);
}
static void
unset_csum_fi()
{
	daos_fail_loc_set(0); /** turn off fault injection */
}

/** daos checksum test context */
struct csum_test_ctx {
	/** Pool */
	daos_handle_t		poh;
	/** Container */
	daos_handle_t		coh;
	daos_cont_info_t	info;
	uuid_t			uuid;
	/** Object */
	daos_handle_t		oh;
	daos_obj_id_t		oid;
	daos_key_t		dkey;
	daos_iod_t		update_iod;
	d_sg_list_t		update_sgl;
	daos_iod_t		fetch_iod;
	d_sg_list_t		fetch_sgl;
	daos_recx_t		recx[4];
};

static void
setup_from_test_args(struct csum_test_ctx *ctx, test_arg_t *state)
{
	ctx->poh = state->pool.poh;
}

/**
 * Setup the container & object portion of the test context. Uses the csum
 * params to create appropriate container properties.
 */
static void
setup_cont_obj(struct csum_test_ctx *ctx, int csum_prop_type, bool csum_sv,
	       int chunksize, daos_oclass_id_t oclass)
{
	daos_prop_t	*props = daos_prop_alloc(3);
	int		 rc;

	uuid_generate(ctx->uuid);

	assert_non_null(props);
	props->dpp_entries[0].dpe_type = DAOS_PROP_CO_CSUM;
	props->dpp_entries[0].dpe_val = csum_prop_type;
	props->dpp_entries[1].dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	props->dpp_entries[1].dpe_val = csum_sv ? DAOS_PROP_CO_CSUM_SV_ON :
					DAOS_PROP_CO_CSUM_SV_OFF;
	props->dpp_entries[2].dpe_type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
	props->dpp_entries[2].dpe_val = chunksize != 0 ? chunksize : 1024*16;

	rc = daos_cont_create(ctx->poh, ctx->uuid, props, NULL);
	daos_prop_free(props);
	assert_int_equal(0, rc);

	rc = daos_cont_open(ctx->poh, ctx->uuid, DAOS_COO_RW,
			    &ctx->coh, &ctx->info, NULL);
	assert_int_equal(0, rc);

	ctx->oid = dts_oid_gen(oclass, 0, 1);
	rc = daos_obj_open(ctx->coh, ctx->oid, 0, &ctx->oh, NULL);
	assert_int_equal(0, rc);
}

static void
setup_simple_data(struct csum_test_ctx *ctx)
{
	dts_sgl_init_with_strings(&ctx->update_sgl, 1, "0123456789");
	/** just need to make the buffers the same size */
	dts_sgl_init_with_strings(&ctx->fetch_sgl, 1, "0000000000");
	d_iov_set(&ctx->dkey, "dkey", strlen("dkey"));
	d_iov_set(&ctx->update_iod.iod_name, "akey", strlen("akey"));
	ctx->recx[0].rx_idx = 0;
	ctx->recx[0].rx_nr = daos_sgl_buf_size(&ctx->update_sgl);
	ctx->update_iod.iod_size = 1;
	ctx->update_iod.iod_nr	= 1;
	ctx->update_iod.iod_recxs = &ctx->recx[0];
	ctx->update_iod.iod_type  = DAOS_IOD_ARRAY;

	/** Setup Fetch IOD*/
	ctx->fetch_iod.iod_name = ctx->update_iod.iod_name;
	ctx->fetch_iod.iod_size = ctx->update_iod.iod_size;
	ctx->fetch_iod.iod_recxs = ctx->update_iod.iod_recxs;
	ctx->fetch_iod.iod_nr = ctx->update_iod.iod_nr;
	ctx->fetch_iod.iod_type = ctx->update_iod.iod_type;
}

/**
 * Setup the data portion of the test context. Data is the string: "0123456789"
 * repeated 2000 times. It is represented by a single sgl, iod, but multiple
 * recxs.
 *
 * The Fetch iod & sgl are also initialized to be appropriate for fetching the
 * data.
 */
static void
setup_multiple_extent_data(struct csum_test_ctx *ctx)
{
	uint32_t	recx_nr = 2;
	uint32_t	rec_size = 8;
	daos_size_t	records;
	daos_size_t	rec_per_recx;
	int		i;

	d_iov_set(&ctx->dkey, "dkey", strlen("dkey"));
	d_iov_set(&ctx->update_iod.iod_name, "akey_complex",
		strlen("akey_complex"));

	dts_sgl_init_with_strings_repeat(&ctx->update_sgl, 2000, 1,
		"9876543210");
	d_sgl_init(&ctx->fetch_sgl, 1);
	ctx->fetch_sgl.sg_iovs->iov_len =
		ctx->fetch_sgl.sg_iovs->iov_buf_len =
		daos_sgl_buf_size(&ctx->update_sgl);
	D_ALLOC(ctx->fetch_sgl.sg_iovs->iov_buf,
		ctx->fetch_sgl.sg_iovs->iov_len);

	records = daos_sgl_buf_size(&ctx->update_sgl) / rec_size;
	rec_per_recx = records / recx_nr;

	ctx->update_iod.iod_size = rec_size;
	ctx->update_iod.iod_nr	= recx_nr;
	ctx->update_iod.iod_recxs = ctx->recx;
	ctx->update_iod.iod_type  = DAOS_IOD_ARRAY;

	for (i = 0; i < recx_nr; i++) {
		ctx->recx[i].rx_nr = rec_per_recx;
		ctx->recx[i].rx_idx   = i * rec_per_recx;
	}

	/** Setup Fetch IOD*/
	ctx->fetch_iod.iod_name = ctx->update_iod.iod_name;
	ctx->fetch_iod.iod_size = ctx->update_iod.iod_size;
	ctx->fetch_iod.iod_recxs = ctx->update_iod.iod_recxs;
	ctx->fetch_iod.iod_nr = ctx->update_iod.iod_nr;
	ctx->fetch_iod.iod_type = ctx->update_iod.iod_type;
}

static void
cleanup_cont_obj(struct csum_test_ctx *ctx)
{
	int rc;

	/** close object */
	rc = daos_obj_close(ctx->oh, NULL);
	assert_int_equal(rc, 0);

	/** Close & Destroy Container */
	rc = daos_cont_close(ctx->coh, NULL);
	assert_int_equal(rc, 0);
	rc = daos_cont_destroy(ctx->poh, ctx->uuid, true, NULL);
	assert_int_equal(rc, 0);
}

static void
cleanup_data(struct csum_test_ctx *ctx)
{
	d_sgl_fini(&ctx->update_sgl, true);
	d_sgl_fini(&ctx->fetch_sgl, true);
}

static void
checksum_disabled(void **state)
{
	struct csum_test_ctx	 ctx = {0};
	int			 rc;

	/**
	 * Setup
	 */
	setup_from_test_args(&ctx, (test_arg_t *)*state);
	setup_simple_data(&ctx);
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_OFF, false, 0, OC_SX);

	/**
	 * Act
	 */
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, 0);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(rc, 0);
	assert_memory_equal(ctx.update_sgl.sg_iovs->iov_buf,
			    ctx.fetch_sgl.sg_iovs->iov_buf,
			    ctx.update_sgl.sg_iovs->iov_buf_len);

	/**
	 * Clean up
	 */

	cleanup_cont_obj(&ctx);
	cleanup_data(&ctx);

}

static void
io_with_server_side_verify(void **state)
{
	struct csum_test_ctx	 ctx = {0};
	int			 rc;

	FAULT_INJECTION_REQUIRED();

	/**
	 * Setup
	 */
	setup_from_test_args(&ctx, (test_arg_t *)*state);
	setup_simple_data(&ctx);

	/**
	 * Act - testing four use cases
	 * 1. Regular, server verify disabled and no corruption ... obviously
	 *    should be success.
	 * 2. Server verify enabled, and still no corruption. Should be success.
	 *    Corruption under checksum field.
	 * 3. Server verify disabled and there's corruption. Update should
	 *    still be success because the corruption won't be caught until
	 *    it's fetched. Corruption under checksum field.
	 * 4. Server verify enabled and corruption occurs. The update should
	 *    fail because the server will catch the corruption.
	 * 5. Server verify enabled and corruption on data field.(Repeat
	 *    test 3 and 4 with data field corrution)
	 *
	 */
	/** 1. Server verify disabled, no corruption */
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, 0, OC_SX);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, 0);
	cleanup_cont_obj(&ctx);

	/** 2. Server verify enabled, no corruption */
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, true, 0, OC_SX);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, 0);
	cleanup_cont_obj(&ctx);

	/** 3. Server verify disabled, corruption occurs, update should work */
	set_update_csum_fi();
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, 0, OC_SX);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, 0);
	cleanup_cont_obj(&ctx);
	unset_csum_fi();

	/** 4. Server verify enabled, corruption occurs, update should fail */
	set_update_csum_fi();
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, true, 0, OC_SX);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, -DER_CSUM);
	cleanup_cont_obj(&ctx);
	unset_csum_fi();

	/**5. Data corruption. Update should fail due CRC mismatch */
	set_client_data_corrupt_fi();
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, 0, OC_SX);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, 0);
	cleanup_cont_obj(&ctx);

	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, true, 0, OC_SX);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, -DER_CSUM);
	cleanup_cont_obj(&ctx);
	unset_csum_fi();
	cleanup_data(&ctx);
}

static void
test_server_data_corruption(void **state)
{
	test_arg_t	*arg = *state;
	struct csum_test_ctx	 ctx = {0};
	int			 rc;

	FAULT_INJECTION_REQUIRED();

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, 1024*8, OC_SX);

	/**1. Simple server data corruption after RDMA */
	setup_multiple_extent_data(&ctx);
	/** Set the Server data corruption flag */
	rc = daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				DAOS_CHECKSUM_SDATA_CORRUPT | DAOS_FAIL_ALWAYS,
				0, NULL);
	assert_int_equal(rc, 0);
	/** Perform the update */
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			&ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, 0);
	/** Clear the fail injection flag */
	rc = daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	assert_int_equal(rc, 0);
	/** Fetch should result in checksum failure : SSD bad data*/
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(rc, -DER_CSUM);

	cleanup_cont_obj(&ctx);
	cleanup_data(&ctx);

}

static void
test_fetch_array(void **state)
{
	int			rc;
	struct csum_test_ctx	ctx = {0};

	FAULT_INJECTION_REQUIRED();

	/**
	 * Setup
	 */
	setup_from_test_args(&ctx, (test_arg_t *) *state);

	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, 1024*8, OC_SX);
	setup_simple_data(&ctx);

	/**
	 * Act
	 * 1. Test that with checksums enabled, a simple update/fetch works
	 *    as expected. There should be no corruption so the fetch should
	 *    succeed. (Keep Server Side Verify off to not
	 *    complicate it at all)
	 * 2. Enable fault injection on the fetch so data is corrupted. Fault
	 *    should be injected on the client side before the checksum
	 *    verification occurs.
	 * 3. Repeat case 1, but with a more complicated I/O: Larger data,
	 *    multiple extents.
	 * 4. Repeate case 2 but with the more complicated I/O from 3.
	 * 5. Repeate cases 2 and 4, but with replica (2) object class.
	 */

	/** 1. Simple success case */
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, 0);
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(rc, 0);
	/** Update/Fetch data matches */
	assert_memory_equal(ctx.update_sgl.sg_iovs->iov_buf,
			    ctx.fetch_sgl.sg_iovs->iov_buf,
			    ctx.update_sgl.sg_iovs->iov_buf_len);

	/** 2. Detect corruption - fetch again with fault injection enabled */

	set_fetch_csum_fi();
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(rc, -DER_CSUM);
	unset_csum_fi();
	cleanup_data(&ctx);

	/** 3. Complicated data success case */
	setup_multiple_extent_data(&ctx);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, 0);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(rc, 0);
	/** Update/Fetch data matches */
	assert_memory_equal(ctx.update_sgl.sg_iovs->iov_buf,
			    ctx.fetch_sgl.sg_iovs->iov_buf,
			    ctx.update_sgl.sg_iovs->iov_buf_len);

	/** 4. Complicated data with corruption */
	set_fetch_csum_fi();
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(rc, -DER_CSUM);
	unset_csum_fi();

	/** 5. Replicated object with corruption */
	if (test_runable(*state, 2)) {
		cleanup_cont_obj(&ctx);
		setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, 1024*8,
			       OC_RP_2GX);
		rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				     &ctx.update_iod, &ctx.update_sgl, NULL);
		assert_int_equal(rc, 0);
		set_fetch_csum_fi();
		rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
		assert_int_equal(rc, 0);
		/** Update/Fetch data matches */
		assert_memory_equal(ctx.update_sgl.sg_iovs->iov_buf,
				    ctx.fetch_sgl.sg_iovs->iov_buf,
				    ctx.update_sgl.sg_iovs->iov_buf_len);
		unset_csum_fi();
		cleanup_data(&ctx);

		/** 6. Replicated (complicated data) object with corruption */
		set_fetch_csum_fi();
		setup_multiple_extent_data(&ctx);
		rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				     &ctx.update_iod, &ctx.update_sgl, NULL);
		assert_int_equal(rc, 0);

		rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
		assert_int_equal(rc, 0);
		/** Update/Fetch data matches */
		assert_memory_equal(ctx.update_sgl.sg_iovs->iov_buf,
				    ctx.fetch_sgl.sg_iovs->iov_buf,
				    ctx.update_sgl.sg_iovs->iov_buf_len);
	}

	/** Clean up */
	unset_csum_fi();
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

/**
 * -----------------------------------------
 * Partial Fetch & Unaligned Chunk tests
 * -----------------------------------------
 */

/** easily setup an iov and allocate */
void
iov_alloc(d_iov_t *iov, size_t len)
{
	D_ALLOC(iov->iov_buf, len);
	assert_non_null(iov->iov_buf);
	iov->iov_buf_len = iov->iov_len = len;
}

#define	IOV_ALLOC_WITH_STR(iov, str) \
	iov_alloc_with_str(iov, str, strlen(str) + 1)
void
iov_alloc_with_str(d_iov_t *iov, char *str, size_t len)
{
	D_ALLOC(iov->iov_buf, len);
	iov->iov_buf_len = iov->iov_len = len;
	strncpy(iov->iov_buf, str, len);
}

/** For defining an extent and data it represents in a test */
struct recx_config {
	uint64_t idx;
	uint64_t nr;
	char *data;
};
#define RECX_CONFIGS_NR 4
struct partial_unaligned_fetch_testcase_args {
	char			*dkey;
	char			*akey;
	uint32_t		 rec_size;
	uint32_t		 csum_prop_type;
	bool			 server_verify;
	size_t			 chunksize;
	struct recx_config	 recx_cfgs[RECX_CONFIGS_NR];
	daos_recx_t		 fetch_recx;
};

static void
setup_obj_data_for_sv(struct csum_test_ctx *ctx)
{
	d_iov_set(&ctx->dkey, "dkey", strlen("dkey"));

	d_iov_set(&ctx->update_iod.iod_name, "akey", strlen("akey"));

	/** setup the buffers for update & fetch */
	dts_sgl_init_with_strings(&ctx->update_sgl, 1, "ABCDEFGHIJKLMNOP");

	d_sgl_init(&ctx->fetch_sgl, 1);
	iov_alloc(&ctx->fetch_sgl.sg_iovs[0],
		  daos_sgl_buf_size(&ctx->update_sgl));

	/** Setup Update IOD */
	ctx->update_iod.iod_size = daos_sgl_buf_size(&ctx->update_sgl);
	/** These test cases always use 1 recx at a time */
	ctx->update_iod.iod_nr	= 1;
	ctx->update_iod.iod_recxs = NULL;
	ctx->update_iod.iod_type  = DAOS_IOD_SINGLE;

	/** Setup Fetch IOD*/
	ctx->fetch_iod.iod_name = ctx->update_iod.iod_name;
	ctx->fetch_iod.iod_size = ctx->update_iod.iod_size;
	ctx->fetch_iod.iod_nr = ctx->update_iod.iod_nr;
	ctx->fetch_iod.iod_type = ctx->update_iod.iod_type;
	ctx->fetch_iod.iod_recxs = NULL;
}

/** Fill an iov buf with data, using \data (duplicate if necessary)
 */
static void iov_update_fill(d_iov_t *iov, char *data, uint64_t len_to_fill)
{
	iov->iov_len = len_to_fill;
	const size_t data_len = strlen(data); /** don't include '\0' */
	size_t bytes_to_copy = min(data_len, len_to_fill);
	char *dest = iov->iov_buf;

	assert_int_not_equal(0, data_len);
	while (len_to_fill > 0) {
		memcpy(dest, data, bytes_to_copy);
		dest += bytes_to_copy;
		len_to_fill -= bytes_to_copy;
		bytes_to_copy = min(data_len, len_to_fill);
	}
}

#define	ARRAY_UPDATE_FETCH_TESTCASE(state, ...) \
	array_update_fetch_testcase(__FILE__, __LINE__, \
		(test_arg_t *) (*state), \
		&(struct partial_unaligned_fetch_testcase_args)__VA_ARGS__)

/**
 * For Array Types
 * Using the provided configuration (\args) update a number of extents,
 * then fetch all or a subset of those extents (as defined in \args). Only
 * checking that the update and fetch succeeded. With checksums enabled, it
 * verifies that the logic when the server must calculate new checksums for
 * unaligned chunk data.
 */
static void
array_update_fetch_testcase(char *file, int line, test_arg_t *test_arg,
			    struct partial_unaligned_fetch_testcase_args *args)
{
	struct csum_test_ctx	ctx = {0};
	uint32_t		rec_size = args->rec_size;
	int			recx_count = 0;
	size_t			max_data_size = 0;
	int			rc;
	int			i;

	if (args->dkey != NULL)
		d_iov_set(&ctx.dkey, args->dkey, strlen(args->dkey));
	else
		d_iov_set(&ctx.dkey, "dkey", strlen("dkey"));

	if (args->akey != NULL)
		d_iov_set(&ctx.update_iod.iod_name, args->akey,
			  strlen(args->akey));
	else
		d_iov_set(&ctx.update_iod.iod_name, "akey", strlen("akey"));

	while (recx_count < RECX_CONFIGS_NR &&
	       args->recx_cfgs[recx_count].nr > 0) {
		max_data_size = max(args->recx_cfgs[recx_count].nr * rec_size,
				    max_data_size);
		recx_count++;
	}

	/** setup the buffers for update & fetch */
	d_sgl_init(&ctx.update_sgl, 1);
	iov_alloc(&ctx.update_sgl.sg_iovs[0], max_data_size);

	d_sgl_init(&ctx.fetch_sgl, 1);
	iov_alloc(&ctx.fetch_sgl.sg_iovs[0], args->fetch_recx.rx_nr * rec_size);

	/** Setup Update IOD */
	ctx.update_iod.iod_size = rec_size;
	/** These thest cases always use 1 recx at a time */
	ctx.update_iod.iod_nr	= 1;
	ctx.update_iod.iod_recxs = ctx.recx;
	ctx.update_iod.iod_type  = DAOS_IOD_ARRAY;

	/** Setup Fetch IOD*/
	ctx.fetch_iod.iod_name = ctx.update_iod.iod_name;
	ctx.fetch_iod.iod_size = ctx.update_iod.iod_size;
	ctx.fetch_iod.iod_recxs = &args->fetch_recx;
	ctx.fetch_iod.iod_nr = ctx.update_iod.iod_nr;
	ctx.fetch_iod.iod_type = ctx.update_iod.iod_type;

	setup_from_test_args(&ctx, test_arg);
	setup_cont_obj(&ctx, args->csum_prop_type, args->server_verify,
		       args->chunksize, OC_SX);

	for (i = 0; i < recx_count; i++) {
		ctx.recx[0].rx_nr = args->recx_cfgs[i].nr;
		ctx.recx[0].rx_idx = args->recx_cfgs[i].idx;
		iov_update_fill(&ctx.update_sgl.sg_iovs[0],
				args->recx_cfgs[i].data,
				args->recx_cfgs[i].nr * rec_size);
		rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				     &ctx.update_iod, &ctx.update_sgl,
				     NULL);
		if (rc != 0) {
			fail_msg("%s:%d daos_obj_update failed with %d",
				 file, line, rc);
		}
	}

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	if (rc != 0) {
		fail_msg("%s:%d daos_obj_fetch failed with %d",
			 file, line, rc);
	}

	/** Clean up */
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

static void
fetch_with_multiple_extents(void **state)
{
	/** Fetching a subset of original extent (not chunk aligned) */
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 8,
		.csum_prop_type = DAOS_PROP_CO_CSUM_CRC64,
		.server_verify = false,
		.rec_size = 8,
		.recx_cfgs = {
			{.idx = 0, .nr = 1024, .data = "A"},
		},
		.fetch_recx = {.rx_idx = 2, .rx_nr = 8},
	});

	/** Extents not aligned with chunksize */
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 2,
		.csum_prop_type = DAOS_PROP_CO_CSUM_CRC64,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 0, .nr = 3, .data = "ABC"},
			{.idx = 1, .nr = 2, .data = "B"},
		},
		.fetch_recx = {.rx_idx = 0, .rx_nr = 3},
	});

	/** Heavily overlapping extents broken up into many chunks */
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 8,
		.csum_prop_type = DAOS_PROP_CO_CSUM_CRC32,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 2, .nr = 510, .data = "ABCDEFG"},
			{.idx = 0, .nr = 512, .data = "1234567890"},
		},
		.fetch_recx = {.rx_idx = 0, .rx_nr = 511},
	});

	/** Extents with small overlap */
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 1024,
		.csum_prop_type = DAOS_PROP_CO_CSUM_CRC16,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 2, .nr = 512, .data = "A"},
			{.idx = 500, .nr = 512, .data = "B"},
		},
		.fetch_recx = {.rx_idx = 2, .rx_nr = 1012},
	});

	/** several smallish extents within a single chunk */
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 1024 * 32,
		.csum_prop_type = DAOS_PROP_CO_CSUM_CRC64,
		.server_verify = false,
		.rec_size = 8,
		.recx_cfgs = {
			{.idx = 2, .nr = 512, .data = "A"},
			{.idx = 500, .nr = 512, .data = "B"},
			{.idx = 1000, .nr = 512, .data = "C"},
			{.idx = 1500, .nr = 512, .data = "D"},
		},
		.fetch_recx = {.rx_idx = 2, .rx_nr = 800},
	});

	/** Extents with holes */
	/** TODO: Holes not supported yet */
#if 0
ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 1024 * 32,
		.csum_prop_type = DAOS_PROP_CO_CSUM_CRC64,
		.server_verify = false,
		.rec_size = 8,
		.recx_cfgs = {
			{.idx = 0, .nr = 8, .data = "Y"},
			{.idx = 10, .nr = 8, .data = "Z"},
		},
		.fetch_recx = {.rx_idx = 0, .rx_nr = 18},
	});
#endif
}

static void
single_value(void **state)
{
	struct csum_test_ctx	ctx = {0};
	int			rc;

	FAULT_INJECTION_REQUIRED();

	setup_from_test_args(&ctx, *state);

	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, 4, OC_SX);
	setup_obj_data_for_sv(&ctx);

	/** Base case ... no fault injection */
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_int_equal(0, rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(0, rc);

	/**
	 * fault injection on update
	 * - update will succeed because server side verification is disabled
	 * - fetch will fail because data was corrupted on update
	 */
	set_update_csum_fi();
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_int_equal(0, rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(-DER_CSUM, rc);
	unset_csum_fi();

	/**
	 * fault injection on fetch
	 * - update will succeed
	 * - fetch will fail because data was corrupted on fetch
	 */
	set_fetch_csum_fi();
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_int_equal(0, rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(-DER_CSUM, rc);
	unset_csum_fi();

	/** Reset the container with server side verification enabled */
	cleanup_cont_obj(&ctx);
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, true, 4, OC_SX);

	/**
	 * fault injection on update
	 * - update will fail because server side verification is enabled
	 * - fetch will not get data
	 */
	set_update_csum_fi();
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_int_equal(-DER_CSUM, rc);

	memset(ctx.fetch_sgl.sg_iovs->iov_buf, 0,
	       ctx.fetch_sgl.sg_iovs->iov_buf_len);
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(0, rc);
	assert_string_equal("", (char *)ctx.fetch_sgl.sg_iovs->iov_buf);

	unset_csum_fi();

	/** Clean up */
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

static void
mix_test(void **state)
{
	struct csum_test_ctx	ctx = {0};
	int			rc;
	daos_key_t		 dkey;
	daos_iod_t		 iods[2] = {0};
	daos_iod_t		*sv_iod = &iods[0];
	daos_iod_t		*array_iod = &iods[1];
	daos_recx_t		 recxs[2] = {0};
	d_sg_list_t		 sgls[2] = {0};
	d_sg_list_t		*sv_sgl = &sgls[0];
	d_sg_list_t		*array_sgl = &sgls[1];
	d_sg_list_t		 fetch_sgls[2] = {0};
	d_sg_list_t		*fetch_sv_sgl = &fetch_sgls[0];
	d_sg_list_t		*fetch_array_sgl = &fetch_sgls[1];

	setup_from_test_args(&ctx, *state);

	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, 4, OC_SX);

	IOV_ALLOC_WITH_STR(&dkey, "dkey");

	/** setup single value iod */
	dts_sgl_init_with_strings(sv_sgl, 1,
		"This is for a single value");
	dts_sgl_init_with_strings(fetch_sv_sgl, 1,
		"XXXXXXXXXXXXXXXXXXXXXXXXXX");
	IOV_ALLOC_WITH_STR(&sv_iod->iod_name, "single value akey");
	sv_iod->iod_type = DAOS_IOD_SINGLE;
	sv_iod->iod_size = daos_sgl_buf_size(sv_sgl);
	sv_iod->iod_nr = 1;

	/** setup array value iod */
	dts_sgl_init_with_strings(array_sgl, 1,
		"This is for an array value");
	dts_sgl_init_with_strings(fetch_array_sgl, 1,
		"XXXXXXXXXXXXXXXXXXXXXXXXXX");
	IOV_ALLOC_WITH_STR(&array_iod->iod_name, "array value akey");
	array_iod->iod_type = DAOS_IOD_ARRAY;
	array_iod->iod_nr = 2; /** split up into two recxs */
	array_iod->iod_size = 1;
	array_iod->iod_recxs = recxs;
	recxs[0].rx_idx = 0;
	recxs[0].rx_nr = 10;
	recxs[1].rx_idx = 10;
	recxs[1].rx_nr = daos_sgl_buf_size(array_sgl) - 10;

	/** Base case ... no fault injection */
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			     iods, sgls, NULL);
	assert_int_equal(0, rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			    iods, fetch_sgls, NULL, NULL);
	assert_int_equal(0, rc);

	/**
	 * fault injection on update
	 * - update will succeed because server side verification is disabled
	 * - fetch will fail because data was corrupted on update
	 */
	set_update_csum_fi();
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			     iods, sgls, NULL);
	assert_int_equal(0, rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			    iods, fetch_sgls, NULL, NULL);
	assert_int_equal(-DER_CSUM, rc);
	unset_csum_fi();

	/**
	 * fault injection on fetch
	 * - update will succeed
	 * - fetch will fail because data was corrupted on fetch
	 */
	set_fetch_csum_fi();
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			     iods, sgls, NULL);
	assert_int_equal(0, rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			    iods, fetch_sgls, NULL, NULL);
	assert_int_equal(-DER_CSUM, rc);
	unset_csum_fi();

	/** Reset the container with server side verification enabled */
	cleanup_cont_obj(&ctx);
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, true, 4, OC_SX);

	/**
	 * fault injection on update
	 * - update will fail because server side verification is enabled
	 * - fetch will not get data
	 */
	set_update_csum_fi();
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			     iods, sgls, NULL);
	assert_int_equal(-DER_CSUM, rc);

	memset(fetch_sv_sgl->sg_iovs->iov_buf, 0,
	       fetch_sv_sgl->sg_iovs->iov_buf_len);
	memset(fetch_array_sgl->sg_iovs->iov_buf, 0,
	       fetch_array_sgl->sg_iovs->iov_buf_len);
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			    iods, fetch_sgls, NULL, NULL);
	assert_int_equal(0, rc);
	assert_string_equal("", (char *)fetch_sv_sgl->sg_iovs->iov_buf);
	assert_string_equal("", (char *)fetch_array_sgl->sg_iovs->iov_buf);

	unset_csum_fi();

	/** Clean up */
	d_sgl_fini(sv_sgl, true);
	d_sgl_fini(array_sgl, true);
	d_sgl_fini(fetch_sv_sgl, true);
	d_sgl_fini(fetch_array_sgl, true);
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			  NULL);
}

static const struct CMUnitTest tests[] = {
	{ "DAOS_CSUM00: csum disabled",
		checksum_disabled, async_disable, test_case_teardown},
	{ "DAOS_CSUM01: simple update with server side verify",
		io_with_server_side_verify, async_disable, test_case_teardown},
	{ "DAOS_CSUM02: Fetch Array Type",
		test_fetch_array, async_disable, test_case_teardown},
	{ "DAOS_CSUM03: Setup multiple overlapping/unaligned extents",
		fetch_with_multiple_extents, async_disable, test_case_teardown},
	{ "DAOS_CSUM04: Server data corrupted after RDMA",
		test_server_data_corruption, async_disable, test_case_teardown},
	{ "DAOS_CSUM05: Single Value Checksum",
		single_value, async_disable, test_case_teardown},
	{ "DAOS_CSUM06: Mix of Single Value and Array values iods",
		mix_test, async_disable, test_case_teardown},
};

int
run_daos_checksum_test(int rank, int size)
{
	int rc = 0;

	if (rank == 0)
		rc = cmocka_run_group_tests_name("DAOS Checksum Tests",
			tests, setup, test_teardown);

	MPI_Barrier(MPI_COMM_WORLD);
	return rc;

}
