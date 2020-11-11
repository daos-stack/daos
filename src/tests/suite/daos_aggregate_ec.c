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
/**
 * This file is for simple tests of EC aggregation. These tests verify the
 * consistency of the EC data following the completion of the aggregation
 * step for the data written in each test.
 *
 * tests/suite/daos_aggegate_ec.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include <isa-l.h>
#include "daos_test.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>
#include <daos/event.h>
#include "../../object/obj_ec.h"

enum {
	EC_FULL_STRIPE,
	EC_FULL_CELL,
	EC_SPECIFIED,
};

#define assert_success(r) do {\
	int __rc = (r); \
	if (__rc != 0) \
		fail_msg("Not successful!! Error code: " DF_RC, DP_RC(__rc)); \
	} while (0)


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

/** daos EC aggregation test context */
struct ec_agg_test_ctx {
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
	daos_iom_t		fetch_iom;
	daos_iod_t		fetch_iod;
	d_sg_list_t             fetch_sgl;
	daos_recx_t             recx[8];
	daos_recx_t             iom_recx;
};

static void
iov_update_fill(d_iov_t *iov)
{
	char	*dest = iov->iov_buf;
	int	 i;

	for (i = 0; i < iov->iov_len; i++)
		dest[i] = i % 128;
}

static void
ec_setup_from_test_args(struct ec_agg_test_ctx *ctx, test_arg_t *state)
{
	ctx->poh = state->pool.poh;
}

/**
 * Setup the container & object portion of the test context. Uses the csum
 * params to create appropriate container properties.
 */
static void
ec_setup_cont_obj(struct ec_agg_test_ctx *ctx, daos_oclass_id_t oclass)
{
	int		 rc;

	uuid_generate(ctx->uuid);

	rc = daos_cont_create(ctx->poh, ctx->uuid, NULL, NULL);
	assert_success(rc);

	rc = daos_cont_open(ctx->poh, ctx->uuid, DAOS_COO_RW,
			    &ctx->coh, &ctx->info, NULL);
	assert_success(rc);

	ctx->oid.lo = 1;
	ctx->oid.hi =  100;
	daos_obj_generate_id(&ctx->oid, 0, oclass, 0);
	rc = daos_obj_open(ctx->coh, ctx->oid, 0, &ctx->oh, NULL);
	assert_success(rc);
}

static void
ec_setup_single_recx_data(struct ec_agg_test_ctx *ctx, unsigned int mode,
			  daos_size_t offset, daos_size_t data_bytes,
			  unsigned char switch_akey)
{
	if (mode != EC_SPECIFIED)
		return;
	/* else set databytes based on oclass */

	iov_alloc_str(&ctx->dkey, "dkey");
	if (switch_akey == 1)
		iov_alloc_str(&ctx->update_iod.iod_name, "bkey");
	else if (switch_akey == 2)
		iov_alloc_str(&ctx->update_iod.iod_name, "ckey");
	else
		iov_alloc_str(&ctx->update_iod.iod_name, "akey");

	daos_sgl_init(&ctx->update_sgl, 1);
	iov_alloc(&ctx->update_sgl.sg_iovs[0], data_bytes);
	iov_update_fill(ctx->update_sgl.sg_iovs);

	daos_sgl_init(&ctx->fetch_sgl, 1);
	iov_alloc(&ctx->fetch_sgl.sg_iovs[0], data_bytes);

	ctx->recx[0].rx_idx = offset;
	ctx->recx[0].rx_nr = daos_sgl_buf_size(&ctx->update_sgl);
	ctx->update_iod.iod_size = 1;
	ctx->update_iod.iod_nr	= 1;
	ctx->update_iod.iod_recxs = &ctx->recx[0];
	ctx->update_iod.iod_type  = DAOS_IOD_ARRAY;

	ctx->iom_recx.rx_idx = offset;
	ctx->iom_recx.rx_nr = data_bytes;

	ctx->fetch_iom.iom_recxs = &ctx->iom_recx;
	ctx->fetch_iom.iom_nr = 1;
	ctx->fetch_iom.iom_nr_out = 0;

	/** Setup Fetch IOD*/
	ctx->fetch_iod.iod_name = ctx->update_iod.iod_name;
	ctx->fetch_iod.iod_size = ctx->update_iod.iod_size;
	ctx->fetch_iod.iod_recxs = ctx->update_iod.iod_recxs;
	ctx->fetch_iod.iod_nr = ctx->update_iod.iod_nr;
	ctx->fetch_iod.iod_type = ctx->update_iod.iod_type;
}

static daos_oclass_id_t dts_ec_agg_oc = DAOS_OC_EC_K2P1_SPEC_RANK_L32K;

static int
incremental_fill(void **statep)
{
	dts_ec_agg_oc = DAOS_OC_EC_K2P1_SPEC_RANK_L32K;
	return 0;
}

static void
ec_cleanup_cont_obj(struct ec_agg_test_ctx *ctx)
{
	int rc;

	rc = daos_obj_close(ctx->oh, NULL);
	assert_int_equal(rc, 0);

	/* Close & Destroy Container */
	rc = daos_cont_close(ctx->coh, NULL);
	assert_int_equal(rc, 0);
	rc = daos_cont_destroy(ctx->poh, ctx->uuid, true, NULL);
	assert_int_equal(rc, 0);
}

static void
ec_cleanup_data(struct ec_agg_test_ctx *ctx)
{
	d_sgl_fini(&ctx->update_sgl, true);
	d_sgl_fini(&ctx->fetch_sgl, true);
}

static void
test_filled_stripe(void **statep)
{
	struct ec_agg_test_ctx	 ctx = { 0 };
	struct daos_oclass_attr	*oca;
	struct obj_ec_codec     *codec;
	tse_task_t		*task = NULL;
	unsigned char		**data = NULL;
	unsigned char		**parity = NULL;
	unsigned char		*buf = NULL;
	unsigned int		 k, p, len, shard = 2;
	int			 i, j, rc;

	ec_setup_from_test_args(&ctx, (test_arg_t *)*statep);
	ec_setup_cont_obj(&ctx, dts_ec_agg_oc);
	assert_int_equal(daos_oclass_is_ec(ctx.oid, &oca), true);
	assert_int_equal(oca->u.ec.e_k, 2);
	len = oca->u.ec.e_len;
	k = oca->u.ec.e_k;
	p = oca->u.ec.e_p;
	D_ALLOC_ARRAY(data, k);
	assert_int_equal(!data, 0);
	D_ALLOC_ARRAY(parity, k);
	assert_int_equal(!parity, 0);
	for (i = 0; i < p; i++) {
		D_ALLOC_ARRAY(parity[i], len);
		assert_int_equal(!parity[i], 0);
	}

	codec = obj_ec_codec_get(daos_obj_id2class(ctx.oid));
	for (i = 0; i < 256; i++) {
		ec_setup_single_recx_data(&ctx, EC_SPECIFIED, i * (len / 2),
					  len / 2, false);
		rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey,
				     1, &ctx.update_iod, &ctx.update_sgl, NULL);
		assert_int_equal(rc, 0);
	}
	for (i = 0; i < 256; i++) {
		ec_setup_single_recx_data(&ctx, EC_SPECIFIED, i * (len / 2),
					  len / 2, true);
		rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey,
				     1, &ctx.update_iod, &ctx.update_sgl, NULL);
		assert_int_equal(rc, 0);
	}
	for (i = 0; i < 256; i++) {
		ec_setup_single_recx_data(&ctx, EC_SPECIFIED, i * (len / 2),
					  len / 2, 2);
		rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey,
				     1, &ctx.update_iod, &ctx.update_sgl, NULL);
		assert_int_equal(rc, 0);
	}
	sleep(30);
	for (i = 0; i < 64; i++) {
		ec_setup_single_recx_data(&ctx, EC_SPECIFIED, i * (2 * len),
					  2 * len, false);
		buf = ctx.update_sgl.sg_iovs[0].iov_buf;
		for (j = 0; j < k; j++)
			data[j] = &buf[j * len];
		ec_encode_data(len, k, p, codec->ec_gftbls, data, parity);
		ctx.fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
		rc = dc_obj_fetch_task_create(ctx.oh, DAOS_TX_NONE, 0,
					      &ctx.dkey, 1, DIOF_TO_SPEC_SHARD,
					      &ctx.fetch_iod, &ctx.fetch_sgl,
					      &ctx.fetch_iom, &shard, NULL,
					      NULL, &task);
		assert_int_equal(rc, 0);
		rc = dc_task_schedule(task, true);
		assert_int_equal(rc, 0);
		/* verify no remaining replicas on parity target */
		assert_int_equal(ctx.fetch_iom.iom_nr_out, 0);
		task = NULL;
		memset(&ctx.fetch_iom, 0, sizeof(daos_iom_t));
		ctx.fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
		ctx.fetch_iod.iod_recxs[0].rx_idx = (i * len) |
						     PARITY_INDICATOR;
		ctx.fetch_iod.iod_recxs[0].rx_nr = len;
		ctx.iom_recx.rx_nr = len;
		rc = dc_obj_fetch_task_create(ctx.oh, DAOS_TX_NONE, 0,
					      &ctx.dkey, 1, DIOF_TO_SPEC_SHARD,
					      &ctx.fetch_iod, &ctx.fetch_sgl,
					      &ctx.fetch_iom, &shard, NULL,
					      NULL, &task);
		assert_int_equal(rc, 0);
		rc = dc_task_schedule(task, true);
		assert_int_equal(rc, 0);
		/* verify parity now exists on parity target */
		assert_int_equal(ctx.fetch_iom.iom_nr_out, 1);
		task = NULL;
	}
	for (i = 0; i < 64; i++) {
		ec_setup_single_recx_data(&ctx, EC_SPECIFIED, i * (2 * len),
					  2 * len, true);
		buf = ctx.update_sgl.sg_iovs[0].iov_buf;
		for (j = 0; j < k; j++)
			data[j] = &buf[j * len];
		ec_encode_data(len, k, p, codec->ec_gftbls, data, parity);
		ctx.fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
		rc = dc_obj_fetch_task_create(ctx.oh, DAOS_TX_NONE, 0,
					      &ctx.dkey, 1, DIOF_TO_SPEC_SHARD,
					      &ctx.fetch_iod, &ctx.fetch_sgl,
					      &ctx.fetch_iom, &shard, NULL,
					      NULL, &task);
		assert_int_equal(rc, 0);
		rc = dc_task_schedule(task, true);
		assert_int_equal(rc, 0);
		/* verify no remaining replicas on parity target */
		assert_int_equal(ctx.fetch_iom.iom_nr_out, 0);
		task = NULL;
		memset(&ctx.fetch_iom, 0, sizeof(daos_iom_t));
		ctx.fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
		ctx.fetch_iod.iod_recxs[0].rx_idx = (i * len) |
						     PARITY_INDICATOR;
		ctx.fetch_iod.iod_recxs[0].rx_nr = len;
		ctx.iom_recx.rx_nr = len;
		rc = dc_obj_fetch_task_create(ctx.oh, DAOS_TX_NONE, 0,
					      &ctx.dkey, 1, DIOF_TO_SPEC_SHARD,
					      &ctx.fetch_iod, &ctx.fetch_sgl,
					      &ctx.fetch_iom, &shard, NULL,
					      NULL, &task);
		assert_int_equal(rc, 0);
		rc = dc_task_schedule(task, true);
		assert_int_equal(rc, 0);
		/* verify parity now exists on parity target */
		assert_int_equal(ctx.fetch_iom.iom_nr_out, 1);
		task = NULL;
	}
	for (i = 0; i < 64; i++) {
		ec_setup_single_recx_data(&ctx, EC_SPECIFIED, i * (2 * len),
					  2 * len, 2);
		buf = ctx.update_sgl.sg_iovs[0].iov_buf;
		for (j = 0; j < k; j++)
			data[j] = &buf[j * len];
		ec_encode_data(len, k, p, codec->ec_gftbls, data, parity);
		ctx.fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
		rc = dc_obj_fetch_task_create(ctx.oh, DAOS_TX_NONE, 0,
					      &ctx.dkey, 1, DIOF_TO_SPEC_SHARD,
					      &ctx.fetch_iod, &ctx.fetch_sgl,
					      &ctx.fetch_iom, &shard, NULL,
					      NULL, &task);
		assert_int_equal(rc, 0);
		rc = dc_task_schedule(task, true);
		assert_int_equal(rc, 0);
		/* verify no remaining replicas on parity target */
		assert_int_equal(ctx.fetch_iom.iom_nr_out, 0);
		task = NULL;
		memset(&ctx.fetch_iom, 0, sizeof(daos_iom_t));
		ctx.fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
		ctx.fetch_iod.iod_recxs[0].rx_idx = (i * len) |
						     PARITY_INDICATOR;
		ctx.fetch_iod.iod_recxs[0].rx_nr = len;
		ctx.iom_recx.rx_nr = len;
		rc = dc_obj_fetch_task_create(ctx.oh, DAOS_TX_NONE, 0,
					      &ctx.dkey, 1, DIOF_TO_SPEC_SHARD,
					      &ctx.fetch_iod, &ctx.fetch_sgl,
					      &ctx.fetch_iom, &shard, NULL,
					      NULL, &task);
		assert_int_equal(rc, 0);
		rc = dc_task_schedule(task, true);
		assert_int_equal(rc, 0);
		/* verify parity now exists on parity target */
		assert_int_equal(ctx.fetch_iom.iom_nr_out, 1);
		task = NULL;
	}
	ec_cleanup_data(&ctx);
	ec_cleanup_cont_obj(&ctx);
}


static int
ec_setup(void **statep)
{
	return test_setup(statep, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			  4, NULL);
}


static const struct CMUnitTest ec_agg_tests[] = {
	{"DAOS_ECAG00: test_filled_stripe", test_filled_stripe,
	  incremental_fill, test_case_teardown},
};


int run_daos_aggregation_ec_test(int rank, int size, int *sub_tests,
				 int sub_tests_size)
{

	int rc = 0;

	if (rank != 0) {
		MPI_Barrier(MPI_COMM_WORLD);
		return rc;
	}
	rc = cmocka_run_group_tests_name("DAOS EC AGGREGATION TESTS",
					 ec_agg_tests, ec_setup, test_teardown);

	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}

