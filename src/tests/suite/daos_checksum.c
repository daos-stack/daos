/**
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"
#include "daos_iotest.h"

#include <daos/checksum.h>
#include <gurt/types.h>
#include <daos_prop.h>
#include <daos/task.h>
#include <daos/event.h>
#include <daos/container.h>
#include <daos/object.h>

#define EC_CSUM_OC	OC_EC_2P2G1

static void
iov_update_fill(d_iov_t *iov, char *data, uint64_t len_to_fill);

unsigned int
daos_checksum_test_arg2type(char *str)
{
	int result = daos_str2csumcontprop(str);

	if (result < 0)
		fail_msg("Invalid csum type: %s", str);

	return (unsigned int) result;
}

/** by default for replica object test */
static daos_oclass_id_t dts_csum_oc = OC_S1;
static uint32_t dts_csum_prop_type = DAOS_PROP_CO_CSUM_SHA512;

/** enable EC obj csum test or replica obj csum test */
static inline int
csum_ec_enable(void **state)
{
	dts_csum_oc = EC_CSUM_OC;
	return 0;
}

static inline int
csum_replia_enable(void **state)
{
	dts_csum_oc = OC_SX;
	return 0;
}

static inline bool
csum_ec_enabled()
{
	return dts_csum_oc == EC_CSUM_OC;
}

static inline uint32_t
csum_ec_grp_size()
{
	return 4;
}

/** fault injection helpers */
static void
set_fi(uint64_t flag)
{
	daos_fail_loc_set(flag | DAOS_FAIL_ONCE);
}
static void
client_corrupt_on_update()
{
	set_fi(DAOS_CSUM_CORRUPT_UPDATE);
}
static void
client_corrupt_on_fetch()
{
	set_fi(DAOS_CSUM_CORRUPT_FETCH);
}

static void
client_corrupt_akey_on_fetch()
{
	set_fi(DAOS_CSUM_CORRUPT_FETCH_AKEY);
}

static void
client_corrupt_dkey_on_fetch()
{
	set_fi(DAOS_CSUM_CORRUPT_FETCH_DKEY);
}

static void
client_clear_fault()
{
	daos_fail_loc_set(0);
}

static void
server_corrupt_disk(const char *group)
{
	int rc = daos_debug_set_params(group, -1, DMG_KEY_FAIL_LOC,
				      DAOS_CSUM_CORRUPT_DISK | DAOS_FAIL_ALWAYS,
				      0, NULL);
	assert_rc_equal(rc, 0);
}

static void
server_clear_fault(const char *group)
{
	int rc = daos_debug_set_params(group, -1,
				      DMG_KEY_FAIL_LOC, 0, 0, NULL);
	assert_rc_equal(rc, 0);
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

/** daos checksum test context */
struct csum_test_ctx {
	/** Pool */
	daos_handle_t		 poh;
	/** Container */
	daos_handle_t		 coh;
	daos_cont_info_t	 info;
	uuid_t			 uuid;
	/** Object */
	daos_handle_t		 oh;
	daos_obj_id_t		 oid;
	daos_key_t		 dkey;
	daos_iod_t		 update_iod;
	d_sg_list_t		 update_sgl;
	daos_iod_t		 fetch_iod;
	d_sg_list_t		 fetch_sgl;
	daos_recx_t		 recx[8];
	test_arg_t		*test_arg;
};

static void
setup_from_test_args(struct csum_test_ctx *ctx, test_arg_t *state)
{
	ctx->poh = state->pool.poh;
	ctx->test_arg = state;
}

/**
 * Setup the container & object portion of the test context. Uses the csum
 * params to create appropriate container properties.
 */
static void
setup_cont_obj(struct csum_test_ctx *ctx, int csum_prop_type, bool csum_sv,
	       int chunksize, daos_oclass_id_t oclass)
{
	char		str[37];
	daos_prop_t	*props = daos_prop_alloc(4);
	int		 rc;

	assert_non_null(props);
	props->dpp_entries[0].dpe_type = DAOS_PROP_CO_CSUM;
	props->dpp_entries[0].dpe_val = csum_prop_type;
	props->dpp_entries[1].dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	props->dpp_entries[1].dpe_val = csum_sv ? DAOS_PROP_CO_CSUM_SV_ON :
					DAOS_PROP_CO_CSUM_SV_OFF;
	props->dpp_entries[2].dpe_type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
	props->dpp_entries[2].dpe_val = chunksize != 0 ? chunksize : 1024*16;
	props->dpp_entries[3].dpe_type = DAOS_PROP_CO_EC_CELL_SZ;
	props->dpp_entries[3].dpe_val = 1 << 15;

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
	rc = daos_obj_open(ctx->coh, ctx->oid, DAOS_OO_RW, &ctx->oh, NULL);
	assert_success(rc);
}

static void
setup_single_recx_data(struct csum_test_ctx *ctx, char *seed_data,
		       daos_size_t data_bytes)
{
	int	rc;

	iov_alloc_str(&ctx->dkey, "dkey");
	iov_alloc_str(&ctx->update_iod.iod_name, "akey");

	rc = d_sgl_init(&ctx->update_sgl, 1);
	if (rc)
		return;
	iov_alloc(&ctx->update_sgl.sg_iovs[0], data_bytes);
	iov_update_fill(ctx->update_sgl.sg_iovs, seed_data, data_bytes);

	d_sgl_init(&ctx->fetch_sgl, 1);
	iov_alloc(&ctx->fetch_sgl.sg_iovs[0], data_bytes);

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
static void
setup_single_value_data(struct csum_test_ctx *ctx, char *seed_data,
		       daos_size_t data_bytes)
{
	int	rc;

	iov_alloc_str(&ctx->dkey, "dkey");
	iov_alloc_str(&ctx->update_iod.iod_name, "akey");

	rc = d_sgl_init(&ctx->update_sgl, 1);
	if (rc)
		return;
	iov_alloc(&ctx->update_sgl.sg_iovs[0], data_bytes);
	iov_update_fill(ctx->update_sgl.sg_iovs, seed_data, data_bytes);

	d_sgl_init(&ctx->fetch_sgl, 1);
	iov_alloc(&ctx->fetch_sgl.sg_iovs[0], data_bytes);

	ctx->update_iod.iod_size = daos_sgl_buf_size(&ctx->update_sgl);
	ctx->update_iod.iod_nr	= 1;
	ctx->update_iod.iod_type  = DAOS_IOD_SINGLE;

	/** Setup Fetch IOD*/
	ctx->fetch_iod.iod_name = ctx->update_iod.iod_name;
	ctx->fetch_iod.iod_size = ctx->update_iod.iod_size;
	ctx->fetch_iod.iod_recxs = ctx->update_iod.iod_recxs;
	ctx->fetch_iod.iod_nr = ctx->update_iod.iod_nr;
	ctx->fetch_iod.iod_type = ctx->update_iod.iod_type;
}

static void
setup_simple_data(struct csum_test_ctx *ctx)
{
	setup_single_recx_data(ctx, "0123456789", strlen("0123456789") + 1);
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

	iov_alloc_str(&ctx->dkey, "dkey");
	iov_alloc_str(&ctx->update_iod.iod_name, "akey_complex");

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
	int	rc;
	char	str[37];

	/** close object */
	rc = daos_obj_close(ctx->oh, NULL);
	assert_rc_equal(rc, 0);

	/** Close & Destroy Container */
	rc = daos_cont_close(ctx->coh, NULL);
	assert_rc_equal(rc, 0);
	uuid_unparse(ctx->uuid, str);
	rc = daos_cont_destroy(ctx->poh, str, true, NULL);
	assert_rc_equal(rc, 0);
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
	daos_oclass_id_t	 oc = dts_csum_oc;
	int			 rc;

	if (csum_ec_enabled() && !test_runable(*state, csum_ec_grp_size()))
		skip();

	/**
	 * Setup
	 */
	setup_from_test_args(&ctx, (test_arg_t *)*state);
	setup_simple_data(&ctx);
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_OFF, false, 0, oc);

	/**
	 * Act
	 */
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_success(rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_success(rc);
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
	daos_oclass_id_t	 oc = dts_csum_oc;
	int			 rc;

	FAULT_INJECTION_REQUIRED();

	if (csum_ec_enabled() && !test_runable(*state, csum_ec_grp_size()))
		skip();

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
	setup_cont_obj(&ctx, dts_csum_prop_type, false, 0, oc);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_rc_equal(rc, 0);
	cleanup_cont_obj(&ctx);

	/** 2. Server verify enabled, no corruption */
	setup_cont_obj(&ctx, dts_csum_prop_type, true, 0, oc);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_rc_equal(rc, 0);
	cleanup_cont_obj(&ctx);

	/** 3. Server verify disabled, corruption occurs, update should work */
	client_corrupt_on_update();
	setup_cont_obj(&ctx, dts_csum_prop_type, false, 0, oc);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_rc_equal(rc, 0);
	cleanup_cont_obj(&ctx);
	client_clear_fault();

	/** 4. Server verify enabled, corruption occurs, update should fail */
	client_corrupt_on_update();
	setup_cont_obj(&ctx, dts_csum_prop_type, true, 0, oc);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_rc_equal(rc, -DER_CSUM);
	cleanup_cont_obj(&ctx);
	client_clear_fault();

	cleanup_data(&ctx);
}

static void
test_server_data_corruption(void **state)
{
	test_arg_t		*arg = *state;
	struct csum_test_ctx	 ctx = {0};
	daos_oclass_id_t	 oc = dts_csum_oc;
	int			 rc;

	FAULT_INJECTION_REQUIRED();

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, dts_csum_prop_type, false, 1024*64, oc);

	/**1. Simple server data corruption after RDMA */
	setup_multiple_extent_data(&ctx);
	/** Set the Server data corruption flag */
	server_corrupt_disk(arg->group);
	/** Perform the update */
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			&ctx.update_iod, &ctx.update_sgl, NULL);
	assert_rc_equal(rc, 0);
	/** Clear the fail injection flag */
	server_clear_fault(arg->group);
	/** Fetch should result in checksum failure : SSD bad data*/
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_rc_equal(rc, -DER_CSUM);

	cleanup_cont_obj(&ctx);
	cleanup_data(&ctx);

}

static void
test_fetch_array(void **state)
{
	struct csum_test_ctx	ctx = {0};
	daos_oclass_id_t	oc = dts_csum_oc;
	uint32_t		node_nr;
	daos_iod_t		tmp_iod;
	int			rc;

	FAULT_INJECTION_REQUIRED();

	if (csum_ec_enabled() && !test_runable(*state, csum_ec_grp_size()))
		skip();

	/**
	 * Setup
	 */
	setup_from_test_args(&ctx, (test_arg_t *) *state);

	setup_cont_obj(&ctx, dts_csum_prop_type, false, 1024*8, oc);

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
	setup_simple_data(&ctx);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_rc_equal(rc, 0);
	tmp_iod = ctx.fetch_iod;
	tmp_iod.iod_size = DAOS_REC_ANY;
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1, &tmp_iod,
			    NULL, NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(tmp_iod.iod_size, 1);
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_rc_equal(rc, 0);
	/** Update/Fetch data matches */
	assert_memory_equal(ctx.update_sgl.sg_iovs->iov_buf,
			    ctx.fetch_sgl.sg_iovs->iov_buf,
			    ctx.update_sgl.sg_iovs->iov_buf_len);

	/** 2. Detect corruption - fetch again with fault injection enabled */
	client_corrupt_on_fetch();
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	if (csum_ec_enabled())
		assert_rc_equal(rc, 0);
	else
		assert_rc_equal(rc, -DER_CSUM);
	client_clear_fault();
	cleanup_data(&ctx);

	/** 3. Complicated data success case */
	setup_multiple_extent_data(&ctx);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_rc_equal(rc, 0);
	/** Update/Fetch data matches */
	assert_memory_equal(ctx.update_sgl.sg_iovs->iov_buf,
			    ctx.fetch_sgl.sg_iovs->iov_buf,
			    ctx.update_sgl.sg_iovs->iov_buf_len);

	/** 4. Complicated data with corruption */
	client_corrupt_on_fetch();
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	if (csum_ec_enabled())
		assert_rc_equal(rc, 0);
	else
		assert_rc_equal(rc, -DER_CSUM);
	client_clear_fault();

	/** verify the csum error can be tolerate */
	if (csum_ec_enabled()) {
		node_nr = csum_ec_grp_size();
	} else {
		node_nr = 2;
		oc = OC_RP_2GX;
	}
	if (test_runable(*state, node_nr)) {
		/** 5. Replicated object with corruption */
		cleanup_cont_obj(&ctx);
		setup_cont_obj(&ctx, dts_csum_prop_type, false, 1024 * 8, oc);
		rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				     &ctx.update_iod, &ctx.update_sgl, NULL);
		assert_rc_equal(rc, 0);
		client_corrupt_on_fetch();
		rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
		assert_rc_equal(rc, 0);
		/** Update/Fetch data matches */
		assert_memory_equal(ctx.update_sgl.sg_iovs->iov_buf,
				    ctx.fetch_sgl.sg_iovs->iov_buf,
				    ctx.update_sgl.sg_iovs->iov_buf_len);
		client_clear_fault();
		cleanup_data(&ctx);

		/** 6. Replicated (complicated data) object with corruption */
		daos_fail_loc_set(DAOS_DTX_COMMIT_SYNC | DAOS_FAIL_ALWAYS);
		setup_multiple_extent_data(&ctx);
		rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				     &ctx.update_iod, &ctx.update_sgl, NULL);
		assert_rc_equal(rc, 0);

		client_corrupt_on_fetch();
		rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
		assert_rc_equal(rc, 0);
		/** Update/Fetch data matches */
		assert_memory_equal(ctx.update_sgl.sg_iovs->iov_buf,
				    ctx.fetch_sgl.sg_iovs->iov_buf,
				    ctx.update_sgl.sg_iovs->iov_buf_len);
	}

	/** Clean up */
	client_clear_fault();
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

/**
 * -----------------------------------------
 * Partial Fetch & Unaligned Chunk tests
 * -----------------------------------------
 */

/** For defining an extent and data it represents in a test */
struct recx_config {
	uint64_t idx;
	uint64_t nr;
	char *data;
};
#define RECX_CONFIGS_NR 10
#define FETCH_RECX_NR 10
struct partial_unaligned_fetch_testcase_args {
	char			*dkey;
	char			*akey;
	uint32_t		 rec_size;
	uint32_t		 csum_prop_type;
	bool			 server_verify;
	size_t			 chunksize;
	struct recx_config	 recx_cfgs[RECX_CONFIGS_NR];
	daos_recx_t		 fetch_recxs[FETCH_RECX_NR];
	daos_oclass_id_t	 oclass;
};

static void
insert_data(const char *akey_format, const char *dkey, struct ioreq *req,
	    const uint32_t akey_loop, const uint32_t rec_loop, char *data_str,
	    daos_iod_type_t iod_type)
{
	char akey[32];
	int a, r;

	req->iod_type = iod_type;
	for (a = 0; a < akey_loop; a++) {
		sprintf(akey, akey_format, a);

		for (r = 0; r < rec_loop; r++) {
			insert_single(dkey, akey, r,
				      data_str,
				      strlen(data_str) + 1,
				      DAOS_TX_NONE, req);
		}
	}
}

static int
disabled_targets(test_arg_t *arg, d_rank_list_t **affected_engines)
{
	int			rc;
	daos_pool_info_t	info;

	/* Note: (info.pi_bits & DPI_ENGINES_ENABLED) == 0, so we get a list of engines with
	 * some (or all) of their targets disabled.
	 */
	rc = daos_pool_query(arg->pool.poh, affected_engines, &info, NULL, NULL);

	if (rc < 0)
		return rc;

	return info.pi_ndisabled;
}


static void
setup_obj_data_for_sv(struct csum_test_ctx *ctx, bool large_buf)
{
	uint32_t	repeat = large_buf ? 1024 : 1;

	d_iov_set(&ctx->dkey, "dkey", strlen("dkey"));

	d_iov_set(&ctx->update_iod.iod_name, "akey", strlen("akey"));

	/** setup the buffers for update & fetch */
	dts_sgl_init_with_strings_repeat(&ctx->update_sgl, repeat, 1,
					 "ABCDEFGHIJKLMNOP");

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
static void
iov_update_fill(d_iov_t *iov, char *data, uint64_t len_to_fill)
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
	daos_oclass_id_t	oc = dts_csum_oc;
	uint32_t		rec_size = args->rec_size;
	int			recx_count = 0;
	uint32_t		fetch_recx_nr;
	daos_size_t		total_fetch_bytes;
	size_t			max_data_size = 0;
	int			rc;
	int			i;

	if (args->oclass != OC_UNKNOWN)
		oc = args->oclass;

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

	fetch_recx_nr = 0;
	total_fetch_bytes = 0;
	while (fetch_recx_nr < FETCH_RECX_NR &&
	       args->fetch_recxs[fetch_recx_nr].rx_nr > 0) {
		total_fetch_bytes +=
			args->fetch_recxs[fetch_recx_nr].rx_nr * rec_size;
		fetch_recx_nr++;
	}

	/** setup the buffers for update & fetch */
	d_sgl_init(&ctx.update_sgl, 1);
	iov_alloc(&ctx.update_sgl.sg_iovs[0], max_data_size);

	d_sgl_init(&ctx.fetch_sgl, 1);
	iov_alloc(&ctx.fetch_sgl.sg_iovs[0], total_fetch_bytes);

	/** Setup Update IOD */
	ctx.update_iod.iod_size = rec_size;
	/** These test cases always use 1 recx at a time */
	ctx.update_iod.iod_nr	= 1;
	ctx.update_iod.iod_recxs = ctx.recx;
	ctx.update_iod.iod_type  = DAOS_IOD_ARRAY;

	/** Setup Fetch IOD*/
	ctx.fetch_iod.iod_name = ctx.update_iod.iod_name;
	ctx.fetch_iod.iod_size = ctx.update_iod.iod_size;
	ctx.fetch_iod.iod_recxs = args->fetch_recxs;
	ctx.fetch_iod.iod_nr = fetch_recx_nr;
	ctx.fetch_iod.iod_type = ctx.update_iod.iod_type;

	setup_from_test_args(&ctx, test_arg);
	setup_cont_obj(&ctx, args->csum_prop_type, args->server_verify,
		       args->chunksize, oc);

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
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 8,
		.recx_cfgs = {
			{.idx = 0, .nr = 1024, .data = "A"},
		},
		.fetch_recxs = { {.rx_idx = 2, .rx_nr = 8} },
	});

	/** Extents not aligned with chunksize */
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 2,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 0, .nr = 3, .data = "ABC"},
			{.idx = 1, .nr = 2, .data = "B"},
		},
		.fetch_recxs = { {.rx_idx = 0, .rx_nr = 3} },
	});

	/** Heavily overlapping extents broken up into many chunks */
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 8,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 2, .nr = 510, .data = "ABCDEFG"},
			{.idx = 0, .nr = 512, .data = "1234567890"},
		},
		.fetch_recxs = { {.rx_idx = 0, .rx_nr = 511} },
	});

	/** Extents with small overlap */
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 1024,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 2, .nr = 512, .data = "A"},
			{.idx = 500, .nr = 512, .data = "B"},
		},
		.fetch_recxs = { {.rx_idx = 2, .rx_nr = 1012} },
	});

	/** several smallish extents within a single chunk */
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 1024 * 32,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 8,
		.recx_cfgs = {
			{.idx = 2, .nr = 512, .data = "A"},
			{.idx = 500, .nr = 512, .data = "B"},
			{.idx = 1000, .nr = 512, .data = "C"},
			{.idx = 1500, .nr = 512, .data = "D"},
		},
		.fetch_recxs = { {.rx_idx = 2, .rx_nr = 800} },
	});
}

static void
overwrites_after_first_chunk(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 32,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 4,
		.recx_cfgs = {
			{.idx = 8, .nr = 2, .data = "B"},
			{.idx = 9, .nr = 2, .data = "C"},
		},
		.fetch_recxs = { {.rx_idx = 8, .rx_nr = 3} },
	});
}

static void
unaligned_record_size(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 4,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 3,
		.recx_cfgs = {
			{.idx = 8, .nr = 5, .data = "B"},
		},
		.fetch_recxs = { {.rx_idx = 8, .rx_nr = 2} },
	});
}

static void
record_size_larger_than_chunksize(void **state)
{
	/** Overwrites after the first chunk */
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 4,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 20,
		.recx_cfgs = {
			{.idx = 0, .nr = 100, .data = "A"},
		},
		.fetch_recxs = { {.rx_idx = 0, .rx_nr = 100} },
	});
}

static void
larger_record_size_with_second_chunk_half_first(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 64,
		.csum_prop_type = DAOS_PROP_CO_CSUM_CRC32,
		.server_verify = false,
		.rec_size = 2,
		.recx_cfgs = {
			{.idx = 0, .nr = 64 * 2, .data = "One"},
			{.idx = 0, .nr = 64, .data = "Six"},
		},
		.fetch_recxs = { {.rx_idx = 0, .rx_nr = 64 * 2} },
	});
}

static void
overlapping_after_first_chunk(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 4,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 0, .nr = 8, .data = "12345678"},
			{.idx = 0, .nr = 4, .data = "ABCD"},
		},
		.fetch_recxs = { {.rx_idx = 0, .rx_nr = 8} },
	});
}

static void
request_second_half_of_chunk(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 1024,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 0, .nr = 1024, .data = "12345678"},
		},
		.fetch_recxs = { {.rx_idx = 512, .rx_nr = 512} },
	});
}

static void
extents_with_holes_1(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 1024 * 32,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 0, .nr = 6, .data = "Y"},
			{.idx = 10, .nr = 10, .data = "Z"},
		},
		.fetch_recxs = { {.rx_idx = 0, .rx_nr = 20} },
	});
}

static void
extents_with_holes_2(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 1024 * 32,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 2, .nr = 6, .data = "Y"},
			{.idx = 10, .nr = 6, .data = "Z"},
		},
		.fetch_recxs = { {.rx_idx = 0, .rx_nr = 20} },
	});
}

static void
extents_with_holes_3(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 8,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 8, .nr = 6, .data = "Y"},
			{.idx = 18, .nr = 6, .data = "Z"},
		},
		.fetch_recxs = { {.rx_idx = 0, .rx_nr = 23} },
	});
}

static void
extents_with_holes_4(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 100,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 8, .nr = 1, .data = "A"},
			{.idx = 15, .nr = 1, .data = "A"},
			{.idx = 25, .nr = 1, .data = "A"},
			{.idx = 30, .nr = 1, .data = "A"},
			{.idx = 35, .nr = 1, .data = "A"},
		},
		.fetch_recxs = { {.rx_idx = 0, .rx_nr = 40} },
	});
}

static void
extents_with_holes_5(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 4,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 8, .nr = 1, .data = "A"},
			{.idx = 100, .nr = 1, .data = "A"},
		},
		.fetch_recxs = { {.rx_idx = 0, .rx_nr = 200} },
	});
}

static void
extents_with_holes_6(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 256,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 8,
		.recx_cfgs = {
			{.idx = 8, .nr = 4, .data = "A"},
			{.idx = 20, .nr = 3, .data = "A"},
			{.idx = 119, .nr = 5, .data = "A"},
			{.idx = 200, .nr = 1, .data = "A"},
			{.idx = 208, .nr = 1, .data = "A"},
			{.idx = 212, .nr = 1, .data = "A"},
		},
		.fetch_recxs = { {.rx_idx = 0, .rx_nr = 250} },
	});
}

static void
extents_with_holes_7(void **state)
{
	struct csum_test_ctx	ctx = {0};
	daos_oclass_id_t	oc = dts_csum_oc;
	uint32_t		rec_size = 1;
	size_t			data_size = 100;
	int			rc;
	int chunksize = 1024;

	d_iov_set(&ctx.dkey, "dkey", strlen("dkey"));
	d_iov_set(&ctx.update_iod.iod_name, "akey", strlen("akey"));


	/** setup the buffers for update & fetch */
	d_sgl_init(&ctx.update_sgl, 1);
	iov_alloc(&ctx.update_sgl.sg_iovs[0], data_size);

	d_sgl_init(&ctx.fetch_sgl, 1);
	iov_alloc(&ctx.fetch_sgl.sg_iovs[0], data_size);

	/** Setup Update IOD */
	ctx.update_iod.iod_size = rec_size;
	/** These test cases always use 1 recx at a time */
	ctx.update_iod.iod_nr	= 1;
	ctx.update_iod.iod_recxs = ctx.recx;
	ctx.update_iod.iod_type  = DAOS_IOD_ARRAY;

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, dts_csum_prop_type, false, chunksize, oc);

	ctx.recx[0].rx_idx = 0;
	ctx.recx->rx_nr = data_size;

	iov_update_fill(&ctx.update_sgl.sg_iovs[0], "A", data_size);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_success(rc);

	/** punch a hole in the middle */
	ctx.update_iod.iod_size = 0;
	ctx.update_sgl.sg_nr = 0;
	ctx.recx[0].rx_idx = data_size / 2;
	ctx.recx[0].rx_nr = 10;

	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_success(rc);

	/** Setup Fetch IOD*/
	ctx.fetch_iod.iod_size = 1;
	ctx.fetch_iod.iod_nr = 1;
	ctx.fetch_iod.iod_name = ctx.update_iod.iod_name;
	ctx.recx[0] = (daos_recx_t){ .rx_idx = 0, .rx_nr = data_size };
	ctx.fetch_iod.iod_recxs = ctx.recx;
	ctx.fetch_iod.iod_type = DAOS_IOD_ARRAY;

	assert_success(daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL));

	/** Clean up */
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

static void
extents_in_reverse_order(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 1024 * 32,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 4,
		.recx_cfgs = {
			{.idx = 99, .nr = 1, .data = "Y"},
			{.idx = 98, .nr = 1, .data = "Y"},
			{.idx = 97, .nr = 1, .data = "Y"},
			{.idx = 96, .nr = 1, .data = "Y"},
			{.idx = 95, .nr = 1, .data = "Y"},
			{.idx = 94, .nr = 1, .data = "Y"},
			{.idx = 93, .nr = 1, .data = "Y"},
			{.idx = 92, .nr = 1, .data = "Y"},
			{.idx = 91, .nr = 1, .data = "Y"},
		},
		.fetch_recxs = {
			{.rx_idx = 99, .rx_nr = 1},
			{.rx_idx = 91, .rx_nr = 8},
		},
	});
}

static void
single_value_test(void **state, bool large_buf)
{
	struct csum_test_ctx	ctx = {0};
	daos_oclass_id_t	oc = dts_csum_oc;
	daos_iod_t		tmp_iod;
	int			rc;

	setup_from_test_args(&ctx, *state);

	setup_cont_obj(&ctx, dts_csum_prop_type, false, 4, oc);
	setup_obj_data_for_sv(&ctx, large_buf);

	/** Base case ... no fault injection */
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_rc_equal(0, rc);

	tmp_iod = ctx.fetch_iod;
	tmp_iod.iod_size = DAOS_REC_ANY;
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1, &tmp_iod,
			    NULL, NULL, NULL);
	assert_rc_equal(0, rc);
	D_ASSERT(tmp_iod.iod_size >= 1);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_rc_equal(0, rc);

	/**
	 * fault injection on update
	 * - update will succeed because server side verification is disabled
	 * - fetch will fail because data was corrupted on update
	 */
	client_corrupt_on_update();
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_rc_equal(0, rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	/* above client_corrupt_on_update only corrupt first shard's update, for
	 * large_buf singv it is evenly distributed to multiple shard and one
	 * shard's csum error can be recovered.
	 */
	if (csum_ec_enabled() && large_buf)
		assert_rc_equal(0, rc);
	else
		assert_rc_equal(-DER_CSUM, rc);
	client_clear_fault();

	/**
	 * fault injection on fetch
	 * - update will succeed
	 * - fetch will fail because data was corrupted on fetch
	 */
	client_corrupt_on_fetch();
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_rc_equal(0, rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	if (csum_ec_enabled())
		assert_rc_equal(0, rc);
	else
		assert_rc_equal(-DER_CSUM, rc);
	client_clear_fault();

	/** Reset the container with server side verification enabled */
	cleanup_cont_obj(&ctx);
	setup_cont_obj(&ctx, dts_csum_prop_type, true, 4, oc);

	/**
	 * fault injection on update
	 * - update will fail because server side verification is enabled
	 * - fetch will not get data
	 */
	client_corrupt_on_update();
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_rc_equal(-DER_CSUM, rc);

	memset(ctx.fetch_sgl.sg_iovs->iov_buf, 0,
	       ctx.fetch_sgl.sg_iovs->iov_buf_len);
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_rc_equal(0, rc);
	assert_string_equal("", (char *)ctx.fetch_sgl.sg_iovs->iov_buf);

	client_clear_fault();

	/** Clean up */
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

static void
single_value(void **state)
{
	FAULT_INJECTION_REQUIRED();

	if (csum_ec_enabled() && !test_runable(*state, csum_ec_grp_size()))
		skip();

	print_message("test small single-value\n");
	single_value_test(state, false);
	print_message("test large single-value\n");
	single_value_test(state, true);
};

static void
dtx_with_csum(void **state)
{
	test_arg_t		*arg = *state;
	struct csum_test_ctx	 ctx = { 0 };
	daos_handle_t		 th = { 0 };
	daos_oclass_id_t	 oc = dts_csum_oc;
	int			 rc;

	if (csum_ec_enabled() && !test_runable(arg, csum_ec_grp_size()))
		skip();

	setup_from_test_args(&ctx, arg);
	setup_simple_data(&ctx);

	/** Server verify enabled, no corruption. */
	setup_cont_obj(&ctx, dts_csum_prop_type, true, 0, oc);
	rc = daos_tx_open(ctx.coh, &th, 0, NULL);
	assert_rc_equal(rc, 0);

	daos_obj_update(ctx.oh, th, 0, &ctx.dkey, 1, &ctx.update_iod,
			&ctx.update_sgl, NULL);
	rc = daos_tx_commit(th, NULL);
	assert_rc_equal(rc, 0);

	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

static void
mix_test(void **state)
{
	struct csum_test_ctx	ctx = {0};
	daos_oclass_id_t	oc = dts_csum_oc;
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

	FAULT_INJECTION_REQUIRED();

	setup_from_test_args(&ctx, *state);

	setup_cont_obj(&ctx, dts_csum_prop_type, false, 4, oc);

	iov_alloc_str(&dkey, "dkey");

	/** setup single value iod */
	dts_sgl_init_with_strings(sv_sgl, 1,
		"This is for a single value");
	dts_sgl_init_with_strings(fetch_sv_sgl, 1,
		"XXXXXXXXXXXXXXXXXXXXXXXXXX");
	iov_alloc_str(&sv_iod->iod_name, "single value akey");
	sv_iod->iod_type = DAOS_IOD_SINGLE;
	sv_iod->iod_size = daos_sgl_buf_size(sv_sgl);
	sv_iod->iod_nr = 1;

	/** setup array value iod */
	dts_sgl_init_with_strings(array_sgl, 1,
		"This is for an array value");
	dts_sgl_init_with_strings(fetch_array_sgl, 1,
		"XXXXXXXXXXXXXXXXXXXXXXXXXX");
	iov_alloc_str(&array_iod->iod_name, "array value akey");
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
	assert_rc_equal(0, rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			    iods, fetch_sgls, NULL, NULL);
	assert_rc_equal(0, rc);

	/**
	 * fault injection on update
	 * - update will succeed because server side verification is disabled
	 * - fetch will fail because data was corrupted on update
	 */
	client_corrupt_on_update();
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			     iods, sgls, NULL);
	assert_rc_equal(0, rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			    iods, fetch_sgls, NULL, NULL);
	assert_rc_equal(-DER_CSUM, rc);
	client_clear_fault();

	/**
	 * fault injection on fetch
	 * - update will succeed
	 * - fetch will fail because data was corrupted on fetch
	 */
	client_corrupt_on_fetch();
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			     iods, sgls, NULL);
	assert_rc_equal(0, rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			    iods, fetch_sgls, NULL, NULL);
	assert_rc_equal(-DER_CSUM, rc);
	client_clear_fault();

	/** Reset the container with server side verification enabled */
	cleanup_cont_obj(&ctx);
	setup_cont_obj(&ctx, dts_csum_prop_type, true, 4, oc);

	/**
	 * fault injection on update
	 * - update will fail because server side verification is enabled
	 * - fetch will not get data
	 */
	client_corrupt_on_update();
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			     iods, sgls, NULL);
	assert_rc_equal(-DER_CSUM, rc);

	memset(fetch_sv_sgl->sg_iovs->iov_buf, 0,
	       fetch_sv_sgl->sg_iovs->iov_buf_len);
	memset(fetch_array_sgl->sg_iovs->iov_buf, 0,
	       fetch_array_sgl->sg_iovs->iov_buf_len);
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &dkey, 2,
			    iods, fetch_sgls, NULL, NULL);
	assert_rc_equal(0, rc);
	assert_string_equal("", (char *)fetch_sv_sgl->sg_iovs->iov_buf);
	assert_string_equal("", (char *)fetch_array_sgl->sg_iovs->iov_buf);

	client_clear_fault();

	/** Clean up */
	d_sgl_fini(sv_sgl, true);
	d_sgl_fini(array_sgl, true);
	d_sgl_fini(fetch_sv_sgl, true);
	d_sgl_fini(fetch_array_sgl, true);
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}


static void
key_csum_fetch_update(void **state, int update_fi_flag, int fetch_fi_flag)
{
	struct csum_test_ctx	ctx;
	daos_oclass_id_t	oc = dts_csum_oc;
	int			rc;

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, dts_csum_prop_type, false, 1024, oc);
	setup_simple_data(&ctx);

	/**
	 * When a key is corrupted, the server should catch it and return
	 * error
	 */
	set_fi(update_fi_flag);
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_rc_equal(-DER_CSUM, rc);
	client_clear_fault();

	/**
	 * Sanity check that with failure injection disabled update still
	 * works
	 */
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_rc_equal(0, rc);

	/**
	 * When a key is corrupted, the server should catch it and return
	 * error
	 */
	set_fi(fetch_fi_flag);
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_rc_equal(-DER_CSUM, rc);
	client_clear_fault();

	/**
	 * Sanity check that with failure injection disabled fetch still
	 * works
	 */
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_rc_equal(0, rc);

	/** Clean up */
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

static void
many_iovs_with_single_values(void **state)
{
#define	AKEY_NR  5
	d_sg_list_t		sgls[AKEY_NR];
	d_iov_t			sg_iovs[AKEY_NR];
	daos_iod_t		iods[AKEY_NR];
	uint64_t		value_1 = 1;
	uint16_t		value_2 = 2;
	uint16_t		value_3 = 3;
	daos_size_t		value_4 = 4;
	daos_oclass_id_t	value_5 = 5;
	int			i, rc;

	struct csum_test_ctx	ctx;
	daos_oclass_id_t	oc = dts_csum_oc;

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, dts_csum_prop_type, false, 1024, oc);
	setup_simple_data(&ctx);

	i = 0;
	d_iov_set(&sg_iovs[i], &value_1, sizeof(value_1));
	d_iov_set(&iods[i].iod_name, "AKEY_1", strlen("AKEY_1"));
	i++;

	d_iov_set(&sg_iovs[i], &value_2, sizeof(value_2));
	d_iov_set(&iods[i].iod_name, "AKEY_2", strlen("AKEY_2"));
	i++;

	d_iov_set(&sg_iovs[i], &value_3, sizeof(value_3));
	d_iov_set(&iods[i].iod_name, "AKEY_3", strlen("AKEY_3"));
	i++;

	d_iov_set(&sg_iovs[i], &value_4, sizeof(value_4));
	d_iov_set(&iods[i].iod_name, "AKEY_4", strlen("AKEY_4"));
	i++;

	d_iov_set(&sg_iovs[i], &value_5, sizeof(value_5));
	d_iov_set(&iods[i].iod_name, "AKEY_5", strlen("AKEY_5"));
	i++;

	iods[0].iod_size = sizeof(value_1);
	iods[1].iod_size = sizeof(value_2);
	iods[2].iod_size = sizeof(value_3);
	iods[3].iod_size = sizeof(value_4);
	iods[4].iod_size = sizeof(value_5);

	for (i = 0; i < AKEY_NR; i++) {
		sgls[i].sg_nr		= 1;
		sgls[i].sg_nr_out	= 0;
		sgls[i].sg_iovs		= &sg_iovs[i];

		iods[i].iod_nr		= 1;
		iods[i].iod_recxs	= NULL;
		iods[i].iod_type	= DAOS_IOD_SINGLE;
	}

	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, DAOS_COND_DKEY_INSERT,
			     &ctx.dkey, AKEY_NR, iods, sgls, NULL);
	assert_rc_equal(0, rc);

	value_1 = 0;
	value_2 = 0;
	value_3 = 0;
	value_4 = 0;
	value_5 = 0;

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey,
			    AKEY_NR, iods, sgls, NULL, NULL);
	assert_rc_equal(0, rc);

	/** Clean up */
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

static void
two_iods_two_recxs(void **state)
{
	struct csum_test_ctx	ctx;
	daos_oclass_id_t	oc = dts_csum_oc;
	d_sg_list_t		sgls[2] = {0};
	daos_iod_t		iods[2] = {0};
	daos_recx_t		recxs[10] = {0};

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, dts_csum_prop_type, false, 1024 * 32, oc);

	iov_alloc_str(&ctx.dkey, "dkey");

	/** Setup the first iod/sgl to have multiple recxs */
	d_sgl_init(&sgls[0], 1);
	iov_alloc(&sgls[0].sg_iovs[0], 6);
	iov_update_fill(&sgls[0].sg_iovs[0], "1", 6);

	iov_alloc_str(&iods[0].iod_name, "akey_one");
	iods[0].iod_nr = 2;
	iods[0].iod_size = daos_sgl_buf_size(&sgls[0]) / 2;
	iods[0].iod_recxs = &recxs[0];
	iods[0].iod_recxs[0].rx_idx = 0;
	iods[0].iod_recxs[0].rx_nr = 1;
	iods[0].iod_recxs[1].rx_idx = 1;
	iods[0].iod_recxs[1].rx_nr = 1;
	iods[0].iod_type = DAOS_IOD_ARRAY;

	/** setup the second iod/sgl to be a */
	d_sgl_init(&sgls[1], 1);
	iov_alloc(&sgls[1].sg_iovs[0], 6);
	iov_update_fill(&sgls[1].sg_iovs[0], "2", 6);

	iov_alloc_str(&iods[1].iod_name, "akey_two");
	iods[1].iod_nr = 2;
	iods[1].iod_size = daos_sgl_buf_size(&sgls[1]) / 2;
	iods[1].iod_recxs = &recxs[2];
	iods[1].iod_recxs[0].rx_idx = 0;
	iods[1].iod_recxs[0].rx_nr = 1;
	iods[1].iod_recxs[1].rx_idx = 1;
	iods[1].iod_recxs[1].rx_nr = 1;
	iods[1].iod_type = DAOS_IOD_ARRAY;

	assert_success(daos_obj_update(ctx.oh, DAOS_TX_NONE,
				       DAOS_COND_DKEY_INSERT, &ctx.dkey, 2,
				       iods, sgls, NULL));

	assert_success(daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 2,
				      iods, sgls, NULL, NULL));

	/** Clean up */
	cleanup_cont_obj(&ctx);
}

static void
request_non_existent_data(void **state)
{
	int			rc;
	struct csum_test_ctx	ctx;

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, dts_csum_prop_type, false, 1024, dts_csum_oc);
	setup_simple_data(&ctx);

	ctx.update_iod.iod_recxs[0].rx_idx = 1;
	ctx.update_iod.iod_recxs[0].rx_nr = 1;

	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, DAOS_COND_DKEY_INSERT,
			     &ctx.dkey, 1, &ctx.update_iod,
			     &ctx.update_sgl, NULL);
	assert_success(rc);

	ctx.fetch_iod.iod_recxs[0].rx_idx = 0;
	ctx.fetch_iod.iod_recxs[0].rx_nr = 1;
	ctx.fetch_iod.iod_recxs[1].rx_idx = 1;
	ctx.fetch_iod.iod_recxs[1].rx_nr = 1;
	ctx.fetch_iod.iod_recxs[2].rx_idx = 3;
	ctx.fetch_iod.iod_recxs[2].rx_nr = 1;
	ctx.fetch_iod.iod_recxs[3].rx_idx = 4;
	ctx.fetch_iod.iod_recxs[3].rx_nr = 1;
	ctx.fetch_iod.iod_nr = 4;

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey,
			    1, &ctx.fetch_iod,
			    &ctx.fetch_sgl, NULL, NULL);
	assert_success(rc);

	/** Clean up */
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

static void
unaligned_hole_at_beginning(void **state)
{
	daos_size_t chunksize = 8;

	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = chunksize,
		.csum_prop_type = DAOS_PROP_CO_CSUM_CRC32,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = chunksize, .nr = chunksize, .data = "Two"},
		},
		.fetch_recxs = { {.rx_idx = 1, .rx_nr = chunksize} },
	});
}

static void
bug_rounding_error(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 65536,
		.csum_prop_type = DAOS_PROP_CO_CSUM_CRC32,
		.server_verify = false,
		.rec_size = 16,
		.recx_cfgs = {
			{.idx = 931011156, .nr = 752674,
				.data = "Lorem ipsum dolor sit amet, consect"},
			{.idx = 931011156, .nr = 695491,
				.data = "adipiscing elit, sed do eiusmod temp"},
		},
		.fetch_recxs = { {.rx_idx = 931011156, .rx_nr = 943009} },
	});
}

static void
request_is_after_extent_start(void **state)
{
	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.chunksize = 32768,
		.csum_prop_type = DAOS_PROP_CO_CSUM_CRC32,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 874741704, .nr = 316950,
				.data = "in reprehenderit in voluptate velit"},
		},
		.fetch_recxs = { {.rx_idx = 874939430, .rx_nr = 168688} },
	});
}

static bool
rank_in_placement(uint32_t rank, struct daos_obj_layout *placement)
{
	int s, r;

	for (s = 0; s < placement->ol_nr; s++) {
		for (r = 0; r < placement->ol_shards[s]->os_replica_nr; r++) {
			if (rank ==
			    placement->ol_shards[s]->os_shard_loc[r].sd_rank)
				return true;
		}
	}
	return false;
}

static int
get_rank_not_in_placement(struct daos_obj_layout *placement,
				   struct daos_obj_layout *not_in_placement)
{
	int s, r;

	for (s = 0; s < placement->ol_nr; s++) {
		struct daos_obj_shard *shard = placement->ol_shards[s];

		for (r = 0; r < shard->os_replica_nr; r++) {
			uint32_t rank = shard->os_shard_loc[r].sd_rank;

			if (!rank_in_placement(rank, not_in_placement))
				return rank;
		}
	}

	return -1;
}

static void
rebuild_test(void **state, int chunksize, int data_len_bytes, int iod_type)
{
	struct csum_test_ctx	 ctx;
	test_arg_t		*arg = *state;
	struct daos_obj_layout *layout1 = NULL;
	struct daos_obj_layout *layout2 = NULL;
	uint32_t		 rank_to_exclude;
	int			 rank_to_fetch;
	uint32_t		 disabled_nr, after_disabled_nr;
	d_rank_list_t		*affected_engines = NULL;
	int			 rc;

	if (!test_runable(*state, 3))
		skip();

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, chunksize,
		       DAOS_OC_R2S_SPEC_RANK);

	if (iod_type == DAOS_IOD_ARRAY)
		setup_single_recx_data(&ctx, "abc", data_len_bytes);
	else if (iod_type == DAOS_IOD_SINGLE)
		setup_single_value_data(&ctx, "abc", data_len_bytes);
	else
		fail_msg("Invalid iod_type: %d\n", iod_type);

	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_success(rc);

	/** insert another overlapping extent */
	if (iod_type == DAOS_IOD_ARRAY) {
		ctx.recx->rx_idx += data_len_bytes / 2;
		ctx.recx->rx_nr -= data_len_bytes / 2;
		rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				     &ctx.update_iod, &ctx.update_sgl, NULL);
		assert_success(rc);
	}

	rc = daos_obj_layout_get(ctx.coh, ctx.oid, &layout1);
	assert_success(rc);
	print_message("Before rebuild: Object replicated across ranks %d, %d\n",
		      layout1->ol_shards[0]->os_shard_loc[0].sd_rank,
		      layout1->ol_shards[0]->os_shard_loc[1].sd_rank);

	rank_to_exclude = layout1->ol_shards[0]->os_shard_loc[0].sd_rank;
	print_message("Excluding rank %d\n", rank_to_exclude);
	disabled_nr = disabled_targets(arg, NULL /* affected_engines */);
	rc = dmg_pool_exclude(arg->dmg_config, arg->pool.pool_uuid, arg->group,
			      layout1->ol_shards[0]->os_shard_loc[0].sd_rank, -1);
	assert_success(rc);
	after_disabled_nr = disabled_targets(arg, &affected_engines);
	assert_true(disabled_nr < after_disabled_nr);
	assert_true(d_rank_list_find(affected_engines, rank_to_exclude, NULL));
	assert_int_equal(affected_engines->rl_nr, 1);
	d_rank_list_free(affected_engines);

	/** wait for rebuild */
	test_rebuild_wait(&arg, 1);

	daos_cont_status_clear(arg->coh, NULL);

	rc = daos_obj_layout_get(ctx.coh, ctx.oid, &layout2);
	assert_success(rc);

	print_message("After rebuild: Object replicated across ranks %d, %d\n",
		      layout2->ol_shards[0]->os_shard_loc[0].sd_rank,
		      layout2->ol_shards[0]->os_shard_loc[1].sd_rank);

	/** force to fetch from rank that was rebuilt to ensure checksum
	 * was rebuilt appropriately
	 */
	rank_to_fetch = get_rank_not_in_placement(layout2, layout1);
	assert_true(rank_to_fetch >= 0);
	print_message("Rank to fetch: %d\n", rank_to_fetch);

	daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD | DAOS_FAIL_ALWAYS);
	daos_fail_num_set(rank_to_fetch);
	daos_cont_status_clear(ctx.coh, NULL);
	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey,
			    1, &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_success(rc);
	daos_fail_loc_reset();
	daos_fail_num_set(0);

	rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group,
				  rank_to_exclude, -1);
	assert_success(rc);
	after_disabled_nr = disabled_targets(arg, NULL);
	assert_int_equal(disabled_nr, after_disabled_nr);
	/** wait for rebuild */
	test_rebuild_wait(&arg, 1);

	daos_obj_layout_free(layout1);
	daos_obj_layout_free(layout2);

	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

int
tst_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	      unsigned int nr, daos_iod_t *iods, d_sg_list_t *sgls,
	      daos_iom_t *maps, uint32_t extra_flag, void *extra_arg,
	      d_iov_t *csum)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_fetch_task_create(oh, DAOS_TX_NONE, 0, dkey, nr, extra_flag,
				      iods, sgls, maps, extra_arg, csum, NULL,
				      NULL, &task);

	assert_rc_equal(0, rc);

	return dc_task_schedule(task, true);
}

static void
ctx_obj_update(struct csum_test_ctx *ctx)
{
	int rc = daos_obj_update(ctx->oh, DAOS_TX_NONE, 0, &ctx->dkey, 1,
				 &ctx->update_iod, &ctx->update_sgl, NULL);


	assert_success(rc);
}

static void
test_fetch_task_api(void **state)
{
	struct csum_test_ctx	 ctx;
	struct dcs_csum_info	*csum_info = NULL;
	int			 rc;

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, 1024 * 32, OC_SX);
	setup_single_recx_data(&ctx, "abc", 1024);

	ctx_obj_update(&ctx);

	d_iov_t iov = {0};

	rc = tst_obj_fetch(ctx.oh, 1, &ctx.dkey, 1, &ctx.fetch_iod,
			   &ctx.fetch_sgl, NULL, 0, NULL, &iov);
	/** Should still succeed, even with an iov that doesn't have a buffer */
	assert_success(rc);

	/** fetch should have set the length to what's needed */
	assert_true(iov.iov_len > 0);

	iov_alloc(&iov, iov.iov_len);
	iov.iov_len = 0;

	/** fetch again with an allocated buffer */
	rc = tst_obj_fetch(ctx.oh, 1, &ctx.dkey, 1, &ctx.fetch_iod,
			   &ctx.fetch_sgl, NULL, 0, NULL, &iov);
	assert_success(rc);

	ci_cast(&csum_info, &iov);
	assert_int_equal(1, csum_info->cs_nr);
	assert_int_equal(8, csum_info->cs_len);

	daos_iov_free(&iov);

	cleanup_cont_obj(&ctx);
}

#define	INLINE_DATA	10
#define	FETCHED_DATA	1024
#define	BULK_DATA	(1024 * 32)
#define CHUNK_SIZE	(1024 * 32)

/** Test rebuild enumerating objects and getting data inline */
static void
rebuild_1(void **state)
{
	rebuild_test(state, CHUNK_SIZE, INLINE_DATA, DAOS_IOD_ARRAY);
}

/** Test rebuild when data is fetched after enumeration */
static void
rebuild_2(void **state)
{
	rebuild_test(state, CHUNK_SIZE, FETCHED_DATA, DAOS_IOD_ARRAY);
}

/** Test rebuild when data is bulk transferred */
static void
rebuild_3(void **state)
{
	rebuild_test(state, CHUNK_SIZE, BULK_DATA, DAOS_IOD_ARRAY);
}

static void
rebuild_4(void **state)
{
	rebuild_test(state, CHUNK_SIZE, INLINE_DATA, DAOS_IOD_SINGLE);

}

/** Test rebuild when data is fetched after enumeration */
static void
rebuild_5(void **state)
{
	rebuild_test(state, CHUNK_SIZE, FETCHED_DATA, DAOS_IOD_SINGLE);
}

/** Test rebuild when data is bulk transferred */
static void
rebuild_6(void **state)
{
	rebuild_test(state, CHUNK_SIZE, BULK_DATA, DAOS_IOD_SINGLE);
}

static void
punch_before_insert(void **state)
{
	struct csum_test_ctx	 ctx;
	int			 rc;

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, 1024 * 32, OC_SX);

	setup_single_recx_data(&ctx, "abc", 25);

	ctx.update_iod.iod_size = 0;
	ctx.update_sgl.sg_iovs->iov_len = 0;
	ctx.update_sgl.sg_iovs->iov_buf_len = 0;
	ctx.update_sgl.sg_iovs->iov_buf = NULL;

	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_success(rc);
	setup_single_recx_data(&ctx, "abc", 25);

	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_success(rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_success(rc);

	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

static void
test_update_fetch_a_key(void **state)
{
	FAULT_INJECTION_REQUIRED();

	key_csum_fetch_update(state,
			      DAOS_CSUM_CORRUPT_UPDATE_AKEY,
			      DAOS_CSUM_CORRUPT_FETCH_AKEY);
}

static void
test_update_fetch_d_key(void **state)
{
	FAULT_INJECTION_REQUIRED();

	key_csum_fetch_update(state,
			      DAOS_CSUM_CORRUPT_UPDATE_DKEY,
			      DAOS_CSUM_CORRUPT_FETCH_DKEY);

}

#define KDS_NR 10
static void
test_enumerate_a_key(void **state)
{
	struct csum_test_ctx	ctx = {0};
	daos_oclass_id_t	oc = dts_csum_oc;
	int			rc;
	uint32_t		i;
	daos_anchor_t		anchor = {0};
	daos_key_desc_t		kds[KDS_NR] = {0};
	d_sg_list_t		sgl = {0};
	uint32_t		nr = KDS_NR;

	FAULT_INJECTION_REQUIRED();

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, dts_csum_prop_type, false, 1024, oc);
	setup_simple_data(&ctx);

	/** insert multiple keys to enumerate */
	for (i = 0; i < KDS_NR; i++) {
		rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				     &ctx.update_iod, &ctx.update_sgl,
				     NULL);
		assert_rc_equal(0, rc);
		((uint8_t *)ctx.update_iod.iod_name.iov_buf)[0] += 1;
	}

	d_sgl_init(&sgl, 1);
	iov_alloc(&sgl.sg_iovs[0], 100);

	/** inject failure ... should return CSUM error */
	client_corrupt_akey_on_fetch();
	rc = daos_obj_list_akey(ctx.oh, DAOS_TX_NONE, &ctx.dkey, &nr, kds, &sgl,
				&anchor, NULL);
	assert_rc_equal(-DER_CSUM, rc);
	client_clear_fault();

	/** Sanity check that no failure still returns success */
	nr = KDS_NR;
	memset(&anchor, 0, sizeof(anchor));
	rc = daos_obj_list_akey(ctx.oh, DAOS_TX_NONE, &ctx.dkey, &nr, kds, &sgl,
				&anchor, NULL);
	assert_rc_equal(0, rc);
	assert_int_equal(KDS_NR, nr);

	/** Clean up */
	d_sgl_fini(&sgl, true);
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

static void
test_enumerate_d_key(void **state)
{
	struct csum_test_ctx	ctx = {0};
	daos_oclass_id_t	oc = dts_csum_oc;
	int			rc = 0;
	uint32_t		i;
	daos_anchor_t		anchor = {0};
	daos_key_desc_t		kds[KDS_NR] = {0};
	d_sg_list_t		sgl = {0};
	uint32_t		nr = KDS_NR;
	uint32_t		key_count = 0;

	FAULT_INJECTION_REQUIRED();

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, dts_csum_prop_type, false, 1024, oc);
	setup_simple_data(&ctx);

	/** insert multiple keys to enumerate */
	for (i = 0; i < KDS_NR; i++) {
		rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				     &ctx.update_iod, &ctx.update_sgl,
				     NULL);
		assert_rc_equal(0, rc);
		((uint8_t *)ctx.dkey.iov_buf)[0] += 1;
	}

	/** Make sure can handle verifying keys over multiple iovs */
	d_sgl_init(&sgl, 1);
	iov_alloc(&sgl.sg_iovs[0], 100);

	/** inject failure ... should return CSUM error */
	client_corrupt_dkey_on_fetch();

	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor) && rc == 0) {
		rc = daos_obj_list_dkey(ctx.oh, DAOS_TX_NONE, &nr, kds, &sgl,
					&anchor, NULL);
		nr = KDS_NR;
	}

	assert_rc_equal(-DER_CSUM, rc);
	client_clear_fault();

	/** Sanity check that no failure still returns success */
	nr = KDS_NR;
	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		rc = daos_obj_list_dkey(ctx.oh, DAOS_TX_NONE, &nr, kds, &sgl,
					&anchor, NULL);
		assert_rc_equal(0, rc);
		key_count += nr;
		nr = KDS_NR;
	}
	assert_int_equal(KDS_NR, key_count);

	/** Clean up */
	d_sgl_fini(&sgl, true);
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

/** Used to test the obj_list_obj. There is no DAOS API that will test
 * this so creating a local test version to verify functionality with checksums
 * for rebuild
 */
int
tst_obj_list_obj(daos_handle_t oh, daos_epoch_range_t *epr, daos_key_t *dkey,
		 daos_key_t *akey, daos_size_t *size, uint32_t *nr,
		 daos_key_desc_t *kds, d_sg_list_t *sgl, daos_anchor_t *anchor,
		 daos_anchor_t *dkey_anchor, daos_anchor_t *akey_anchor,
		 d_iov_t *csum_iov)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_list_obj_task_create(oh, DAOS_TX_NONE, epr, dkey, akey,
					 size, nr, kds, sgl,
					 anchor, dkey_anchor, akey_anchor,
					 true, NULL, NULL, csum_iov, &task);
	assert_rc_equal(0, rc);
	return dc_task_schedule(task, true);
}

static int
test_enum_unpack_cb(struct dc_obj_enum_unpack_io *io, void *arg)
{
	struct daos_csummer	*csummer = NULL;
	int			 rc;
	int			 i;
	d_iov_t			 tmp_iov;

	rc = daos_csummer_csum_init_with_packed(&csummer, &io->ui_csum_iov);
	assert_success(rc);

	tmp_iov = io->ui_csum_iov;
	/* +1 to convert from idx to nr */
	for (i = 0; i < io->ui_iods_top + 1; i++) {
		struct dcs_iod_csums *iod_csums = NULL;

		if (io->ui_sgls[i].sg_nr_out == 0 ||
		    io->ui_sgls[i].sg_iovs == NULL ||
		    io->ui_iods[i].iod_nr == 0) {
			/* Not inlined */
			continue;
		}

		rc = daos_csummer_alloc_iods_csums_with_packed(csummer, &io->ui_iods[i], 1,
							       &tmp_iov, &iod_csums);
		assert_success(rc);

		rc = daos_csummer_verify_iod(csummer, &io->ui_iods[i], &io->ui_sgls[i], iod_csums,
					     NULL, 0, NULL);

		assert_success(rc);

		daos_csummer_free_ic(csummer, &iod_csums);
	}

	daos_csummer_destroy(&csummer);

	return 0;
}

static void
test_enumerate_object(void **state)
{
	test_arg_t		*arg = *state;
	struct csum_test_ctx	 ctx = {0};
	daos_anchor_t		 anchor = {0};
	daos_anchor_t		 dkey_anchor = {0};
	daos_anchor_t		 akey_anchor = {0};
	d_iov_t			 csum_iov = {0};
	d_iov_t			 tmp_csum_iov = {0};
	d_sg_list_t		 sgl = {0};
	struct dcs_csum_info	*csum_info = NULL;
	void			*end_byte;
	daos_key_desc_t		*kds = NULL;
	uint32_t		 nr;
	int			 rc;
	char			 dkey[32];
	int			 d;
	daos_size_t		 size = 0;
	struct ioreq		 req;
	const uint32_t		 dkey_loop = 3;
	const uint32_t		 akey_loop = 3;
	const uint32_t		 rec_loop = 3;
	daos_unit_oid_t		 unit_oid = {0};
	daos_epoch_range_t	 epr;

	/*
	 * data less than 32 bytes should be inlined. see ds_obj_enum_handler()
	 * for inline_thres set on enum args
	 */
	char *data_small = "Lorem ipsum dolor sit amet, co";
	char *data_medium = "Lorem ipsum dolor sit amet, consectetur adipiscing"
			    " elit, sed do eiusmod tempor incididunt ut labore"
			    " et dolore magna aliqua. Ut enim ad minim veniam,"
			    " quis nostrud exercitation ullamco laboris nisi ut"
			    " aliquip ex ea commodo consequat. Duis aute irure"
			    " dolor in reprehenderit in voluptate velit esse"
			    " cillum dolore eu fugiat nulla pariatur. Excepteur"
			    " sint occaecat cupidatat non proident, sunt in"
			    " culpa qui officia deserunt mollit anim id est"
			    " laborum.";
	char *data_large = NULL;


	D_ALLOC(data_large, 1024 * 3);
	assert_non_null(data_large);
	memset(data_large, 'L', 1024 * 3 - 1); /* -1: keep \0 */

	FAULT_INJECTION_REQUIRED();

	assert_true(strlen(data_small) + 1 <= 32);
	assert_true(strlen(data_medium) + 1 > 32);
	assert_true(strlen(data_large) + 1 > 2048);

	d_sgl_init(&sgl, 1);
	iov_alloc(&sgl.sg_iovs[0], 2048);

	D_ALLOC_ARRAY(kds, 1024);
	nr = 1024;

	iov_alloc(&csum_iov, 2048);
	/* iov_len is used for how much of the buffer is used */
	csum_iov.iov_len = 0;

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, 10, OC_S1);

	ioreq_init(&req, ctx.coh, ctx.oid, DAOS_IOD_ARRAY, arg);

	/* insert data to be enumerated */
	for (d = 0; d < dkey_loop; d++) {
		sprintf(dkey, "dkey-%d", d);

		insert_data("akey-%d-array-small", dkey,
			    &req, akey_loop, rec_loop, data_small,
			    DAOS_IOD_ARRAY);
		insert_data("akey-%d-array-medium", dkey,
			    &req, akey_loop, rec_loop, data_medium,
			    DAOS_IOD_ARRAY);
		insert_data("akey-%d-array-large", dkey,
			    &req, akey_loop, rec_loop, data_large,
			    DAOS_IOD_ARRAY);
		insert_data("akey-%d-single-small", dkey,
			    &req, akey_loop, rec_loop, data_small,
			    DAOS_IOD_SINGLE);
		insert_data("akey-%d-single-medium", dkey,
			    &req, akey_loop, rec_loop, data_medium,
			    DAOS_IOD_SINGLE);
		insert_data("akey-%d-single-large", dkey,
			    &req, akey_loop, rec_loop, data_large,
			    DAOS_IOD_SINGLE);
	}

	/** inject failure ... should return CSUM error */
	client_corrupt_akey_on_fetch();
	epr.epr_lo = 0;
	epr.epr_hi = DAOS_EPOCH_MAX;
	rc = tst_obj_list_obj(ctx.oh, &epr, NULL, NULL, NULL, &nr, kds,
		&sgl, &anchor, &dkey_anchor, &akey_anchor, &csum_iov);

	assert_rc_equal(-DER_CSUM, rc);
	client_clear_fault();

	/** Sanity check that no failure still returns success. Setting
	 * nr to larger than expected because tst_obj_list_obj will set it to
	 * what's returned. If there is a bug an tst_obj_list_obj could return
	 * more than expected, want to catch it.
	 */
	memset(&anchor, 0, sizeof(anchor));
	memset(&dkey_anchor, 0, sizeof(dkey_anchor));
	memset(&akey_anchor, 0, sizeof(akey_anchor));
	sgl.sg_nr_out = 0;
	csum_iov.iov_len = 0;
	rc = tst_obj_list_obj(ctx.oh, NULL, NULL, NULL, &size, &nr, kds,
			      &sgl, &anchor, &dkey_anchor, &akey_anchor,
			      &csum_iov);

	assert_success(rc);

	if (csum_iov.iov_len == 0)
		fail_msg("Should have had something in csum_iov");
	ioreq_fini(&req);

	/** Make sure csum iov is correct */
	tmp_csum_iov = csum_iov;
	end_byte = tmp_csum_iov.iov_buf + tmp_csum_iov.iov_len;
	while (tmp_csum_iov.iov_buf < end_byte) {
		ci_cast(&csum_info, &tmp_csum_iov);
		tmp_csum_iov.iov_buf += ci_size(*csum_info);
		tmp_csum_iov.iov_buf_len -= ci_size(*csum_info);
		tmp_csum_iov.iov_len -= ci_size(*csum_info);
	}


	unit_oid.id_pub = ctx.oid;
	rc = dc_obj_enum_unpack(unit_oid, kds, nr, &sgl, &csum_iov,
				test_enum_unpack_cb, NULL);
	assert_success(rc);

	/** Clean up */
	d_sgl_fini(&sgl, true);
	daos_iov_free(&csum_iov);
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
	D_FREE(kds);
}

static uint32_t
get_csum_count(d_iov_t *csum_iov)
{
	void			*end_byte;
	struct dcs_csum_info	*csum_info = NULL;
	uint32_t		 csum_count = 0;

	end_byte = csum_iov->iov_buf + csum_iov->iov_len;

	while (csum_iov->iov_buf < end_byte) {
		ci_cast(&csum_info, csum_iov);
		csum_iov->iov_buf += ci_size(*csum_info);
		csum_iov->iov_buf_len -= ci_size(*csum_info);
		csum_iov->iov_len -= ci_size(*csum_info);
		csum_count++;
	}
	return csum_count;
}

/**
 * insert overlapping extents. When enumerating visible extents are checksums
 * must be calculated for the partial extent.
 */
static void
test_enumerate_object2(void **state)
{
#define KDS_NR 10
	struct csum_test_ctx	 ctx = {0};
	daos_oclass_id_t	 oc = dts_csum_oc;
	daos_anchor_t		 anchor = {0};
	daos_anchor_t		 dkey_anchor = {0};
	daos_anchor_t		 akey_anchor = {0};
	d_iov_t			 csum_iov = {0};
	d_sg_list_t		 list_sgl = {0};
	uint32_t		 nr = KDS_NR;
	daos_key_desc_t		 kds[KDS_NR] = {0};
	uint8_t			 csum_buf[2048];
	int			 rc;

	d_iov_set(&csum_iov, csum_buf, sizeof(csum_buf));
	csum_iov.iov_len = 0;

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, 8, oc);
	setup_single_recx_data(&ctx, "ABCDEF", 1024);

	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_rc_equal(0, rc);
	ctx.update_iod.iod_recxs->rx_idx += 5;
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl,
			     NULL);
	assert_rc_equal(0, rc);

	d_sgl_init(&list_sgl, 1);
	iov_alloc(&list_sgl.sg_iovs[0], 1024 * 64);
	/** Sanity check that no failure still returns success */
	daos_epoch_range_t epr = {.epr_lo = 0, .epr_hi = DAOS_EPOCH_MAX};

	/** force to only list visible extents */
	daos_anchor_set_flags(&dkey_anchor,
			      DIOF_TO_LEADER | DIOF_WITH_SPEC_EPOCH);

	memset(list_sgl.sg_iovs[0].iov_buf, 0, list_sgl.sg_iovs[0].iov_buf_len);
	rc = tst_obj_list_obj(ctx.oh, &epr, &ctx.dkey, NULL, NULL, &nr, kds,
			      &list_sgl, &anchor, &dkey_anchor, &akey_anchor,
			      &csum_iov);
	assert_success(rc);

	/** make sure got correct number of key descriptors. Should have gotten
	 * for: obj, dkey, akey, recx
	 */
	assert_int_equal(4, nr);

	/** only 2 checksums, dkey, akey */
	assert_int_equal(2, get_csum_count(&csum_iov));

	/** Clean up */
	d_sgl_fini(&list_sgl, true);
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

static void
test_enumerate_object_csum_buf_too_small(void **state)
{
	struct csum_test_ctx	ctx = {0};
	daos_anchor_t		anchor = {0};
	daos_anchor_t		dkey_anchor = {0};
	daos_anchor_t		akey_anchor = {0};
	daos_anchor_t		zero_anchor = {0};
	d_iov_t			csum_iov = {0};
	d_sg_list_t		sgl = {0};
	const uint32_t		akey_nr = 5;
	const uint32_t		enum_nr = akey_nr * 2 + 2;
	daos_key_desc_t		kds[enum_nr];
	const size_t		csum_buf_len = 10;
	uint8_t			csum_buf[csum_buf_len];
	uint32_t		i;
	uint32_t		nr;
	int			rc;

	memset(kds, 0, enum_nr * sizeof(*kds));

	d_iov_set(&csum_iov, csum_buf, csum_buf_len);
	csum_iov.iov_len = 0;

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC64, false, 1024, dts_csum_oc);
	setup_simple_data(&ctx);

	/** insert multiple a keys.*/
	for (i = 0; i < akey_nr; i++) {
		rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
				     &ctx.update_iod, &ctx.update_sgl,
				     NULL);
		assert_rc_equal(0, rc);
		((uint8_t *)ctx.update_iod.iod_name.iov_buf)[1] += 1;
		((uint8_t *)ctx.update_sgl.sg_iovs->iov_buf)[0] += 1;
	}

	d_sgl_init(&sgl, 1);
	iov_alloc(&sgl.sg_iovs[0], 1024);

	nr = enum_nr;
	rc = tst_obj_list_obj(ctx.oh, NULL, &ctx.dkey, NULL, NULL, &nr, kds,
			      &sgl, &anchor, &dkey_anchor, &akey_anchor,
			      &csum_iov);
	assert_rc_equal(-DER_TRUNC, rc);
	/** ensure anchors don't change.  */
	assert_memory_equal(&zero_anchor, &anchor, sizeof(zero_anchor));
	assert_memory_equal(&zero_anchor, &dkey_anchor, sizeof(zero_anchor));
	assert_memory_equal(&zero_anchor, &akey_anchor, sizeof(zero_anchor));

	/** Clean up */
	d_sgl_fini(&sgl, true);
	cleanup_data(&ctx);
	cleanup_cont_obj(&ctx);
}

static void
ec_chunk_plus_one(void **state)
{
	const uint32_t ec_chunk_plus_one = 1024 * 1024 + 1;

	if (!test_runable(*state, 3))
		skip();


	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.oclass = OC_EC_2P1G1,
		.chunksize = 8,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 0, .nr = ec_chunk_plus_one, .data = "ABCDEF"},
		},
		.fetch_recxs = { {.rx_idx = 0, .rx_nr = ec_chunk_plus_one} },
	});
}

static void
ec_two_chunk_plus_one(void **state)
{
	const uint32_t ec_chunk_plus_one = 2 * 1024 * 1024 + 1;

	if (!test_runable(*state, 3))
		skip();

	ARRAY_UPDATE_FETCH_TESTCASE(state, {
		.oclass = OC_EC_2P1G1,
		.chunksize = 8,
		.csum_prop_type = dts_csum_prop_type,
		.server_verify = false,
		.rec_size = 1,
		.recx_cfgs = {
			{.idx = 0, .nr = ec_chunk_plus_one, .data = "ABCDEF"},
		},
		.fetch_recxs = { {.rx_idx = 0, .rx_nr = ec_chunk_plus_one} },
	});
}

static void
basic_scrubbing_test(void **state, char *scrub_freq)
{
	struct csum_test_ctx	 ctx = {0};
	daos_oclass_id_t	 oc = dts_csum_oc;
	int			 rc;

	if (csum_ec_enabled() && !test_runable(*state, csum_ec_grp_size()))
		skip();

	/**
	 * Setup
	 */
	setup_from_test_args(&ctx, (test_arg_t *)*state);
	rc = dmg_pool_set_prop(dmg_config_file, "scrub", "timed",
			       ctx.test_arg->pool.pool_uuid);
	assert_success(rc);
	rc = dmg_pool_set_prop(dmg_config_file, "scrub-freq", scrub_freq,
			       ctx.test_arg->pool.pool_uuid);
	assert_success(rc);

	setup_simple_data(&ctx);
	setup_cont_obj(&ctx, DAOS_PROP_CO_CSUM_CRC32, false, 1024, oc);

	/**
	 * Act
	 */
	rc = daos_obj_update(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			     &ctx.update_iod, &ctx.update_sgl, NULL);
	assert_success(rc);

	rc = daos_obj_fetch(ctx.oh, DAOS_TX_NONE, 0, &ctx.dkey, 1,
			    &ctx.fetch_iod, &ctx.fetch_sgl, NULL, NULL);
	assert_success(rc);
	assert_memory_equal(ctx.update_sgl.sg_iovs->iov_buf,
			    ctx.fetch_sgl.sg_iovs->iov_buf,
			    ctx.update_sgl.sg_iovs->iov_buf_len);

	/**
	 * Clean up
	 */

	sleep(5); /* Make sure scrubber has had time to start running */
	cleanup_cont_obj(&ctx);
	cleanup_data(&ctx);
}

static void
scrubbing_a_lot(void **state)
{
	basic_scrubbing_test(state, "1");
}

static void
scrubbing_with_large_sleep(void **state)
{
	basic_scrubbing_test(state, "100000");
}

static void
multiple_ec_singv_csum(void **state)
{
	test_arg_t		*arg = *state;
	struct csum_test_ctx	 ctx = {0};
	daos_oclass_id_t	 oc = dts_csum_oc;
	daos_handle_t		 oh;
	d_iov_t			 dkey;
	d_sg_list_t		 sgl[3];
	d_iov_t			 sg_iov[3];
	daos_iod_t		 iod[3];
	char			*buf[3];
	size_t			 len[3];
	int			 i, rc;

	FAULT_INJECTION_REQUIRED();

	if (csum_ec_enabled() && !test_runable(arg, csum_ec_grp_size()))
		skip();

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, dts_csum_prop_type, true, 0, oc);

	for (i = 0; i < 3; i++) {
		buf[i] = NULL;
		len[i] = 0;
	}

	len[0] = 195;
	len[1] = 5822;
	len[2] = 6162;
	for (i = 0; i < 3; i++) {
		D_ALLOC(buf[i], len[i]);
		assert_non_null(buf[i]);
		dts_buf_render(buf[i], len[i]);
	}

	oh = ctx.oh;
	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	for (i = 0; i < 3; i++)
		d_iov_set(&sg_iov[i], buf[i], len[i]);

	d_iov_set(&iod[0].iod_name, "akey1", strlen("akey1"));
	d_iov_set(&iod[1].iod_name, "akey2", strlen("akey2"));
	d_iov_set(&iod[2].iod_name, "akey3", strlen("akey2"));

	for (i = 0; i < 3; i++) {
		sgl[i].sg_nr = 1;
		sgl[i].sg_nr_out = 0;
		sgl[i].sg_iovs = &sg_iov[i];
		iod[i].iod_nr = 1;
		iod[i].iod_recxs = NULL;
		iod[i].iod_type = DAOS_IOD_SINGLE;
		iod[i].iod_size = len[i];
	}

	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 3, iod, sgl, NULL);
	assert_rc_equal(rc, 0);

	iod[0].iod_size = 0;
	daos_fail_loc_set(DAOS_CSUM_CORRUPT_FETCH | DAOS_FAIL_ALWAYS);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 3, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, -DER_CSUM);
	assert_rc_equal(iod[0].iod_size, 195);

	daos_fail_loc_set(0);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 3, iod, sgl, NULL, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 3; i++)
		D_FREE(buf[i]);
	client_clear_fault();
	cleanup_cont_obj(&ctx);
	cleanup_data(&ctx);
}

static void
multiple_ec_singv_csum_with_iod_size_0(void **state)
{
	test_arg_t          *arg = *state;
	struct csum_test_ctx ctx = {0};
	daos_oclass_id_t     oc  = dts_csum_oc;
	daos_handle_t        oh;
	d_iov_t              dkey;
	d_sg_list_t          sgl[3];
	d_iov_t              sg_iov[3];
	daos_iod_t           iod[3];
	char                *buf[3];
	size_t               len[3];
	int                  i, rc;

	if (csum_ec_enabled() && !test_runable(arg, csum_ec_grp_size()))
		skip();

	setup_from_test_args(&ctx, *state);
	setup_cont_obj(&ctx, dts_csum_prop_type, true, 0, oc);

	for (i = 0; i < 3; i++) {
		buf[i] = NULL;
		len[i] = 0;
	}

	len[0] = 195;
	len[1] = 5822;
	len[2] = 6162;
	for (i = 0; i < 3; i++) {
		D_ALLOC(buf[i], len[i]);
		assert_non_null(buf[i]);
		dts_buf_render(buf[i], len[i]);
	}

	oh = ctx.oh;
	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	for (i = 0; i < 3; i++)
		d_iov_set(&sg_iov[i], buf[i], len[i]);

	d_iov_set(&iod[0].iod_name, "akey1", strlen("akey1"));
	d_iov_set(&iod[1].iod_name, "akey2", strlen("akey2"));
	d_iov_set(&iod[2].iod_name, "akey3", strlen("akey2"));

	for (i = 0; i < 3; i++) {
		sgl[i].sg_nr     = 1;
		sgl[i].sg_nr_out = 0;
		sgl[i].sg_iovs   = &sg_iov[i];
		iod[i].iod_nr    = 1;
		iod[i].iod_recxs = NULL;
		iod[i].iod_type  = DAOS_IOD_SINGLE;
		iod[i].iod_size  = len[i];
	}

	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 3, iod, sgl, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 3; i++) {
		iod[i].iod_size = DAOS_REC_ANY;
	}
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 3, iod, sgl, NULL, NULL);
	assert_success(rc);

	for (i = 0; i < 3; i++)
		D_FREE(buf[i]);
	cleanup_cont_obj(&ctx);
	cleanup_data(&ctx);
}

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			  0, NULL);
}

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define CSUM_TEST(dsc, test) { STR(__COUNTER__)". " dsc, test, \
			  csum_replia_enable, test_case_teardown }
#define EC_CSUM_TEST(dsc, test) { STR(__COUNTER__)". " dsc, test, \
			  csum_ec_enable, test_case_teardown }

static const struct CMUnitTest csum_tests[] = {
    CSUM_TEST("DAOS_CSUM00: csum disabled", checksum_disabled),
    CSUM_TEST("DAOS_CSUM01: simple update with server side verify", io_with_server_side_verify),
    CSUM_TEST("DAOS_CSUM02: Fetch Array Type", test_fetch_array),
    CSUM_TEST("DAOS_CSUM03: Setup multiple overlapping/unaligned extents",
	      fetch_with_multiple_extents),
    CSUM_TEST("DAOS_CSUM03.1: Overwrites after first chunk", overwrites_after_first_chunk),
    CSUM_TEST("DAOS_CSUM03.2: Unaligned record size", unaligned_record_size),
    CSUM_TEST("DAOS_CSUM03.3: Record size is larger than chunk size",
	      record_size_larger_than_chunksize),
    CSUM_TEST("DAOS_CSUM03.4: Record size is 20", larger_record_size_with_second_chunk_half_first),
    CSUM_TEST("DAOS_CSUM03.5: Setup multiple overlapping/unaligned extents",
	      overlapping_after_first_chunk),
    CSUM_TEST("DAOS_CSUM03.6: Request the second half of a chunk", request_second_half_of_chunk),
    CSUM_TEST("DAOS_CSUM04.1: With holes between extents. All in 1 chunk", extents_with_holes_1),
    CSUM_TEST("DAOS_CSUM04.2: With holes at beginning and end. of extent."
	      " All in 1 chunk",
	      extents_with_holes_2),
    CSUM_TEST("DAOS_CSUM04.3: With hole that spans into next chunk", extents_with_holes_3),
    CSUM_TEST("DAOS_CSUM04.4: With many holes in a single chunk", extents_with_holes_4),
    CSUM_TEST("DAOS_CSUM04.5: With hole that spans many chunks", extents_with_holes_5),
    CSUM_TEST("DAOS_CSUM04.6: With holes with larger record size.", extents_with_holes_6),
    CSUM_TEST("DAOS_CSUM04.7: With hole caused by a punch.", extents_with_holes_7),
    CSUM_TEST("DAOS_CSUM04.8: With extents in reverse order", extents_in_reverse_order),
    CSUM_TEST("DAOS_CSUM05: Server data corrupted after RDMA", test_server_data_corruption),
    CSUM_TEST("DAOS_CSUM06: Single Value Checksum", single_value),
    CSUM_TEST("DAOS_CSUM07: Mix of Single Value and Array values iods", mix_test),
    CSUM_TEST("DAOS_CSUM08: Update/Fetch A Key", test_update_fetch_a_key),
    CSUM_TEST("DAOS_CSUM09: Update/Fetch D Key", test_update_fetch_d_key),
    CSUM_TEST("DAOS_CSUM10: Enumerate A Keys", test_enumerate_a_key),
    CSUM_TEST("DAOS_CSUM11: Enumerate D Keys", test_enumerate_d_key),
    CSUM_TEST("DAOS_CSUM12: Enumerate objects", test_enumerate_object),
    CSUM_TEST("DAOS_CSUM12.1: Enumerate objects with overlapping "
	      "extents",
	      test_enumerate_object2),
    CSUM_TEST("DAOS_CSUM13: Enumerate objects with too small csum buffer",
	      test_enumerate_object_csum_buf_too_small),
    CSUM_TEST("DAOS_CSUM14 - Get checksum through fetch task api", test_fetch_task_api),
    CSUM_TEST("DAOS_CSUM14: Many IODs", many_iovs_with_single_values),
    CSUM_TEST("DAOS_CSUM14.1: two iods and two recx", two_iods_two_recxs),
    CSUM_TEST("DAOS_CSUM15: Request non existent data", request_non_existent_data),
    CSUM_TEST("DAOS_CSUM16: Unaligned hole at beginning", unaligned_hole_at_beginning),
    CSUM_TEST("DAOS_CSUM17: Through some random testing found a rounding "
	      "error",
	      bug_rounding_error),
    CSUM_TEST("DAOS_CSUM18: request extent starts much later than the "
	      "beginning of the stored extent",
	      request_is_after_extent_start),
    CSUM_TEST("DAOS_CSUM19: DTX with checksum enabled against REP obj", dtx_with_csum),
    CSUM_TEST("DAOS_CSUM_REBUILD01: Array, Data is inlined", rebuild_1),
    CSUM_TEST("DAOS_CSUM_REBUILD02: Array, Data not inlined, not bulk", rebuild_2),
    CSUM_TEST("DAOS_CSUM_REBUILD03: Array, Data bulk transfer", rebuild_3),
    CSUM_TEST("DAOS_CSUM_REBUILD04: SV, Data is inlined", rebuild_4),
    CSUM_TEST("DAOS_CSUM_REBUILD05: SV, Data not inlined, not bulk", rebuild_5),
    CSUM_TEST("DAOS_CSUM_REBUILD06: SV, Data bulk transfer", rebuild_6),
    CSUM_TEST("Punch before insert", punch_before_insert),
    EC_CSUM_TEST("DAOS_EC_CSUM00: csum disabled", checksum_disabled),
    EC_CSUM_TEST("DAOS_EC_CSUM01: simple update with server side verify",
		 io_with_server_side_verify),
    EC_CSUM_TEST("DAOS_EC_CSUM02: Fetch EC Array Type", test_fetch_array),
    EC_CSUM_TEST("DAOS_EC_CSUM03: Single Value Checksum", single_value),
    EC_CSUM_TEST("DAOS_EC_CSUM04: DTX with checksum enabled against EC obj", dtx_with_csum),
    CSUM_TEST("DAOS_EC_CSUM04: Single extent that is 1 byte larger than "
	      "EC chunk",
	      ec_chunk_plus_one),
    CSUM_TEST("DAOS_EC_CSUM04: Single extent that is 1 byte larger than "
	      "2 EC chunks",
	      ec_two_chunk_plus_one),
    EC_CSUM_TEST("DAOS_EC_CSUM05: multiple EC single value csum", multiple_ec_singv_csum),
    EC_CSUM_TEST("DAOS_EC_CSUM06: multiple EC single value csum with iod size DAOS_REC_ANY",
		 multiple_ec_singv_csum_with_iod_size_0),
    CSUM_TEST("DAOS_SCRUBBING00: A basic scrubbing test with scrubbing "
	      "running very frequently",
	      scrubbing_a_lot),
    CSUM_TEST("DAOS_SCRUBBING01: A basic scrubbing test with a long wait "
	      "in between. Should still be able to destroy cont and pool",
	      scrubbing_with_large_sleep)};

static int
run_csum_tests(int rc)
{
	rc += cmocka_run_group_tests_name("DAOS_Checksum",
		  csum_tests, setup,
		  test_teardown);
	return rc;
}

int
run_daos_checksum_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;
	int i;

	if (rank != 0) {
		par_barrier(PAR_COMM_WORLD);
		return 0;
	}

	if (sub_tests_size == 0) {
		if (getenv("DAOS_CSUM_TEST_ALL_TYPE")) {
			for (i = DAOS_PROP_CO_CSUM_OFF + 1;
			     i <= DAOS_PROP_CO_CSUM_ADLER32; i++) {
				dts_csum_prop_type = i;
				print_message("Running tests with csum_type: "
					      "%d\n", i);
				rc = run_csum_tests(rc);
			}
		} else {
			rc = run_csum_tests(rc);
		}

	} else {
		rc = run_daos_sub_tests("DAOS_Checksum",
					csum_tests,
					ARRAY_SIZE(csum_tests),
					sub_tests,
					sub_tests_size, setup,
					test_teardown);
	}

	par_barrier(PAR_COMM_WORLD);
	return rc;
}
