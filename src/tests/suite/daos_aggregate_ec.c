/** * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

#define LAYER_COORD 1

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

#define TEST_EC_CELL_SZ	(1 << 15)

bool
oid_is_ec(daos_obj_id_t oid, struct daos_oclass_attr **attr)
{
	struct daos_oclass_attr *oca;

	oca = daos_oclass_attr_find(oid, NULL);
	if (attr != NULL)
		*attr = oca;
	if (oca == NULL)
		return false;

	return daos_oclass_is_ec(oca);
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
	d_sg_list_t		punch_sgl;
	daos_iom_t		fetch_iom;
	daos_iod_t		fetch_iod;
	d_sg_list_t             fetch_sgl;
	daos_recx_t             recx[8];
	daos_recx_t             iom_recx;
};

static void
iov_update_pfill(d_iov_t *iov, unsigned int cell, unsigned int len,
		 unsigned int overwrite)
{
	char	*dest = iov->iov_buf;
	int	 k, i = 0;

	for (k = 0; k < len; k++)
		dest[i++] = cell;
}

static void
iov_update_fill(d_iov_t *iov, unsigned int cells, unsigned int len,
		unsigned int overwrite)
{
	char	*dest = iov->iov_buf;
	int	 j, k, i = 0;

	for (j = 0; j < cells; j++)
		for (k = 0; k < len; k++)
			if (overwrite)
				dest[i++] = 128;
			else
				dest[i++] = j;
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
	char	str[37];
	int	rc;
	daos_prop_t *props;

	props = daos_prop_alloc(3);
	assert_non_null(props);
	props->dpp_entries[0].dpe_type = DAOS_PROP_CO_EC_CELL_SZ;
	props->dpp_entries[0].dpe_val = TEST_EC_CELL_SZ;
	props->dpp_entries[1].dpe_type = DAOS_PROP_CO_CSUM;
	props->dpp_entries[1].dpe_val = DAOS_PROP_CO_CSUM_CRC32;
	props->dpp_entries[2].dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	props->dpp_entries[2].dpe_val = DAOS_PROP_CO_CSUM_SV_ON;

	rc = daos_cont_create(ctx->poh, &ctx->uuid, props, NULL);
	daos_prop_free(props);
	assert_success(rc);

	uuid_unparse(ctx->uuid, str);
	rc = daos_cont_open(ctx->poh, str, DAOS_COO_RW,
			    &ctx->coh, &ctx->info, NULL);
	assert_success(rc);

	ctx->oid.lo = 1;
	ctx->oid.hi =  100;
	daos_obj_generate_oid(ctx->coh, &ctx->oid, 0, oclass, 0, 0);
	rc = daos_obj_open(ctx->coh, ctx->oid, 0, &ctx->oh, NULL);
	assert_success(rc);
}

static void
ec_setup_obj(struct ec_agg_test_ctx *ctx, daos_oclass_id_t oclass, int low)
{
	int		 rc;

	ctx->oid.lo = low;
	ctx->oid.hi =  100;
	daos_obj_generate_oid(ctx->coh, &ctx->oid, 0, oclass, 0, 0);
	ctx->oh = DAOS_HDL_INVAL;
	rc = daos_obj_open(ctx->coh, ctx->oid, 0, &ctx->oh, NULL);
	assert_success(rc);
}

static void
ec_setup_punch_recx_data(struct ec_agg_test_ctx *ctx, unsigned int mode,
			 daos_size_t offset, daos_size_t data_bytes,
			 unsigned char switch_akey, unsigned int cell)
{
	struct daos_oclass_attr	*oca, oca1;
	unsigned int		len;
	int			rc;

	if (mode != EC_SPECIFIED)
		return;
	/* else set databytes based on oclass */

	assert_int_equal(oid_is_ec(ctx->oid, &oca), true);
	rc = daos_obj2oc_attr(ctx->oh, &oca1);
	assert_int_equal(rc, 0);
	len = oca1.u.ec.e_len;

	iov_alloc_str(&ctx->dkey, "dkey");
	if (switch_akey == 1)
		iov_alloc_str(&ctx->update_iod.iod_name, "bkey");
	else if (switch_akey == 2)
		iov_alloc_str(&ctx->update_iod.iod_name, "ckey");
	else
		iov_alloc_str(&ctx->update_iod.iod_name, "akey");

	d_sgl_init(&ctx->update_sgl, 1);

	d_sgl_init(&ctx->fetch_sgl, 1);
	iov_alloc(&ctx->fetch_sgl.sg_iovs[0], data_bytes);

	ctx->recx[0].rx_idx = offset;
	ctx->recx[0].rx_nr = data_bytes;
	ctx->update_iod.iod_size = 0;
	ctx->update_iod.iod_nr	= 1;
	ctx->update_iod.iod_recxs = &ctx->recx[0];
	ctx->update_iod.iod_type  = DAOS_IOD_ARRAY;

	ctx->iom_recx.rx_idx = offset;
	ctx->iom_recx.rx_nr = len;

	ctx->fetch_iom.iom_recxs = &ctx->iom_recx;
	ctx->fetch_iom.iom_nr = 1;
	ctx->fetch_iom.iom_nr_out = 0;

	/** Setup Fetch IOD*/
	ctx->fetch_iod.iod_name = ctx->update_iod.iod_name;
	ctx->fetch_iod.iod_size = 1;
	ctx->fetch_iod.iod_recxs = ctx->update_iod.iod_recxs;
	ctx->fetch_iod.iod_nr = ctx->update_iod.iod_nr;
	ctx->fetch_iod.iod_type = ctx->update_iod.iod_type;
}

static void
ec_setup_single_recx_data(struct ec_agg_test_ctx *ctx, unsigned int mode,
			  daos_size_t offset, daos_size_t data_bytes,
			  unsigned char switch_akey, bool partial_write,
			  bool overwrite, unsigned int cell)
{
	struct daos_oclass_attr	*oca, oca1;
	unsigned int		k, len;
	int			rc;

	if (mode != EC_SPECIFIED)
		return;
	/* else set databytes based on oclass */

	assert_int_equal(oid_is_ec(ctx->oid, &oca), true);
	rc = daos_obj2oc_attr(ctx->oh, &oca1);
	assert_int_equal(rc, 0);
	len = oca1.u.ec.e_len;

	k = oca->u.ec.e_k;
	iov_alloc_str(&ctx->dkey, "dkey");
	if (switch_akey == 1)
		iov_alloc_str(&ctx->update_iod.iod_name, "bkey");
	else if (switch_akey == 2)
		iov_alloc_str(&ctx->update_iod.iod_name, "ckey");
	else
		iov_alloc_str(&ctx->update_iod.iod_name, "akey");

	d_sgl_init(&ctx->update_sgl, 1);
	iov_alloc(&ctx->update_sgl.sg_iovs[0], data_bytes);
	if (overwrite) {
		iov_update_fill(ctx->update_sgl.sg_iovs, 1, len, true);
	} else if (partial_write) {
		iov_update_pfill(ctx->update_sgl.sg_iovs, cell,
				 data_bytes, false);
	} else {
		iov_update_fill(ctx->update_sgl.sg_iovs, k, len, false);
	}

	d_sgl_init(&ctx->fetch_sgl, 1);
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

	/** Setup Fetch IOD */
	ctx->fetch_iod.iod_name = ctx->update_iod.iod_name;
	ctx->fetch_iod.iod_size = ctx->update_iod.iod_size;
	ctx->fetch_iod.iod_recxs = ctx->update_iod.iod_recxs;
	ctx->fetch_iod.iod_nr = ctx->update_iod.iod_nr;
	ctx->fetch_iod.iod_type = ctx->update_iod.iod_type;
}

static daos_oclass_id_t dts_ec_agg_oc = OC_EC_2P1G1;

static int
incremental_fill(void **statep)
{
	dts_ec_agg_oc = OC_EC_2P1G1;
	return 0;
}

static void
ec_cleanup_cont(struct ec_agg_test_ctx *ctx)
{
	char	str[37];
	int	rc;

	/* Close & Destroy Container */
	rc = daos_cont_close(ctx->coh, NULL);
	assert_rc_equal(rc, 0);
	uuid_unparse(ctx->uuid, str);
	rc = daos_cont_destroy(ctx->poh, str, true, NULL);
	assert_rc_equal(rc, 0);
}

static void
ec_cleanup_data(struct ec_agg_test_ctx *ctx)
{
	d_sgl_fini(&ctx->update_sgl, true);
	d_sgl_fini(&ctx->fetch_sgl, true);
}

#define NUM_STRIPES 64
#define NUM_KEYS 3

#define EXTS_PER_STRIPE 4
static void
test_filled_stripe(struct ec_agg_test_ctx *ctx)
{
	struct daos_oclass_attr	*oca, oca1;
	unsigned int		 len;
	int			 i, j, rc;

	dts_ec_agg_oc = OC_EC_2P1G1;
	ec_setup_cont_obj(ctx, dts_ec_agg_oc);
	assert_int_equal(oid_is_ec(ctx->oid, &oca), true);
	assert_int_equal(oca->u.ec.e_k, 2);
	rc = daos_obj2oc_attr(ctx->oh, &oca1);
	assert_int_equal(rc, 0);
	len = oca1.u.ec.e_len;


	for (j = 0; j < NUM_KEYS; j++)
		for (i = 0; i < NUM_STRIPES * EXTS_PER_STRIPE; i++) {
			int cell = i % 4 < 2 ? 0 : 1;

			ec_setup_single_recx_data(ctx, EC_SPECIFIED,
						  i * (len / 2), len / 2, j,
						  true, false, cell);
			rc = daos_obj_update(ctx->oh, DAOS_TX_NONE, 0,
					     &ctx->dkey, 1, &ctx->update_iod,
					     &ctx->update_sgl, NULL);
			assert_rc_equal(rc, 0);
			ec_cleanup_data(ctx);
		}

	rc = daos_obj_close(ctx->oh, NULL);
	assert_rc_equal(rc, 0);
}

static void
verify_1p(struct ec_agg_test_ctx *ctx, daos_oclass_id_t ec_agg_oc,
	  unsigned int shard)
{
	struct daos_oclass_attr	*oca, oca1;
	struct obj_ec_codec     *codec;
	tse_task_t		*task = NULL;
	unsigned char		**data = NULL;
	unsigned char		**parity = NULL;
	unsigned char		*buf = NULL;
	unsigned int		 k, p, len;
	int			 i, j, rc;

	if (shard > 2)
		ec_setup_obj(ctx, ec_agg_oc, 3);
	else
		ec_setup_obj(ctx, ec_agg_oc, 1);
	assert_int_equal(oid_is_ec(ctx->oid, &oca), true);
	rc = daos_obj2oc_attr(ctx->oh, &oca1);
	assert_int_equal(rc, 0);
	len = oca1.u.ec.e_len;
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

	ec_setup_single_recx_data(ctx, EC_SPECIFIED, 0, (daos_size_t)k * len, 0,
				  false, false, 0);
	if (shard > 2)
		iov_update_fill(ctx->update_sgl.sg_iovs, 1, len, true);

	buf = ctx->update_sgl.sg_iovs[0].iov_buf;
	for (j = 0; j < k; j++)
		data[j] = &buf[j * len];
	codec = obj_ec_codec_get(daos_obj_id2class(ctx->oid));
	ec_encode_data(len, k, p, codec->ec_gftbls, data, parity);

	ec_cleanup_data(ctx);

	for (j = 0; j < NUM_KEYS; j++)
		for (i = 0; i < NUM_STRIPES; i++) {
			ec_setup_single_recx_data(ctx, EC_SPECIFIED,
						  i * ((daos_size_t)k * len),
						  k * (daos_size_t)len, j,
						  false, false, 0);
			ctx->fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
			rc = dc_obj_fetch_task_create(ctx->oh, DAOS_TX_NONE, 0,
						      &ctx->dkey, 1,
						      DIOF_TO_SPEC_SHARD,
						      &ctx->fetch_iod,
						      &ctx->fetch_sgl,
						      &ctx->fetch_iom, &shard,
						      NULL, NULL, NULL, &task);
			assert_rc_equal(rc, 0);
			rc = dc_task_schedule(task, true);
			assert_rc_equal(rc, 0);
			/* verify no remaining replicas on parity tgt */
			assert_int_equal(ctx->fetch_iom.iom_nr_out, 0);
			task = NULL;
			memset(&ctx->fetch_iom, 0, sizeof(daos_iom_t));
			ctx->fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
			ctx->fetch_iod.iod_recxs[0].rx_idx = (i * len) |
							     PARITY_INDICATOR;
			ctx->fetch_iod.iod_recxs[0].rx_nr = len;
			ctx->iom_recx.rx_nr = len;
			rc = dc_obj_fetch_task_create(ctx->oh, DAOS_TX_NONE, 0,
						      &ctx->dkey, 1,
						      DIOF_TO_SPEC_SHARD,
						      &ctx->fetch_iod,
						      &ctx->fetch_sgl,
						      &ctx->fetch_iom, &shard,
						      NULL, NULL, NULL, &task);
			assert_rc_equal(rc, 0);
			rc = dc_task_schedule(task, true);
			assert_rc_equal(rc, 0);
			/* verify parity now exists on parity target */
			assert_int_equal(ctx->fetch_iom.iom_nr_out, 1);
			assert_int_equal(memcmp(ctx->
						fetch_sgl.sg_iovs[0].iov_buf,
						parity[0], len), 0);
			task = NULL;
			ec_cleanup_data(ctx);
		}
	rc = daos_obj_close(ctx->oh, NULL);
	assert_rc_equal(rc, 0);
}

static void
test_half_stripe(struct ec_agg_test_ctx *ctx)
{
	struct daos_oclass_attr	*oca, oca1;
	unsigned int		 len;
	int			 i, j, rc;

	dts_ec_agg_oc = OC_EC_2P2G1;
	ec_setup_obj(ctx, dts_ec_agg_oc, 2);
	assert_int_equal(oid_is_ec(ctx->oid, &oca), true);
	assert_int_equal(oca->u.ec.e_k, 2);
	rc = daos_obj2oc_attr(ctx->oh, &oca1);
	assert_int_equal(rc, 0);
	len = oca1.u.ec.e_len;

	for (j = 0; j < NUM_KEYS; j++)
		for (i = 0; i < NUM_STRIPES; i++) {
			ec_setup_single_recx_data(ctx, EC_SPECIFIED,
						  i * (len * 2), len * 2, j,
						  false, false, 0);
			rc = daos_obj_update(ctx->oh, DAOS_TX_NONE, 0,
					     &ctx->dkey, 1, &ctx->update_iod,
					     &ctx->update_sgl, NULL);
			assert_rc_equal(rc, 0);
			ec_cleanup_data(ctx);
		}

	sleep(2);

	for (j = 0; j < NUM_KEYS; j++)
		for (i = 0; i < NUM_STRIPES; i++) {
			ec_setup_single_recx_data(ctx, EC_SPECIFIED,
						  i * (len * 2), len, j,
						  false, true, 0);
			rc = daos_obj_update(ctx->oh, DAOS_TX_NONE, 0,
					     &ctx->dkey, 1, &ctx->update_iod,
					     &ctx->update_sgl, NULL);
			assert_rc_equal(rc, 0);
			ec_cleanup_data(ctx);
		}

	rc = daos_obj_close(ctx->oh, NULL);
	assert_rc_equal(rc, 0);
}

static void
verify_2p(struct ec_agg_test_ctx *ctx, daos_oclass_id_t ec_agg_oc)
{
	struct daos_oclass_attr	*oca, oca1;
	struct obj_ec_codec     *codec;
	tse_task_t		*task = NULL;
	unsigned char		**data = NULL;
	unsigned char		**parity = NULL;
	unsigned char		*buf = NULL;
	unsigned int		 k, p, len, shard = 2;
	int			 i, j, rc;

	ec_setup_obj(ctx, ec_agg_oc, 2);
	assert_int_equal(oid_is_ec(ctx->oid, &oca), true);
	assert_int_equal(oca->u.ec.e_k, 2);
	rc = daos_obj2oc_attr(ctx->oh, &oca1);
	assert_int_equal(rc, 0);
	len = oca1.u.ec.e_len;
	k = oca->u.ec.e_k;
	p = oca->u.ec.e_p;
	D_ALLOC_ARRAY(data, k);
	assert_int_equal(!data, 0);
	D_ALLOC_ARRAY(parity, p);
	assert_int_equal(!parity, 0);
	for (i = 0; i < p; i++) {
		D_ALLOC_ARRAY(parity[i], len);
		assert_int_equal(!parity[i], 0);
	}

	ec_setup_single_recx_data(ctx, EC_SPECIFIED, 0, 2 * len, 0, false,
				  false, 0);
	iov_update_fill(ctx->update_sgl.sg_iovs, 1, len, true);
	buf = ctx->update_sgl.sg_iovs[0].iov_buf;
	for (j = 0; j < k; j++)
		data[j] = &buf[j * len];

	codec = obj_ec_codec_get(daos_obj_id2class(ctx->oid));
	ec_encode_data(len, k, p, codec->ec_gftbls, data, parity);

	for (j = 0; j < NUM_KEYS; j++)
		for (i = 0; i < NUM_STRIPES; i++) {
			ec_setup_single_recx_data(ctx, EC_SPECIFIED,
						  i * (2 * len), 2 * len, j,
						  false, false, 0);
			ctx->fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
			shard++;
			rc = dc_obj_fetch_task_create(ctx->oh, DAOS_TX_NONE, 0,
						      &ctx->dkey, 1,
						      DIOF_TO_SPEC_SHARD,
						      &ctx->fetch_iod,
						      &ctx->fetch_sgl,
						      &ctx->fetch_iom, &shard,
						      NULL, NULL, NULL, &task);
			assert_rc_equal(rc, 0);
			rc = dc_task_schedule(task, true);
			assert_rc_equal(rc, 0);
			/* verify no remaining replicas on parity tgt */
			assert_int_equal(ctx->fetch_iom.iom_nr_out, 0);
			task = NULL;
			memset(&ctx->fetch_iom, 0, sizeof(daos_iom_t));
			ctx->fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
			shard--;
			rc = dc_obj_fetch_task_create(ctx->oh, DAOS_TX_NONE, 0,
						      &ctx->dkey, 1,
						      DIOF_TO_SPEC_SHARD,
						      &ctx->fetch_iod,
						      &ctx->fetch_sgl,
						      &ctx->fetch_iom, &shard,
						      NULL, NULL, NULL, &task);
			assert_rc_equal(rc, 0);
			rc = dc_task_schedule(task, true);
			assert_rc_equal(rc, 0);
			/* verify no remaining replicas on peer parity tgt */
			assert_int_equal(ctx->fetch_iom.iom_nr_out, 0);
			task = NULL;
			memset(&ctx->fetch_iom, 0, sizeof(daos_iom_t));
			shard++;
			ctx->fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
			ctx->fetch_iod.iod_recxs[0].rx_idx = (i * len) |
							PARITY_INDICATOR;
			ctx->fetch_iod.iod_recxs[0].rx_nr = len;
			ctx->iom_recx.rx_nr = len;
			rc = dc_obj_fetch_task_create(ctx->oh, DAOS_TX_NONE, 0,
						      &ctx->dkey, 1,
						      DIOF_TO_SPEC_SHARD,
						      &ctx->fetch_iod,
						      &ctx->fetch_sgl,
						      &ctx->fetch_iom, &shard,
						      NULL, NULL, NULL, &task);
			assert_rc_equal(rc, 0);
			rc = dc_task_schedule(task, true);
			assert_rc_equal(rc, 0);
			/* verify on parity leader */
			assert_int_equal(ctx->fetch_iom.iom_nr_out, 1);
			assert_int_equal(memcmp(ctx->fetch_sgl.sg_iovs[0].
						iov_buf,
						parity[1], len), 0);
			task = NULL;
			memset(&ctx->fetch_iom, 0, sizeof(daos_iom_t));
			shard--;
			ctx->fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
			ctx->iom_recx.rx_nr = len;
			rc = dc_obj_fetch_task_create(ctx->oh, DAOS_TX_NONE, 0,
						      &ctx->dkey, 1,
						      DIOF_TO_SPEC_SHARD,
						      &ctx->fetch_iod,
						      &ctx->fetch_sgl,
						      &ctx->fetch_iom, &shard,
						      NULL, NULL, NULL, &task);
			assert_rc_equal(rc, 0);
			rc = dc_task_schedule(task, true);
			assert_rc_equal(rc, 0);
			/* verify parity now exists on parity target */
			assert_int_equal(ctx->fetch_iom.iom_nr_out, 1);
			assert_int_equal(memcmp(ctx->fetch_sgl.sg_iovs[0].
						iov_buf,
						parity[0], len), 0);
			task = NULL;
			ec_cleanup_data(ctx);
		}
	rc = daos_obj_close(ctx->oh, NULL);
	assert_rc_equal(rc, 0);
}

static void
test_partial_stripe(struct ec_agg_test_ctx *ctx)
{
	struct daos_oclass_attr	*oca, oca1;
	unsigned int		 len;
	int			 i, j, rc;

	dts_ec_agg_oc = OC_EC_4P1G1;
	ec_setup_obj(ctx, dts_ec_agg_oc, 3);
	assert_int_equal(oid_is_ec(ctx->oid, &oca), true);
	rc = daos_obj2oc_attr(ctx->oh, &oca1);
	assert_int_equal(rc, 0);
	len = oca1.u.ec.e_len;

	for (j = 0; j < NUM_KEYS; j++)
		for (i = 0; i < NUM_STRIPES; i++) {
			ec_setup_single_recx_data(ctx, EC_SPECIFIED,
						  i * (len * 4), len * 4, j,
						  false, false, 0);
			rc = daos_obj_update(ctx->oh, DAOS_TX_NONE, 0,
					     &ctx->dkey, 1, &ctx->update_iod,
					     &ctx->update_sgl, NULL);
			assert_rc_equal(rc, 0);
			ec_cleanup_data(ctx);
		}

	sleep(2);

	for (j = 0; j < NUM_KEYS; j++)
		for (i = 0; i < NUM_STRIPES; i++) {
			ec_setup_single_recx_data(ctx, EC_SPECIFIED,
						  i * (len * 4), len, j,
						  false, true, 0);
			rc = daos_obj_update(ctx->oh, DAOS_TX_NONE, 0,
					     &ctx->dkey, 1, &ctx->update_iod,
					     &ctx->update_sgl, NULL);
			assert_rc_equal(rc, 0);
			ec_cleanup_data(ctx);
		}

	rc = daos_obj_close(ctx->oh, NULL);
	assert_rc_equal(rc, 0);
}

static void
test_range_punch(struct ec_agg_test_ctx *ctx)
{
	struct daos_oclass_attr	*oca, oca1;
	unsigned int		 len, k;
	int			 i, j, rc;

	dts_ec_agg_oc = OC_EC_4P1G1;
	ec_setup_obj(ctx, dts_ec_agg_oc, 4);
	assert_int_equal(oid_is_ec(ctx->oid, &oca), true);
	rc = daos_obj2oc_attr(ctx->oh, &oca1);
	assert_int_equal(rc, 0);
	len = oca1.u.ec.e_len;
	k = oca->u.ec.e_k;
	for (j = 0; j < NUM_KEYS; j++)
		for (i = 0; i < NUM_STRIPES; i++) {
			ec_setup_single_recx_data(ctx, EC_SPECIFIED,
						  i * (len * 4), len * 4, j,
						  false, false, 0);
			rc = daos_obj_update(ctx->oh, DAOS_TX_NONE, 0,
					     &ctx->dkey, 1, &ctx->update_iod,
					     &ctx->update_sgl, NULL);
			assert_int_equal(rc, 0);
			ec_cleanup_data(ctx);
		}

	sleep(1);

	for (j = 0; j < NUM_KEYS; j++)
		for (i = 0; i < NUM_STRIPES; i++) {
			ec_setup_punch_recx_data(ctx, EC_SPECIFIED,
						 i * len * (daos_size_t)k, len, j, 0);
			rc = daos_obj_update(ctx->oh, DAOS_TX_NONE, 0,
					     &ctx->dkey, 1, &ctx->update_iod,
					     &ctx->update_sgl, NULL);
			assert_int_equal(rc, 0);
			ec_cleanup_data(ctx);
		}

	for (j = 0; j < NUM_KEYS; j++)
		for (i = 0; i < NUM_STRIPES; i++) {
			ec_setup_single_recx_data(ctx, EC_SPECIFIED,
						  i * len * (daos_size_t)k + 2 * len,
						  len, j, false, true, 0);
			rc = daos_obj_update(ctx->oh, DAOS_TX_NONE, 0,
					     &ctx->dkey, 1, &ctx->update_iod,
					     &ctx->update_sgl, NULL);
			assert_int_equal(rc, 0);
			ec_cleanup_data(ctx);
		}

	rc = daos_obj_close(ctx->oh, NULL);
	assert_int_equal(rc, 0);
}

static void
verify_rp1p(struct ec_agg_test_ctx *ctx, daos_oclass_id_t ec_agg_oc,
	    unsigned int shard)
{
	struct daos_oclass_attr	*oca, oca1;
	tse_task_t		*task = NULL;
	unsigned int		 k, len;
	int			 i, j, rc;

	ec_setup_obj(ctx, ec_agg_oc, 4);

	assert_int_equal(oid_is_ec(ctx->oid, &oca), true);
	rc = daos_obj2oc_attr(ctx->oh, &oca1);
	assert_int_equal(rc, 0);
	len = oca1.u.ec.e_len;

	k = oca->u.ec.e_k;

	for (j = 0; j < NUM_KEYS; j++)
		for (i = 0; i < NUM_STRIPES; i++) {
			ec_setup_single_recx_data(ctx, EC_SPECIFIED,
						  i * len * (daos_size_t)k + len,
						  len,
						  j, true, false, 0);
			memset(&ctx->fetch_iom, 0, sizeof(daos_iom_t));
			ctx->fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
			ctx->fetch_iod.iod_recxs[0].rx_idx = i * k * len + len;
			ctx->fetch_iod.iod_recxs[0].rx_nr = len;
			ctx->iom_recx.rx_nr = len;
			ctx->iom_recx.rx_idx = i * k * len + len;
			rc = dc_obj_fetch_task_create(ctx->oh, DAOS_TX_NONE, 0,
						      &ctx->dkey, 1,
						      DIOF_TO_SPEC_SHARD,
						      &ctx->fetch_iod,
						      &ctx->fetch_sgl,
						      &ctx->fetch_iom, &shard,
						      NULL, NULL, NULL, &task);
			assert_rc_equal(rc, 0);
			rc = dc_task_schedule(task, true);
			assert_rc_equal(rc, 0);
			/* verify replicas on parity tgt */
			assert_int_equal(ctx->fetch_iom.iom_nr_out, 1);
			task = NULL;
			memset(&ctx->fetch_iom, 0, sizeof(daos_iom_t));
			ctx->fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
			ctx->fetch_iod.iod_recxs[0].rx_idx = i * k * len +
				2 * len;
			ctx->fetch_iod.iod_recxs[0].rx_nr = len;
			ctx->iom_recx.rx_nr = len;
			ctx->iom_recx.rx_idx = i * k * len + 2 * len;
			rc = dc_obj_fetch_task_create(ctx->oh, DAOS_TX_NONE, 0,
						      &ctx->dkey, 1,
						      DIOF_TO_SPEC_SHARD,
						      &ctx->fetch_iod,
						      &ctx->fetch_sgl,
						      &ctx->fetch_iom, &shard,
						      NULL, NULL, NULL, &task);
			assert_rc_equal(rc, 0);
			rc = dc_task_schedule(task, true);
			assert_rc_equal(rc, 0);
			/* verify replicas on parity tgt */
			assert_int_equal(ctx->fetch_iom.iom_nr_out, 1);
			task = NULL;
			memset(&ctx->fetch_iom, 0, sizeof(daos_iom_t));
			ctx->fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
			ctx->fetch_iod.iod_recxs[0].rx_idx = i * k * len +
				3 * len;
			ctx->fetch_iod.iod_recxs[0].rx_nr = len;
			ctx->iom_recx.rx_nr = len;
			ctx->iom_recx.rx_idx = i * k * len + 3 * len;
			rc = dc_obj_fetch_task_create(ctx->oh, DAOS_TX_NONE, 0,
						      &ctx->dkey, 1,
						      DIOF_TO_SPEC_SHARD,
						      &ctx->fetch_iod,
						      &ctx->fetch_sgl,
						      &ctx->fetch_iom, &shard,
						      NULL, NULL, NULL, &task);
			assert_rc_equal(rc, 0);
			rc = dc_task_schedule(task, true);
			assert_rc_equal(rc, 0);
			/* verify replicas on parity tgt */
			assert_int_equal(ctx->fetch_iom.iom_nr_out, 1);
			task = NULL;
			memset(&ctx->fetch_iom, 0, sizeof(daos_iom_t));
			ctx->fetch_iom.iom_flags = DAOS_IOMF_DETAIL;
			ctx->fetch_iod.iod_recxs[0].rx_idx = (i * len) |
							     PARITY_INDICATOR;
			ctx->fetch_iod.iod_recxs[0].rx_nr = len;
			ctx->iom_recx.rx_nr = len;
			rc = dc_obj_fetch_task_create(ctx->oh, DAOS_TX_NONE, 0,
						      &ctx->dkey, 1,
						      DIOF_TO_SPEC_SHARD,
						      &ctx->fetch_iod,
						      &ctx->fetch_sgl,
						      &ctx->fetch_iom, &shard,
						      NULL, NULL, NULL, &task);
			assert_rc_equal(rc, 0);
			rc = dc_task_schedule(task, true);
			assert_rc_equal(rc, 0);
			/* verify parity no longer exists on parity target */
			assert_int_equal(ctx->fetch_iom.iom_nr_out, 0);
			task = NULL;
			ec_cleanup_data(ctx);
		}
	rc = daos_obj_close(ctx->oh, NULL);
	assert_rc_equal(rc, 0);
}

static void
setup_ec_agg_tests(void **statep, struct ec_agg_test_ctx *ctx)
{
	ec_setup_from_test_args(ctx, (test_arg_t *)*statep);
}

static void
cleanup_ec_agg_tests(struct ec_agg_test_ctx *ctx)
{
	if (ctx->update_iod.iod_name.iov_buf)
		D_FREE(ctx->update_iod.iod_name.iov_buf);
	if (ctx->dkey.iov_buf)
		D_FREE(ctx->dkey.iov_buf);
	ec_cleanup_cont(ctx);
}

static void
test_all_ec_agg(void **statep)
{
	test_arg_t		*arg = *statep;
	struct ec_agg_test_ctx	 ctx = { 0 };

	if (!test_runable(arg, 5))
		return;

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "disabled");
	setup_ec_agg_tests(statep, &ctx);
	test_filled_stripe(&ctx);
	test_half_stripe(&ctx);
	test_partial_stripe(&ctx);
	test_range_punch(&ctx);
	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      DAOS_FORCE_EC_AGG | DAOS_FAIL_ALWAYS,
			      0, NULL);
	print_message("sleep 45 seconds for aggregation ...\n");
	sleep(45);
	print_message("verification after aggregation\n");
	verify_1p(&ctx, OC_EC_2P1G1, 2);
	verify_2p(&ctx, OC_EC_2P2G1);
	verify_1p(&ctx, OC_EC_4P1G1, 4);
	verify_rp1p(&ctx, OC_EC_4P1G1, 4);
	cleanup_ec_agg_tests(&ctx);
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      0, 0, NULL);
}

static void
fetch_snap_with_agg(void **statep)
{
	test_arg_t		*arg = *statep;
	struct ec_agg_test_ctx	 ctx = { 0 };
	struct daos_oclass_attr	*oca;
	uint32_t		 cs, ss;
	daos_epoch_t		 snap_epoch;
	d_iov_t			 dkey;
	d_sg_list_t		 sgl;
	d_iov_t			 sg_iov;
	daos_iod_t		 iod;
	daos_recx_t		 recx;
	char			 *wbuf1;
	char			 *wbuf2;
	char			 *rbuf;
	daos_handle_t		 th;
	uint16_t		 fail_shard = 1;
	uint64_t		 fail_val;
	bool			 snap_higher = false;
	int			 rc;

	if (!test_runable(arg, 4))
		skip();

	FAULT_INJECTION_REQUIRED();

	daos_pool_set_prop(arg->pool.pool_uuid, "reclaim", "time");
	setup_ec_agg_tests(statep, &ctx);
	dts_ec_agg_oc = OC_EC_2P2G1;
	ec_setup_cont_obj(&ctx, dts_ec_agg_oc);
	assert_int_equal(oid_is_ec(ctx.oid, &oca), true);
	assert_int_equal(oca->u.ec.e_k, 2);
	assert_int_equal(oca->u.ec.e_k, 2);
	cs = TEST_EC_CELL_SZ;
	ss = cs * oca->u.ec.e_k;
	wbuf1 = calloc(ss, 1);
	wbuf2 = calloc(ss, 1);
	rbuf = calloc(ss, 1);
	memset(wbuf1, 'a', ss);
	memset(wbuf2, 'b', ss);
	memset(rbuf, 0, ss);

	d_iov_set(&dkey, "dkey", strlen("dkey"));
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));

	d_iov_set(&sg_iov, wbuf1, ss);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &sg_iov;
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= 0;
	recx.rx_nr	= ss;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

re_test:
	/* write @low_epoch */
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			     NULL);
	assert_rc_equal(rc, 0);
	/* create snapshot */
	if (!snap_higher) {
		print_message("test fetch_snapshot < vos_agg_boundary\n");
		rc = daos_cont_create_snap(ctx.coh, &snap_epoch, NULL, NULL);
		assert_rc_equal(rc, 0);
	}
	/* overwrite @high_epoch */
	d_iov_set(&sg_iov, wbuf2, ss);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			     NULL);
	assert_rc_equal(rc, 0);

	/* wait aggregation */
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      DAOS_FORCE_EC_AGG | DAOS_FAIL_ALWAYS,
			      0, NULL);
	print_message("wait for 25 seconds for VOS aggregation.\n");
	sleep(25);

	if (snap_higher) {
		print_message("test fetch_snapshot > vos_agg_boundary\n");
		rc = daos_cont_create_snap(ctx.coh, &snap_epoch, NULL, NULL);
		assert_rc_equal(rc, 0);
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_FAIL_AGG_BOUNDRY_MOVED |
				      DAOS_FAIL_ONCE, 0, NULL);
	}

	/* degraded fetch from snapshot, compare fetched data with
	 * buffer written @low_epoch
	 */
	rc = daos_tx_open_snap(ctx.coh, snap_epoch, &th, NULL);
	assert_rc_equal(rc, 0);

	fail_val = daos_shard_fail_value(&fail_shard, 1);
	daos_fail_loc_set(DAOS_FAIL_SHARD_OPEN | DAOS_FAIL_ONCE);
	daos_fail_value_set(fail_val);
	d_iov_set(&sg_iov, rbuf, ss);
	rc = daos_obj_fetch(ctx.oh, th, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	if (!snap_higher)
		assert_memory_equal(rbuf, wbuf1, ss);
	else
		assert_memory_equal(rbuf, wbuf2, ss);

	rc = daos_tx_close(th, NULL);
	assert_rc_equal(rc, 0);

	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      0, 0, NULL);
	daos_fail_loc_set(0);
	daos_fail_value_set(0);

	if (!snap_higher) {
		snap_higher = true;
		goto re_test;
	}

	free(wbuf1);
	free(wbuf2);
	free(rbuf);
	rc = daos_obj_close(ctx.oh, NULL);
	assert_rc_equal(rc, 0);
	cleanup_ec_agg_tests(&ctx);
}

#define NUM_SERVERS 5
static int
ec_setup(void **statep)
{
	return test_setup(statep, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			  NUM_SERVERS, NULL);
}

static const struct CMUnitTest ec_agg_tests[] = {
	{"DAOS_ECAG00: test EC aggregation", test_all_ec_agg,
	  incremental_fill, test_case_teardown},
	{"DAOS_ECAG01: test fetch snapshot lower than vos agg boundary",
	  fetch_snap_with_agg, async_disable, test_case_teardown},
};

int run_daos_aggregation_ec_test(int rank, int size, int *sub_tests,
				 int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(ec_agg_tests);
		sub_tests = NULL;
	}
	if (rank != 0)
		goto out;
	rc += run_daos_sub_tests("DAOS_EC_Aggregation", ec_agg_tests,
				 ARRAY_SIZE(ec_agg_tests), sub_tests,
				 sub_tests_size, ec_setup, test_teardown);

out:
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
