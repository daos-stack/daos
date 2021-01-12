/**
 * (C) Copyright 2020 Intel Corporation.
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

struct dedup_test_ctx {
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

enum THRESHOLD_SETTING {
	THRESHOLD_GREATER_THAN_DATA = 1,
	THRESHOLD_LESS_THAN_DATA,
};

static bool
dedup_is_nvme_enabled(test_arg_t *arg)
{
	daos_pool_info_t	 pinfo = { 0 };
	struct daos_pool_space	*ps = &pinfo.pi_space;
	int			 rc;

	pinfo.pi_bits = DPI_ALL;
	rc = test_pool_get_info(arg, &pinfo);
	assert_int_equal(rc, 0);

	return ps->ps_free_min[DAOS_MEDIA_NVME] != 0;
}

/** easily setup an iov and allocate */
static void
iov_alloc(d_iov_t *iov, size_t len)
{
	D_ALLOC(iov->iov_buf, len);
	iov->iov_buf_len = iov->iov_len = len;
}

static void
iov_alloc_str(d_iov_t *iov, const char *str)
{
	iov_alloc(iov, strlen(str) + 1);
	strcpy(iov->iov_buf, str);
}

static void
setup_from_test_args(struct dedup_test_ctx *ctx, test_arg_t *state)
{
	ctx->poh = state->pool.poh;
}

static void
setup_sgl(struct dedup_test_ctx *ctx)
{
	dts_sgl_init_with_strings_repeat(&ctx->update_sgl, 1000, 1,
					 "Lorem ipsum dolor sit "
					 "amet, consectetur adipiscing elit,"
					 " sed do eiusmod tempor incididunt ut"
					 " labore et dolore magna aliqua.");

	d_sgl_init(&ctx->fetch_sgl, 1);
	iov_alloc(&ctx->fetch_sgl.sg_iovs[0],
		daos_sgl_buf_size(&ctx->update_sgl));
}

static void
setup_keys(struct dedup_test_ctx *ctx)
{
	iov_alloc_str(&ctx->dkey, "dkey");
	iov_alloc_str(&ctx->update_iod.iod_name, "akey");
}

static void
setup_as_array(struct dedup_test_ctx *ctx)
{
	ctx->recx[0].rx_idx = 0;
	ctx->recx[0].rx_nr = daos_sgl_buf_size(&ctx->update_sgl);
	ctx->update_iod.iod_size = 1;
	ctx->update_iod.iod_nr	= 1;
	ctx->update_iod.iod_recxs = &ctx->recx[0];
	ctx->update_iod.iod_type  = DAOS_IOD_ARRAY;
}

static void
setup_as_single_value(struct dedup_test_ctx *ctx)
{
	ctx->update_iod.iod_nr	= 1;
	ctx->update_iod.iod_size = daos_sgl_buf_size(&ctx->update_sgl);
	ctx->update_iod.iod_recxs = NULL;
	ctx->update_iod.iod_type  = DAOS_IOD_SINGLE;
}

static void
setup_fetch_iod(struct dedup_test_ctx *ctx)
{
	ctx->fetch_iod.iod_name = ctx->update_iod.iod_name;
	ctx->fetch_iod.iod_size = ctx->update_iod.iod_size;
	ctx->fetch_iod.iod_recxs = ctx->update_iod.iod_recxs;
	ctx->fetch_iod.iod_nr = ctx->update_iod.iod_nr;
	ctx->fetch_iod.iod_type = ctx->update_iod.iod_type;
}

static void
setup_cont_obj(struct dedup_test_ctx *ctx,
	       uint32_t csum_prop_type,
	       daos_oclass_id_t oclass,
	       uint32_t dedup_type,
	       uint32_t dedup_threshold_setting)
{
	daos_prop_t *props = NULL;
	int		 rc;
	daos_size_t	 data_len;
	daos_size_t	 dedup_threshold;


	/** calc threshold based on data size and setting */
	data_len = daos_sgl_buf_size(&ctx->update_sgl);
	dedup_threshold = dedup_threshold_setting == THRESHOLD_GREATER_THAN_DATA
			  ? data_len + 10 : data_len - 10;

	uuid_generate(ctx->uuid);

	props = daos_prop_alloc(3);
	assert_non_null(props);
	props->dpp_entries[0].dpe_type = DAOS_PROP_CO_CSUM;
	props->dpp_entries[0].dpe_val = csum_prop_type;
	props->dpp_entries[1].dpe_type = DAOS_PROP_CO_DEDUP;
	props->dpp_entries[1].dpe_val = dedup_type;
	props->dpp_entries[2].dpe_type = DAOS_PROP_CO_DEDUP_THRESHOLD;
	props->dpp_entries[2].dpe_val = dedup_threshold;

	rc = daos_cont_create(ctx->poh, ctx->uuid, props, NULL);
	assert_int_equal(0, rc);
	daos_prop_free(props);

	rc = daos_cont_open(ctx->poh, ctx->uuid, DAOS_COO_RW,
			    &ctx->coh, &ctx->info, NULL);
	assert_int_equal(0, rc);

	ctx->oid = dts_oid_gen(oclass, 0, 1);
	rc = daos_obj_open(ctx->coh, ctx->oid, 0, &ctx->oh, NULL);
	assert_int_equal(0, rc);
}

static void
setup_context(struct dedup_test_ctx *ctx, test_arg_t *state,
	      uint32_t iod_type,
	      uint32_t csum_prop_type,
	      daos_oclass_id_t oclass,
	      uint32_t dedup_type,
	      enum THRESHOLD_SETTING dedup_threshold_setting)
{
	setup_from_test_args(ctx, state);
	setup_keys(ctx);
	setup_sgl(ctx);

	if (iod_type == DAOS_IOD_ARRAY)
		setup_as_array(ctx);
	else if (iod_type == DAOS_IOD_SINGLE)
		setup_as_single_value(ctx);
	else
		fail_msg("Invalid iod_type: %d\n", iod_type);

	setup_fetch_iod(ctx);

	setup_cont_obj(ctx, csum_prop_type, oclass,
		       dedup_type, dedup_threshold_setting);
}

static daos_size_t
get_size(struct dedup_test_ctx *ctx)
{
	daos_pool_info_t	info;
	int			rc;

	info.pi_bits = DPI_SPACE;
	rc = daos_pool_query((*ctx).poh, NULL, &info, NULL, NULL);
	assert_success(rc);
	return info.pi_space.ps_space.s_free[0];
}

static int ctx_update(struct dedup_test_ctx *ctx)
{
	return daos_obj_update(ctx->oh, DAOS_TX_NONE, 0,
			       &ctx->dkey,
			       1, &ctx->update_iod, &ctx->update_sgl,
			       NULL);
}

static void
with_identical_updates(void *const *state, uint32_t iod_type, int csum_type,
		       daos_oclass_id_t oc, int dedup_type,
		       enum THRESHOLD_SETTING dedup_threshold_setting)
{
	struct dedup_test_ctx	ctx;
	/** acceptable size increase when dedup identifies identical data
	 * being inserted
	 */
	const daos_size_t	dedup_size_increase = 256;
	daos_size_t		after_first_update;
	daos_size_t		after_second_update;
	daos_size_t		delta;
	int			rc;

	if (dedup_is_nvme_enabled(*state)) {
		print_message("Currently dedup doesn't support NVMe.\n");
		skip();
	}

	setup_context(&ctx, *state, iod_type, csum_type, oc, dedup_type,
		      dedup_threshold_setting);

	rc = ctx_update(&ctx);
	assert_success(rc);
	after_first_update = get_size(&ctx);

	/** update again with same data */
	rc = ctx_update(&ctx);
	assert_success(rc);

	/** if threshold is less than data size, dedup should prevent the extra
	 * update and therefore the data used from the pool is much less.
	 * Otherwise, the data used from the pool will be larger.
	 */
	after_second_update = get_size(&ctx);
	delta = after_first_update - after_second_update;
	if (dedup_threshold_setting == THRESHOLD_LESS_THAN_DATA &&
	    delta > dedup_size_increase)
		fail_msg("Pool used size increased by %lu, which is larger "
			 "than expected size increase of less than or equal "
			 "to %lu", delta, dedup_size_increase);
	else if (dedup_threshold_setting == THRESHOLD_GREATER_THAN_DATA &&
	    delta < dedup_size_increase)
		fail_msg("Pool used size increased by %lu, which is less "
			 "than expected size increase of greater than or equal "
			 "to %lu", delta, dedup_size_increase);
}

static void
array_csumoff_deduphash(void **state)
{
	with_identical_updates(state,
			       DAOS_IOD_ARRAY,
			       DAOS_PROP_CO_CSUM_OFF,
			       OC_SX,
			       DAOS_PROP_CO_DEDUP_HASH,
			       THRESHOLD_LESS_THAN_DATA);
}
static void
array_csumoff_dedupmemcmp(void **state)
{
	with_identical_updates(state,
			       DAOS_IOD_ARRAY,
			       DAOS_PROP_CO_CSUM_OFF,
			       OC_SX,
			       DAOS_PROP_CO_DEDUP_MEMCMP,
			       THRESHOLD_LESS_THAN_DATA);
}

static void
array_csumcrc64_deduphash(void **state)
{
	with_identical_updates(state,
			       DAOS_IOD_ARRAY,
			       DAOS_PROP_CO_CSUM_CRC64,
			       OC_SX,
			       DAOS_PROP_CO_DEDUP_HASH,
			       THRESHOLD_LESS_THAN_DATA);
}

static void
array_csumcrc64_dedupmemcmp(void **state)
{
	with_identical_updates(state,
			       DAOS_IOD_ARRAY,
			       DAOS_PROP_CO_CSUM_CRC64,
			       OC_SX,
			       DAOS_PROP_CO_DEDUP_MEMCMP,
			       THRESHOLD_LESS_THAN_DATA);
}

static void
array_above_threshold(void **state)
{
	with_identical_updates(state,
			       DAOS_IOD_ARRAY,
			       DAOS_PROP_CO_CSUM_CRC64,
			       OC_SX,
			       DAOS_PROP_CO_DEDUP_MEMCMP,
			       THRESHOLD_GREATER_THAN_DATA);
}

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			  0, NULL);
}

#define DEDUP_TEST(dsc, test) { dsc, test, NULL, test_case_teardown }
static const struct CMUnitTest dedup_tests[] = {
	DEDUP_TEST("DAOS_DEDUP01: With array type, csums disabled, hash dedup",
		array_csumoff_deduphash),
	DEDUP_TEST("DAOS_DEDUP02: With array type, csums disabled, hash memcmp",
		   array_csumoff_dedupmemcmp),
	DEDUP_TEST("DAOS_DEDUP03: With array type, csums crc64, hash dedup",
		   array_csumcrc64_deduphash),
	DEDUP_TEST("DAOS_DEDUP04: With array type, csums crc64, hash memcmp",
		   array_csumcrc64_dedupmemcmp),
	DEDUP_TEST("DAOS_DEDUP05: With array type, threshold greater than data "
		   "should still update",
		   array_above_threshold),
};

int
run_daos_dedup_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	if (rank == 0) {
		if (sub_tests_size == 0) {
			rc = cmocka_run_group_tests_name("DAOS Checksum Tests",
							 dedup_tests, setup,
							 test_teardown);
		} else {
			rc = run_daos_sub_tests("DAOS Checksum Tests",
						dedup_tests,
						ARRAY_SIZE(dedup_tests),
						sub_tests,
						sub_tests_size, setup,
						test_teardown);
		}
	}

	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
