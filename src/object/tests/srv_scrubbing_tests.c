/*
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <daos/test_utils.h>
#include <daos/checksum.h>
#include <daos_srv/evtree.h>
#include <daos_srv/srv_csum.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/vos.h>
#include <fcntl.h>
#include <daos/tests_lib.h>


/**
 * Scrubbing tests are integration tests between checksum functionality
 * and VOS. VOS does not calculate any checksums so the checksums for the
 * data are calculated here in the tests, which makes it convinient for making
 * the data appear as though it is corrupted. In general the tests write data
 * using vos_obj_update, run the scanner, then try to fetch the data using
 * vos_obj_fetch. If the data is corrupted, vos_obj_fetch should return
 * -DER_CSUM. There are also callbacks that should be called appropriately
 * to handle progress of the scanner and when corruption is discovered.
 */

#define assert_csum_error(r) do {\
	int __rc = (r); \
	if (__rc != -DER_CSUM) \
		fail_msg("Expected -DER_CSUM but found: " DF_RC, DP_RC(__rc)); \
	} while (0)

/** ds_progress_handler_t */
static int progress_cb_count;
static int
tst_handle_progress()
{
	progress_cb_count++;
	return 0;
}

/** ds_corruption_handler */
static int corruption_cb_count;
static int
tst_handle_corruption()
{
	corruption_cb_count++;
	return 0;
}

/** easily setup and allocate an iov */
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

/** Different types of IOD configurations for the test */
enum TEST_IOD_TYPE {
	TEST_IOD_SINGLE, /** DAOS_IOD_SINGLE */
	TEST_IOD_ARRAY_1, /** DAOS_IOD_ARRAY with record size 1 */
	TEST_IOD_ARRAY_2, /** DAOS_IOD_ARRAY with record size 2 */
	TEST_IOD_ARRAY_20, /** DAOS_IOD_ARRAY with record size 20 */
	TEST_IOD_ARRAY_256, /** DAOS_IOD_ARRAY with record size 256 */
};

/**
 * setup the iod based on the iod test type. Will define the iod type, recxs
 * if an array with different record sizes and start indexes.
 */
static void
setup_iod_type(daos_iod_t *iod, int iod_type, daos_size_t data_len,
	       daos_recx_t *recx)
{
	iod->iod_nr = 1;

	switch (iod_type) {
	case TEST_IOD_SINGLE:
		iod->iod_type = DAOS_IOD_SINGLE;
		iod->iod_size = data_len;
		break;
	case TEST_IOD_ARRAY_1:
		iod->iod_recxs = recx;
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_size = 1;
		iod->iod_recxs->rx_nr = data_len;
		break;
	case TEST_IOD_ARRAY_2:
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_recxs = recx;
		iod->iod_size = 2;
		iod->iod_recxs->rx_idx = 10;
		iod->iod_recxs->rx_nr = data_len / 2;
		break;
	case TEST_IOD_ARRAY_20:
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_recxs = recx;
		iod->iod_size = 20;
		iod->iod_recxs->rx_idx = 95;
		iod->iod_recxs->rx_nr = data_len / 20;
		break;
	case TEST_IOD_ARRAY_256:
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_recxs = recx;
		iod->iod_size = 256;
		iod->iod_recxs->rx_idx = 12345678;
		iod->iod_recxs->rx_nr = data_len / 256;
	}
}

/** scrubbing test context */
struct sts_context {
	char			 tsc_pmem_file[256];
	uuid_t			 tsc_pool_uuid;
	uuid_t			 tsc_cont_uuid;
	uint64_t		 tsc_scm_size;
	uint64_t		 tsc_nvme_size;
	daos_size_t		 tsc_chunk_size;
	daos_size_t		 tsc_data_len;
	daos_handle_t		 tsc_poh;
	daos_handle_t		 tsc_coh;
	struct daos_csummer	*tsc_csummer;
	ds_progress_handler_t	 tsc_credits_consumed_handler;
	ds_corruption_handler_t	 tsc_corruption_handler;
};

static void
sts_ctx_pool_init(struct sts_context *ctx)
{
	char		*pmem_file = ctx->tsc_pmem_file;
	daos_handle_t	 poh = DAOS_HDL_INVAL;
	int		 fd;
	int		 rc;

	if (!daos_file_is_dax(pmem_file)) {
		rc = open(pmem_file, O_CREAT | O_TRUNC | O_RDWR, 0666);
		if (rc < 0)
			fail_msg("Unable to open pmem_file");

		fd = rc;
		rc = fallocate(fd, 0, 0, ctx->tsc_scm_size);
		if (rc)
			fail_msg("fallocate failed");
	}

	/* Use pool size as blob size for this moment. */
	assert_success(vos_pool_create(pmem_file, ctx->tsc_pool_uuid, 0,
			     ctx->tsc_nvme_size));
	assert_success(vos_pool_open(pmem_file, ctx->tsc_pool_uuid, &poh));

	ctx->tsc_poh = poh;
}

static void
sts_ctx_pool_fini(struct sts_context *ctx)
{
	int	rc;

	vos_pool_close(ctx->tsc_poh);
	rc = vos_pool_destroy(ctx->tsc_pmem_file, ctx->tsc_pool_uuid);
	D_ASSERTF(rc == 0 || rc == -DER_NONEXIST, "rc="DF_RC"\n", DP_RC(rc));
}

static int
sts_ctx_cont_init(struct sts_context *ctx)
{
	daos_handle_t	coh = DAOS_HDL_INVAL;

	assert_success(vos_cont_create(ctx->tsc_poh, ctx->tsc_cont_uuid));
	assert_success(vos_cont_open(ctx->tsc_poh, ctx->tsc_cont_uuid, &coh));

	ctx->tsc_coh = coh;

	return 0;
}

static void
sts_ctx_cont_fini(struct sts_context *ctx)
{
	vos_cont_close(ctx->tsc_coh);
}

static void
sts_ctx_init(struct sts_context *ctx)
{
	/** default values */
	ctx->tsc_scm_size = (1024 * 1024 * 1024);
	if (ctx->tsc_scm_size == 0)
		ctx->tsc_scm_size = (1024 * 1024 * 1024);
	if (ctx->tsc_chunk_size == 0)
		ctx->tsc_chunk_size = 1024;
	if (ctx->tsc_data_len == 0)
		ctx->tsc_data_len = 1024;

	ctx->tsc_credits_consumed_handler = tst_handle_progress;
	ctx->tsc_corruption_handler = tst_handle_corruption;
	uuid_parse("12345678-1234-1234-1234-123456789012", ctx->tsc_pool_uuid);
	sprintf(ctx->tsc_pmem_file, "/mnt/daos/vos_scrubbing.pmem");

	assert_success(daos_debug_init(DAOS_LOG_DEFAULT));
	assert_success(vos_init());
	sts_ctx_pool_init(ctx);
	sts_ctx_cont_init(ctx);

	assert_success(
		daos_csummer_init_with_type(&ctx->tsc_csummer,
					    CSUM_TYPE_ISAL_CRC16_T10DIF,
					    ctx->tsc_chunk_size, false));
}

static void
sts_ctx_fini(struct sts_context *ctx)
{
	daos_csummer_destroy(&ctx->tsc_csummer);
	sts_ctx_cont_fini(ctx);
	sts_ctx_pool_fini(ctx);
}

static int
sts_ctx_fetch(struct sts_context *ctx, daos_oclass_id_t oclass, int oid_lo,
	      uint32_t shard, int iod_type, const char *dkey_str,
	      const char *akey_str, int epoch, uint64_t data_len)
{
	daos_unit_oid_t	oid = {0};
	daos_key_t	dkey;
	daos_recx_t	recx = {0};
	daos_iod_t	iod = {0};
	char		data[data_len];
	d_sg_list_t	sgl;
	int		rc;

	oid.id_shard	= shard;
	oid.id_pad_32	= 0;
	oid.id_pub.lo = oid_lo;
	daos_obj_generate_id(&oid.id_pub, 0, oclass, 0);

	iov_alloc_str(&iod.iod_name, akey_str);
	setup_iod_type(&iod, iod_type, data_len, &recx);

	d_sgl_init(&sgl, 1);
	d_iov_set(&sgl.sg_iovs[0], data, data_len);

	iov_alloc_str(&dkey, dkey_str);
	rc = vos_obj_fetch(ctx->tsc_coh, oid, epoch, 0, &dkey, 1, &iod, &sgl);

	D_FREE(dkey.iov_buf);
	D_FREE(iod.iod_name.iov_buf);

	return rc;
}

static void
sts_ctx_update(struct sts_context *ctx, daos_oclass_id_t oclass, int oid_lo,
	       uint32_t shard, int iod_type, const char *dkey_str,
	       const char *akey_str, int epoch, const char *data_str,
	       bool corrupt_it)
{
	daos_unit_oid_t		 oid = {0};
	daos_key_t		 dkey;
	struct dcs_iod_csums	*iod_csum = NULL;
	daos_iod_t		 iod = {0};
	daos_recx_t		 recx = {0};
	d_sg_list_t		 sgl;
	size_t			 data_len;
	char			*data = NULL;
	int			 rc;

	oid.id_shard	= shard;
	oid.id_pad_32	= 0;
	oid.id_pub.lo = oid_lo;
	daos_obj_generate_id(&oid.id_pub, 0, oclass, 0);

	if (data_str != NULL) {
		data_len = strlen(data_str);
		D_ALLOC(data,  data_len);
		memcpy(data, data_str, data_len);
	} else {
		data_len = ctx->tsc_data_len;
		D_ALLOC(data, data_len);
		dts_buf_render(data, data_len);
	}

	iov_alloc_str(&iod.iod_name, akey_str);
	setup_iod_type(&iod, iod_type, data_len, &recx);

	d_sgl_init(&sgl, 1);
	d_iov_set(&sgl.sg_iovs[0], data, data_len);

	rc = daos_csummer_calc_iods(ctx->tsc_csummer, &sgl, &iod, NULL, 1,
				    false, NULL, 0, &iod_csum);
	assert_success(rc);
	if (corrupt_it) {
		uint32_t idx_to_corrupt = 0;

		if (iod.iod_type == DAOS_IOD_ARRAY) {
			/** corrupt last record */
			idx_to_corrupt = (iod.iod_recxs->rx_nr - 1)  *
				iod.iod_size;
		}

		((char *)sgl.sg_iovs[0].iov_buf)[idx_to_corrupt] += 2;
	}

	iov_alloc_str(&dkey, dkey_str);
	rc = vos_obj_update(ctx->tsc_coh, oid, epoch, 0, 0, &dkey, 1, &iod,
			    iod_csum, &sgl);
	assert_success(rc);

	/**
	 * make sure can fetch right after update. Even if data was corrupted,
	 * it should still fetch fine
	 */
	rc = sts_ctx_fetch(ctx, oclass, oid_lo, shard, iod_type,
			   dkey_str, akey_str, epoch, data_len);
	assert_success(rc);

	daos_csummer_free_ic(ctx->tsc_csummer, &iod_csum);

	D_FREE(dkey.iov_buf);
	D_FREE(iod.iod_name.iov_buf);
}

static void
sts_ctx_do_scrub(struct sts_context *ctx)
{
	assert_success(ds_obj_csum_scrub(ctx->tsc_coh, ctx->tsc_csummer,
					 ctx->tsc_credits_consumed_handler,
					 ctx->tsc_corruption_handler));
}

/**
 * ----------------------------------------------------------------------------
 * Tests
 * ----------------------------------------------------------------------------
 */
static void
scrubbing_with_no_corruption(void **state)
{
	struct sts_context *ctx = *state;

	/** setup data */
	sts_ctx_update(ctx, OC_SX, 1, 0, TEST_IOD_SINGLE, "dkey", "akey",
		       1, NULL, false);

	/** act */
	sts_ctx_do_scrub(ctx);

	/** verify after scrub value is still good */
	assert_success(
		sts_ctx_fetch(ctx, OC_SX, 1, 0, TEST_IOD_SINGLE, "dkey",
			      "akey", 1, 1024));

	assert_int_equal(1, progress_cb_count);
	assert_int_equal(0, corruption_cb_count);
}

static void
scrubbing_with_sv_corrupted(void **state)
{
	struct sts_context *ctx = *state;

	/** setup data */
	sts_ctx_update(ctx, OC_SX, 1, 0, TEST_IOD_SINGLE, "dkey", "akey",
		       1, NULL, true);

	/** act */
	sts_ctx_do_scrub(ctx);

	/** verify after scrub fetching the akey returns a csum error */
	assert_csum_error(
		sts_ctx_fetch(ctx, OC_SX, 1, 0, TEST_IOD_SINGLE, "dkey",
			      "akey", 1, 1024));

	assert_int_equal(1, progress_cb_count);
	assert_int_equal(1, corruption_cb_count);
}

static void
corrupted_extent(void **state)
{
	struct sts_context *ctx = *state;


	ctx->tsc_data_len = ctx->tsc_chunk_size * 2;
	/** setup data */
	sts_ctx_update(ctx, OC_SX, 1, 0, TEST_IOD_ARRAY_1, "dkey", "akey",
		       1, NULL, true);

	sts_ctx_do_scrub(ctx);

	assert_int_equal(2, progress_cb_count);
	assert_int_equal(1, corruption_cb_count);
}

static void
scrubbing_with_arrays_corrupted(void **state)
{
	struct sts_context *ctx = *state;

	/** setup data */
	sts_ctx_update(ctx, OC_SX, 1, 0, TEST_IOD_ARRAY_1, "dkey", "akey",
		       1, NULL, true);
	sts_ctx_update(ctx, OC_SX, 1, 0, TEST_IOD_ARRAY_2, "dkey", "akey-2",
		       1, NULL, true);
	sts_ctx_update(ctx, OC_SX, 1, 0, TEST_IOD_ARRAY_20,
		       "dkey", "akey-3", 1, NULL, true);

	/** act */
	sts_ctx_do_scrub(ctx);

	/** verify saw all errors */
	assert_int_equal(3, corruption_cb_count);

	/** verify after scrub fetching the akey values return csum errors */
	assert_csum_error(
		sts_ctx_fetch(ctx, OC_SX, 1, 0, TEST_IOD_ARRAY_1, "dkey",
			      "akey", 1, 1024));
	assert_csum_error(
		sts_ctx_fetch(ctx, OC_SX, 1, 0, TEST_IOD_ARRAY_2, "dkey",
			      "akey-2", 1, 1024));
	assert_csum_error(
		sts_ctx_fetch(ctx, OC_SX, 1, 0, TEST_IOD_ARRAY_20, "dkey",
			      "akey-3", 1, 1024));
}

static void
scrubbing_with_multiple_dkeys_akeys(void **state)
{
	struct sts_context *ctx = *state;

	sts_ctx_update(ctx, OC_SX, 1, 0, TEST_IOD_SINGLE, "dkey", "akey",
		       1, NULL, false);

	/** insert a corrupted value */
	sts_ctx_update(ctx, OC_SX, 1, 0, TEST_IOD_SINGLE, "dkey",
		       "akey-corrupted",
		       1, NULL, true);

	/** Cover corruption with write to later epoch */
	sts_ctx_update(ctx, OC_SX, 1, 0, TEST_IOD_SINGLE, "dkey",
		       "akey-corrupted", 2, NULL, false);

	sts_ctx_update(ctx, OC_SX, 1, 0, TEST_IOD_SINGLE, "dkey",
		       "akey-2", 1, NULL, false);

	sts_ctx_update(ctx, OC_SX, 1, 1, TEST_IOD_ARRAY_1, "dkey",
		       "akey-3", 1, NULL, false);

	sts_ctx_update(ctx, OC_SX, 1, 1, TEST_IOD_ARRAY_2, "dkey",
		       "akey-4", 1, NULL, false);

	/** Act */
	sts_ctx_do_scrub(ctx);

	/** corrupted akey should error */
	assert_csum_error(sts_ctx_fetch(ctx, OC_SX, 1, 0, TEST_IOD_SINGLE,
					"dkey", "akey-corrupted", 1, 1024));

	/** non-corrupted akey should still succeed */
	assert_success(sts_ctx_fetch(ctx, OC_SX, 1, 1, TEST_IOD_ARRAY_1,
				     "dkey", "akey", 1, 1024));
}

static int
sts_setup(void **state)
{
	struct sts_context	*ctx;

	D_ALLOC_PTR(ctx);

	assert_non_null(ctx);
	sts_ctx_init(ctx);
	*state = ctx;

	progress_cb_count = 0;
	corruption_cb_count = 0;

	return 0;
}

static int
sts_teardown(void **state)
{
	struct sts_context *ctx = *state;

	sts_ctx_fini(ctx);
	D_FREE(ctx);

	return 0;
}

#define	TS(desc, test_fn) \
	{ desc, test_fn, sts_setup, sts_teardown }

static const struct CMUnitTest scrubbing_tests[] = {
	TS("CSUM_SCRUBBING_01: WIP With no corruption",
	   scrubbing_with_no_corruption),
	TS("CSUM_SCRUBBING_02: With a single value corrupted",
	   scrubbing_with_sv_corrupted),
	TS("CSUM_SCRUBBING_03: With an corrupted extent value",
	   corrupted_extent),
	TS("CSUM_SCRUBBING_04: With an array value",
	   scrubbing_with_arrays_corrupted),
	TS("CSUM_SCRUBBING_05: Multiple keys",
	   scrubbing_with_multiple_dkeys_akeys),
};

int
run_scrubbing_tests()
{
	return cmocka_run_group_tests_name(
		"Storage and retrieval of checksums for Single Value Type",
		scrubbing_tests, NULL, NULL);

}
