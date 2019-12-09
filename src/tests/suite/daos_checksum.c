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

#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"

#include <daos/checksum.h>
#include <gurt/types.h>
#include <daos_prop.h>

/** fault injection helpers */
static uint64_t g_prev_fi_loc;
static void
set_update_csum_fi()
{
	g_prev_fi_loc = daos_fail_value_get();
	daos_fail_loc_set(DAOS_CHECKSUM_UPDATE_FAIL | DAOS_FAIL_ALWAYS);
}
static void
set_fetch_csum_fi()
{
	g_prev_fi_loc = daos_fail_value_get();
	daos_fail_loc_set(DAOS_CHECKSUM_FETCH_FAIL | DAOS_FAIL_ALWAYS);
}
static void
unset_csum_fi()
{
	daos_fail_loc_set(g_prev_fi_loc); /** turn off fault injection */
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
setup_cont_obj(struct csum_test_ctx *ctx, int csum_prop_type,
	       bool csum_sv)
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
	props->dpp_entries[2].dpe_val = 1024*16;

	rc = daos_cont_create(ctx->poh, ctx->uuid, props, NULL);
	daos_prop_free(props);
	assert_int_equal(0, rc);

	rc = daos_cont_open(ctx->poh, ctx->uuid, DAOS_COO_RW,
			    &ctx->coh, &ctx->info, NULL);
	assert_int_equal(0, rc);

	ctx->oid = dts_oid_gen(OC_SX, 0, 1);
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
	ctx->update_iod.iod_eprs  = NULL;
	ctx->update_iod.iod_type  = DAOS_IOD_ARRAY;
	ctx->update_iod.iod_csums = NULL;

	/** Setup Fetch IOD*/
	ctx->fetch_iod.iod_name = ctx->update_iod.iod_name;
	ctx->fetch_iod.iod_size = ctx->update_iod.iod_size;
	ctx->fetch_iod.iod_recxs = ctx->update_iod.iod_recxs;
	ctx->fetch_iod.iod_nr = ctx->update_iod.iod_nr;
	ctx->fetch_iod.iod_type = ctx->update_iod.iod_type;
	ctx->fetch_iod.iod_csums = NULL;
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
	ctx->update_iod.iod_eprs  = NULL;
	ctx->update_iod.iod_csums = NULL;
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
	ctx->fetch_iod.iod_csums = NULL;
	ctx->fetch_iod.iod_eprs = NULL;
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
io_with_server_side_verify(void **state)
{
	struct csum_test_ctx	 ctx = {0};
	int			 rc;

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
	 * 3. Server verify disabled and there's corruption. Update should
	 *    still be success because the corruption won't be caught until
	 *    it's fetched.
	 * 4. Server verify enabled and corruption occurs. The update should
	 *    fail because the server will catch the corruption.
	 *
	 */
	/** 1. Server verify disabled, no corruption */
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, 0);
	cleanup_cont_obj(&ctx);

	/** 2. Server verify enabled, no corruption */
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, true);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, 0);
	cleanup_cont_obj(&ctx);

	/** 3. Server verify disabled, corruption occurs, update should work */
	set_update_csum_fi();
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, 0);
	cleanup_cont_obj(&ctx);

	/** 4. Server verify enabled, corruption occurs, update should fail */
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, true);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, -DER_IO);
	cleanup_cont_obj(&ctx);

	unset_csum_fi();

	cleanup_data(&ctx);
}

static void
test_fetch_array(void **state)
{
	int			rc;
	struct csum_test_ctx	ctx = {0};

	/**
	 * Setup
	 */

	setup_from_test_args(&ctx, (test_arg_t *) *state);

	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false);
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
	 */

	/** 1. Simple success case */
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, 0);
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(rc, 0);
	/** Update/Fetch data matches */
	assert_memory_equal(ctx.update_sgl.sg_iovs->iov_buf,
			    ctx.fetch_sgl.sg_iovs->iov_buf,
			    ctx.update_sgl.sg_iovs->iov_buf_len);

	/** 2. Detect corruption - fetch again with fault injection enabled */

	set_fetch_csum_fi();
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(rc, -DER_IO);
	unset_csum_fi();

	/** 3. Complicated data success case */
	cleanup_data(&ctx);
	setup_multiple_extent_data(&ctx);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_int_equal(rc, 0);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(rc, 0);
	/** Update/Fetch data matches */
	assert_memory_equal(ctx.update_sgl.sg_iovs->iov_buf,
			    ctx.fetch_sgl.sg_iovs->iov_buf,
			    ctx.update_sgl.sg_iovs->iov_buf_len);

	/** 4. Complicated data with corruption */
	set_fetch_csum_fi();
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_int_equal(rc, -DER_IO);
	unset_csum_fi();

	/** Clean up */
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
	{ "DAOS_CSUM01: simple update with server side verify",
		io_with_server_side_verify, async_disable, test_case_teardown},
	{ "DAOS_CSUM02: Fetch Array Type",
		test_fetch_array, async_disable, test_case_teardown},
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
