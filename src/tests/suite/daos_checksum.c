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

struct csum_test_context {
	daos_handle_t		poh;
	daos_handle_t		coh;
	daos_cont_info_t	info;
	daos_obj_id_t		oid;
	uuid_t			uuid;
	daos_key_t		dkey;
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	daos_recx_t		recx;
	daos_handle_t		oh;
};

static void
setup_test_context(struct csum_test_context *ctx, daos_prop_t *props)
{
	int rc;

	uuid_generate(ctx->uuid);
	rc = daos_cont_create(ctx->poh, ctx->uuid, props, NULL);
	assert_int_equal(0, rc);

	rc = daos_cont_open(ctx->poh, ctx->uuid, DAOS_COO_RW,
		&ctx->coh, &ctx->info, NULL);
	assert_int_equal(0, rc);

	ctx->oid = dts_oid_gen(OC_SX, 0, 1);
	rc = daos_obj_open(ctx->coh, ctx->oid, 0, &ctx->oh, NULL);
	assert_int_equal(0, rc);

	daos_sgl_init_with_strings(&ctx->sgl, 1, "0123456789");
	d_iov_set(&ctx->dkey, "dkey", strlen("dkey"));
	ctx->recx.rx_idx   = 0;
	ctx->recx.rx_nr    = daos_sgl_buf_size(&ctx->sgl);
	d_iov_set(&ctx->iod.iod_name, "akey", strlen("akey"));
	ctx->iod.iod_nr	= 1;
	ctx->iod.iod_size	= 1;
	ctx->iod.iod_recxs = &ctx->recx;
	ctx->iod.iod_eprs  = NULL;
	ctx->iod.iod_type  = DAOS_IOD_ARRAY;
}

static void
cleanup_test(struct csum_test_context *ctx)
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

	d_sgl_fini(&ctx->sgl, true);
}

static void
io_with_server_side_verify(void **state)
{
	struct csum_test_context	 ctx = {0};
	daos_prop_t			*props;
	int				 rc;

	/**
	 * Setup
	 */
	ctx.poh = ((test_arg_t *)*state)->pool.poh;

	/** Container props for checksum (default chunksize is good) */
	props = daos_prop_alloc(2);
	assert_non_null(props);
	props->dpp_entries[0].dpe_type = DAOS_PROP_CO_CSUM;
	props->dpp_entries[0].dpe_val = DAOS_PROP_CO_CSUM_CRC64;
	props->dpp_entries[1].dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	props->dpp_entries[1].dpe_val = DAOS_PROP_CO_CSUM_SV_OFF;

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
	setup_test_context(&ctx, props);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
		&ctx.iod, &ctx.sgl, NULL);
	assert_int_equal(rc, 0);
	cleanup_test(&ctx);
/* [todo-ryon]:  */
#if 0
	/** 2. Server verify enabled, no corruption */
	props->dpp_entries[1].dpe_val = DAOS_PROP_CO_CSUM_SV_ON;
	setup_test_context(&ctx, props);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
			     &ctx.iod, &ctx.sgl, NULL);
	assert_int_equal(rc, 0);
	cleanup_test(&ctx);

	/** 3. Server verify disabled, corruption occurs, update should work */
	props->dpp_entries[1].dpe_val = DAOS_PROP_CO_CSUM_SV_OFF;
	daos_fail_loc_set(DAOS_CHECKSUM_UPDATE_FAIL | DAOS_FAIL_ALWAYS);
	setup_test_context(&ctx, props);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
			     &ctx.iod, &ctx.sgl, NULL);
	assert_int_equal(rc, 0);
	cleanup_test(&ctx);

	/** 4. Server verify enabled, corruption occurs, update should fail */
	props->dpp_entries[1].dpe_val = DAOS_PROP_CO_CSUM_SV_ON;
	setup_test_context(&ctx, props);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, &ctx.dkey, 1,
			     &ctx.iod, &ctx.sgl, NULL);
	assert_int_equal(rc, -DER_IO);
	cleanup_test(&ctx);

	/**
	 * Clean Up
	 */
	daos_prop_free(props);
#endif
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
