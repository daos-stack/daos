/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

/*
 * ms_between_periods is a helper function for determining how much time to wait between
 * scrubs if mode is "timed"
 */
static void
ms_between_periods_tests(void **state)
{
#define ONE_SECOND_NS (1000000000) /* 1e+9 */
#define HALF_SECOND_NS (500000000) /* 5e+8 */
#define assert_ms_eq(exp, duration, periods, curr, elapsed_ns) do {  \
	struct timespec start;                                       \
	struct timespec elapsed;                                     \
	d_gettime(&start); elapsed = start;                          \
	d_timeinc(&elapsed, elapsed_ns);                             \
	assert_int_equal(exp,                                        \
		get_ms_between_periods(start, elapsed, duration,     \
		periods, curr));                                     \
	} while (false)

	/*
	 * ---------------------------------------------------------
	 * assert_ms_eq takes the following values in this order:
	 * Expected, duration, periods, current period, elapsed ns
	 * ---------------------------------------------------------
	 */

	/*
	 * First period, no time has elapsed, total of 10 periods in 10 seconds.
	 * Should be 1 second.
	 */
	assert_ms_eq(1000, 10, 10, 0, 0);

	/*
	 * With 10 periods and 10 second duration, then each period should
	 * take 1 second.
	 * if half a second has elapsed already for the first period, then only
	 * need to wait another half second
	 */
	assert_ms_eq(500, 10, 10, 0, HALF_SECOND_NS);

	/*
	 * With 10 periods and 10 second duration, then each period should
	 * take 1 second.
	 * if one second (or more) has elapsed already for the first period,
	 * then shouldn't wait at all
	 */
	assert_ms_eq(0, 10, 10, 0, ONE_SECOND_NS);
	assert_ms_eq(0, 10, 10, 0, ONE_SECOND_NS + HALF_SECOND_NS);

	/*
	 * With 10 periods and 10 second duration, then each period should
	 * take 1 second.
	 * if one and a half second has elapsed and in the second period,
	 * then should wait half a second
	 */
	assert_ms_eq(500, 10, 10, 1, ONE_SECOND_NS + HALF_SECOND_NS);

	/*
	 * Multiple tests with 5 periods into a 10 second duration
	 */
	assert_ms_eq(2000, 10, 5, 0, 0);
	assert_ms_eq(1750, 10, 5, 0, HALF_SECOND_NS / 2);
	assert_ms_eq(3750, 10, 5, 1, HALF_SECOND_NS / 2);

	/* No time has elapsed, but already done with all periods, plus
	 * some. Should wait full 10 seconds now, but not more
	 */
	assert_ms_eq(10000, 10, 5, 6, 0);
	assert_ms_eq(10000, 10, 5, 100, 0);

	/* What should wait be if duration isn't set and periods are not set */
	assert_ms_eq(0, 0, 0, 0, 0);

	/* periods is larger than duration in seconds */
	assert_ms_eq(908, 10, 11, 0, 1);
}

/**
 * Scrubbing tests are integration tests between checksum functionality
 * and VOS. VOS does not calculate any checksums so the checksums for the
 * data are calculated here in the tests, which makes it convenient for making
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
	TEST_IOD_ARRAY_1, /** DAOS_IOD_ARRAY with a single recx */
	TEST_IOD_ARRAY_2, /** DAOS_IOD_ARRAY with 2 recx, data split evenly */
	TEST_IOD_ARRAY_4, /** DAOS_IOD_ARRAY with 4 recx, data split evenly */
};

/**
 * setup the iod based on the iod test type. Will define the iod type, recxs
 * if an array with different record sizes and start indexes.
 */
static void
setup_iod_type(daos_iod_t *iod, int iod_type, daos_size_t data_len,
	       daos_recx_t *recx)
{
	int i;

	iod->iod_size = 1;

	switch (iod_type) {
	case TEST_IOD_SINGLE:
		iod->iod_type = DAOS_IOD_SINGLE;
		iod->iod_size = data_len;
		iod->iod_nr = 1;
		break;
	case TEST_IOD_ARRAY_1:
		iod->iod_nr = 1;
		iod->iod_recxs = recx;
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_recxs->rx_idx = 0;
		iod->iod_recxs->rx_nr = data_len;
		break;
	case TEST_IOD_ARRAY_2:
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_nr = 2;
		iod->iod_recxs = recx;
		for (i = 0; i < 2; i++) {
			iod->iod_recxs[i].rx_idx = 10 + i * data_len / 4;
			iod->iod_recxs[i].rx_nr = data_len / 4;
		}
		break;
	case TEST_IOD_ARRAY_4:
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_nr = 4;
		iod->iod_recxs = recx;
		for (i = 0; i < 4; i++) {
			iod->iod_recxs[i].rx_idx = 256 + i * data_len / 4;
			iod->iod_recxs[i].rx_nr = data_len / 4;
		}
		break;
	}
}

/** scrubbing test context */
struct sts_context {
	struct scrub_ctx	 tsc_scrub_ctx;
	char			 tsc_pmem_file[256];
	struct ds_pool		 tsc_pool;
	struct dss_module_info	 tsc_dmi;
	uuid_t			 tsc_pool_uuid;
	uuid_t			 tsc_cont_uuid;
	uint64_t		 tsc_scm_size;
	uint64_t		 tsc_nvme_size;
	daos_size_t		 tsc_chunk_size;
	daos_size_t		 tsc_data_len;
	daos_handle_t		 tsc_poh;
	daos_handle_t		 tsc_coh;
	struct daos_csummer	*tsc_csummer;
	sc_get_cont_fn_t	 tsc_get_cont_fn;
	sc_yield_fn_t		 tsc_yield_fn;
	sc_sleep_fn_t		 tsc_sleep_fn;
	sc_is_idle_fn_t		 tsc_is_idle_fn;
	void			*tsc_sched_arg;
	int			 tsc_fd;
	int			 tsc_expected_rc;
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
			fail_msg("fallocate failed: %d\n", rc);
		ctx->tsc_fd = fd;
	}

	/* Use pool size as blob size for this moment. */
	rc = vos_pool_create(pmem_file, ctx->tsc_pool_uuid, 0,
			     ctx->tsc_nvme_size, 0, &poh);
	assert_success(rc);

	ctx->tsc_poh = poh;
}

static void
sts_ctx_pool_fini(struct sts_context *ctx)
{
	int	rc;

	vos_pool_close(ctx->tsc_poh);
	rc = vos_pool_destroy(ctx->tsc_pmem_file, ctx->tsc_pool_uuid);
	D_ASSERTF(rc == 0 || rc == -DER_NONEXIST, "rc="DF_RC"\n", DP_RC(rc));
	close(ctx->tsc_fd);
}

static int
get_cont_fn(uuid_t pool_uuid, uuid_t cont_uuid, void *arg,
	    struct cont_scrub *cont)
{
	struct sts_context *ctx = arg;

	cont->scs_cont_csummer = ctx->tsc_csummer;
	cont->scs_cont_hdl = ctx->tsc_coh;
	uuid_copy(cont->scs_cont_uuid, cont_uuid);

	return 0;
}

static int
sts_ctx_cont_init(struct sts_context *ctx)
{
	daos_handle_t	coh = DAOS_HDL_INVAL;

	assert_success(vos_cont_create(ctx->tsc_poh, ctx->tsc_cont_uuid));
	assert_success(vos_cont_open(ctx->tsc_poh, ctx->tsc_cont_uuid, &coh));

	ctx->tsc_coh = coh;
	ctx->tsc_get_cont_fn = get_cont_fn;
	ctx->tsc_sched_arg = ctx;

	return 0;
}

static void
sts_ctx_cont_fini(struct sts_context *ctx)
{
	vos_cont_close(ctx->tsc_coh);
}


static struct daos_csummer *test_csummer;

static int
fake_yield(void *arg)
{
	return 0;
}

static bool fake_is_idle_result;
static bool
fake_is_idle()
{
	return fake_is_idle_result;
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

	uuid_parse("12345678-1234-1234-1234-123456789012", ctx->tsc_pool_uuid);
	uuid_parse("87654321-4321-4321-4321-210987654321", ctx->tsc_cont_uuid);
	sprintf(ctx->tsc_pmem_file, "/mnt/daos/vos_scrubbing.pmem");


	sts_ctx_pool_init(ctx);
	sts_ctx_cont_init(ctx);

	ctx->tsc_yield_fn = fake_yield;
	ctx->tsc_is_idle_fn = fake_is_idle;
	assert_success(
		daos_csummer_init_with_type(&test_csummer,
					    HASH_TYPE_CRC16,
					    ctx->tsc_chunk_size, false));
	ctx->tsc_csummer = test_csummer;
}

static void
sts_ctx_fini(struct sts_context *ctx)
{
	daos_csummer_destroy(&ctx->tsc_csummer);
	sts_ctx_cont_fini(ctx);
	sts_ctx_pool_fini(ctx);
}

static void
set_test_oid(daos_unit_oid_t *oid, uint64_t oid_lo)
{
	oid->id_shard	= 1;
	oid->id_layout_ver = 0;
	oid->id_padding = 0;
	oid->id_pub.lo = oid_lo;
	daos_obj_set_oid(&oid->id_pub, 0, OR_RP_1, MAX_NUM_GROUPS, 0);
}

static int
sts_ctx_fetch(struct sts_context *ctx, int oid_lo, int iod_type,
	      const char *dkey_str, const char *akey_str, int epoch)
{
	daos_unit_oid_t	 oid = {0};
	daos_key_t	 dkey;
	daos_recx_t	 recx[4] = {0};
	daos_iod_t	 iod = {0};
	d_sg_list_t	 sgl;
	uint64_t	 data_len;
	char		*data;
	int		 rc;

	data_len = ctx->tsc_data_len;
	D_ALLOC(data, data_len);
	assert_non_null(data);

	set_test_oid(&oid, oid_lo);

	iov_alloc_str(&iod.iod_name, akey_str);
	setup_iod_type(&iod, iod_type, data_len, recx);

	d_sgl_init(&sgl, 1);
	d_iov_set(&sgl.sg_iovs[0], data, data_len);

	iov_alloc_str(&dkey, dkey_str);
	rc = vos_obj_fetch(ctx->tsc_coh, oid, epoch, 0, &dkey, 1, &iod, &sgl);

	/* If no data was returned then let test know */
	if (rc == 0 && sgl.sg_nr_out == 0)
		rc = -DER_NONEXIST;


	D_FREE(dkey.iov_buf);
	daos_iov_free(&iod.iod_name);
	D_FREE(data);
	d_sgl_fini(&sgl, false);

	return rc;
}

static void
sts_ctx_update(struct sts_context *ctx, int oid_lo, int iod_type,
	       const char *dkey_str, const char *akey_str,
	       int epoch, bool corrupt_it)
{
	daos_unit_oid_t		 oid = {0};
	daos_key_t		 dkey;
	struct dcs_iod_csums	*iod_csum = NULL;
	daos_iod_t		 iod = {0};
	daos_recx_t		 recx[4] = {0};
	d_sg_list_t		 sgl;
	size_t			 data_len;
	char			*data = NULL;
	int			 rc;

	set_test_oid(&oid, oid_lo);

	data_len = ctx->tsc_data_len;
	D_ALLOC(data, data_len);
	dts_buf_render(data, data_len);

	iov_alloc_str(&iod.iod_name, akey_str);
	setup_iod_type(&iod, iod_type, data_len, recx);

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

		/* confirm corruption */
		rc = daos_csummer_verify_iod(ctx->tsc_csummer, &iod, &sgl, iod_csum, NULL, 0, NULL);
		assert_csum_error(rc);
	}

	iov_alloc_str(&dkey, dkey_str);
	rc = vos_obj_update(ctx->tsc_coh, oid, epoch, 0, 0, &dkey, 1, &iod, iod_csum, &sgl);
	assert_success(rc);

	/**
	 * make sure can fetch right after update. Even if data was corrupted,
	 * it should still fetch fine
	 */
	rc = sts_ctx_fetch(ctx, oid_lo, iod_type, dkey_str, akey_str, epoch);
	assert_success(rc);

	daos_csummer_free_ic(ctx->tsc_csummer, &iod_csum);

	D_FREE(data);
	D_FREE(dkey.iov_buf);
	daos_iov_free(&iod.iod_name);
	d_sgl_fini(&sgl, false);
}

int fake_target_drain_call_count;
static int
fake_target_drain(struct ds_pool *pool)
{
	fake_target_drain_call_count++;
	return 0;
}

static void
sts_ctx_punch_dkey(struct sts_context *ctx, int oid_lo, const char *dkey_str,
		   int epoch)
{
	daos_unit_oid_t		 oid = {0};
	daos_key_t		 dkey;
	int			 rc;

	set_test_oid(&oid, oid_lo);

	iov_alloc_str(&dkey, dkey_str);
	rc = vos_obj_punch(ctx->tsc_coh, oid, epoch, 0, 0, &dkey, 0,
			   NULL, NULL);
	assert_success(rc);

	D_FREE(dkey.iov_buf);
}

static void
clear_ctx(struct scrub_ctx *ctx)
{
	uuid_clear(ctx->sc_cont_uuid);
	memset(&ctx->sc_cur_oid, 0, sizeof(ctx->sc_cur_oid));
	memset(&ctx->sc_dkey, 0, sizeof(ctx->sc_dkey));
	memset(&ctx->sc_iod, 0, sizeof(ctx->sc_iod));
	ctx->sc_epoch = 0;
	ctx->sc_minor_epoch = 0;
}

static void
sts_ctx_setup_scrub_ctx(struct sts_context *ctx)
{
	uuid_copy(ctx->tsc_scrub_ctx.sc_pool_uuid, ctx->tsc_pool_uuid);
	ctx->tsc_scrub_ctx.sc_vos_pool_hdl = ctx->tsc_poh;
	ctx->tsc_scrub_ctx.sc_sleep_fn = NULL;
	ctx->tsc_scrub_ctx.sc_yield_fn = ctx->tsc_yield_fn;
	ctx->tsc_scrub_ctx.sc_sleep_fn = ctx->tsc_sleep_fn;
	ctx->tsc_scrub_ctx.sc_is_idle_fn = ctx->tsc_is_idle_fn;
	ctx->tsc_scrub_ctx.sc_sched_arg = ctx->tsc_sched_arg;
	ctx->tsc_scrub_ctx.sc_cont_lookup_fn = ctx->tsc_get_cont_fn;
	ctx->tsc_scrub_ctx.sc_drain_pool_tgt_fn = fake_target_drain;
	ctx->tsc_scrub_ctx.sc_pool = &ctx->tsc_pool;
	ctx->tsc_scrub_ctx.sc_dmi = &ctx->tsc_dmi;
	ctx->tsc_scrub_ctx.sc_cont.scs_props_fetched = true;
}

static void
sts_ctx_do_scrub(struct sts_context *ctx)
{
	sts_ctx_setup_scrub_ctx(ctx);

	clear_ctx(&ctx->tsc_scrub_ctx);

	assert_rc_equal(ctx->tsc_expected_rc, vos_scrub_pool(&ctx->tsc_scrub_ctx));
}

/**
 * ----------------------------------------------------------------------------
 * Tests
 * ----------------------------------------------------------------------------
 */

static void
lazy_scrubbing_only_when_idle(void **state)
{
	struct sts_context *ctx = *state;

	/* setup data with corruption */
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1, true);
	ctx->tsc_pool.sp_scrub_mode = DAOS_SCRUB_MODE_LAZY;

	/* When not idle it shouldn't run the scrubber and won't find corruption */
	fake_is_idle_result = false;
	sts_ctx_do_scrub(ctx);
	/* value is still good because didn't actually scrub */
	assert_success(sts_ctx_fetch(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1));

	/* When idle it should now run the scrubber */
	fake_is_idle_result = true;
	sts_ctx_do_scrub(ctx);
	assert_csum_error(sts_ctx_fetch(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1));
}

static void
scrubbing_with_no_corruption_sv(void **state)
{
	struct sts_context *ctx = *state;

	/** setup data */
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1, false);

	/** act */
	ctx->tsc_pool.sp_scrub_mode = DAOS_SCRUB_MODE_LAZY;
	sts_ctx_do_scrub(ctx);

	/** verify after scrub value is still good */
	assert_success(sts_ctx_fetch(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1));
}

static void
scrubbing_with_no_corruption_array(void **state)
{
	struct sts_context *ctx = *state;

	ctx->tsc_data_len = 1024 * 1024;
	ctx->tsc_chunk_size = 1024;

	/** setup data */
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey", 1, false);

	/** act */
	sts_ctx_do_scrub(ctx);

	/** verify after scrub value is still good */
	assert_success(sts_ctx_fetch(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey", 1));
}

static void
scrubbing_with_sv_corrupted(void **state)
{
	struct sts_context *ctx = *state;

	/** setup data */
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1, true);

	/** act */
	sts_ctx_do_scrub(ctx);

	/** verify after scrub fetching the akey returns a csum error */
	assert_csum_error(sts_ctx_fetch(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1));
}

static void
corrupted_extent(void **state)
{
	struct sts_context *ctx = *state;


	ctx->tsc_data_len = ctx->tsc_chunk_size * 2;
	/** setup data */
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey", 1, true);

	sts_ctx_do_scrub(ctx);

	assert_csum_error(sts_ctx_fetch(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey", 1));
}

static void
scrubbing_with_arrays_corrupted(void **state)
{
	struct sts_context *ctx = *state;

	/** setup data */
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey-1", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_2, "dkey", "akey-2", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_4, "dkey", "akey-4", 1, true);

	/** act */
	sts_ctx_do_scrub(ctx);

	/** verify after scrub fetching the akey values return csum errors */
	assert_csum_error(sts_ctx_fetch(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey-1", 1));
	assert_csum_error(sts_ctx_fetch(ctx, 1, TEST_IOD_ARRAY_2, "dkey", "akey-2", 1));
	assert_csum_error(sts_ctx_fetch(ctx, 1, TEST_IOD_ARRAY_4, "dkey", "akey-4", 1));
}

static void
scrub_multiple_epochs(void **state)
{
	struct sts_context *ctx = *state;

	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1, false);

	/** insert a corrupted value */
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey-corrupted", 1, true);

	/** Cover corruption with write to later epoch */
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey-corrupted", 2, false);

	/** Act */
	sts_ctx_do_scrub(ctx);

	/** corrupted akey should error */
	assert_csum_error(sts_ctx_fetch(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey-corrupted", 1));

	/** non-corrupted akey should still succeed */
	assert_success(sts_ctx_fetch(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 2));
}

static void
scrubbing_with_multiple_akeys(void **state)
{
	struct sts_context *ctx = *state;

	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey-1", 1, false);
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey-2", 1, false);
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey-3", 1, false);
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_2, "dkey", "akey-4", 1, false);

	/** Act */
	sts_ctx_do_scrub(ctx);

	assert_success(sts_ctx_fetch(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey-1", 1));
	assert_success(sts_ctx_fetch(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey-2", 1));
	assert_success(sts_ctx_fetch(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey-3", 1));
	assert_success(sts_ctx_fetch(ctx, 1, TEST_IOD_ARRAY_2, "dkey", "akey-4", 1));
}

static void
scrubbing_with_good_akey_then_bad_akey(void **state)
{
	struct sts_context *ctx = *state;

	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1, false);

	sts_ctx_do_scrub(ctx);
	assert_success(sts_ctx_fetch(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1));
	ctx->tsc_scrub_ctx.sc_pool_start_scrub.tv_sec -= 10;

	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1, true);
	sts_ctx_do_scrub(ctx);
	assert_csum_error(sts_ctx_fetch(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1));
}

static int
test_yield_deletes_extent(void *arg)
{
	struct sts_context	*ctx = arg;
	int			 rc;
	daos_epoch_range_t	 epr = {.epr_lo = 0,
		.epr_hi = DAOS_EPOCH_MAX - 1};
	daos_unit_oid_t		 oid = {0};

	oid.id_shard	= 0;
	oid.id_layout_ver = 0;
	oid.id_pub.lo = 1;
	daos_obj_set_oid(&oid.id_pub, 0, OR_RP_1, MAX_NUM_GROUPS, 0);

	/* insert another extent at a later epoch so the original extent is
	 * deleted by vos_aggregation.
	 */
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey", 2, false);

	rc = vos_aggregate(ctx->tsc_coh, &epr, NULL, NULL,
			   VOS_AGG_FL_FORCE_SCAN | VOS_AGG_FL_FORCE_MERGE);
	assert_success(rc);

	return 0;
}

static int
test_sleep_deletes_extent(void *arg, uint32_t msec)
{
	return test_yield_deletes_extent(arg);
}

static void
extent_deleted_by_aggregation(void **state)
{
	struct sts_context *ctx = *state;

	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey", 1, true);

	ctx->tsc_yield_fn = test_yield_deletes_extent;
	ctx->tsc_sleep_fn = test_sleep_deletes_extent;
	ctx->tsc_sched_arg = ctx;

	sts_ctx_do_scrub(ctx);

	/* First epoch should not exist */
	assert_rc_equal(sts_ctx_fetch(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey", 1), -DER_NONEXIST);
	/* Second (inserted by test_yield_deletes_extent) should now exist */
	assert_success(sts_ctx_fetch(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey", 2));
}

static int
test_yield_deletes_dkey(void *arg)
{
	struct sts_context	*ctx = arg;
	int			 rc;
	daos_epoch_range_t	 epr = {.epr_lo = 0, .epr_hi = DAOS_EPOCH_MAX - 1};

	sts_ctx_punch_dkey(ctx, 1, "dkey", 2);

	rc = vos_aggregate(ctx->tsc_coh, &epr, NULL, NULL,
			   VOS_AGG_FL_FORCE_SCAN | VOS_AGG_FL_FORCE_MERGE);
	assert_success(rc);

	return 0;
}

static void
dkey_deleted_by_aggregation_with_multiple_extents(void **state)
{
	struct sts_context *ctx = *state;

	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_4, "dkey", "akey",
		       1, false);

	ctx->tsc_yield_fn = test_yield_deletes_dkey;
	ctx->tsc_sched_arg = ctx;

	sts_ctx_do_scrub(ctx);
}

static void
dkey_deleted_by_aggregation_with_multiple_akeys(void **state)
{
	struct sts_context *ctx = *state;

	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey1", 1, false);
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey2", 1, false);
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey3", 1, false);

	ctx->tsc_yield_fn = test_yield_deletes_dkey;
	ctx->tsc_sched_arg = ctx;

	sts_ctx_do_scrub(ctx);
}

static int
test_yield_deletes_container(void *arg)
{
	struct sts_context	*ctx = arg;
	int			 rc;

	rc = vos_cont_destroy(ctx->tsc_poh, ctx->tsc_cont_uuid);
	assert_rc_equal(-DER_BUSY, rc);

	return 0;
}

static void
container_deleted(void **state)
{
	struct sts_context *ctx = *state;

	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey1", 1, false);
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey2", 1, false);
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey3", 1, false);

	ctx->tsc_yield_fn = test_yield_deletes_container;
	ctx->tsc_sched_arg = ctx;

	sts_ctx_do_scrub(ctx);
}

static void
multiple_objects(void **state)
{
	struct sts_context *ctx = *state;

	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1, false);
	sts_ctx_update(ctx, 2, TEST_IOD_SINGLE, "dkey", "akey", 1, false);
	sts_ctx_update(ctx, 3, TEST_IOD_SINGLE, "dkey", "akey", 1, false);
	sts_ctx_update(ctx, 4, TEST_IOD_SINGLE, "dkey", "akey", 1, true);

	sts_ctx_do_scrub(ctx);

	assert_success(sts_ctx_fetch(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey", 1));
	assert_success(sts_ctx_fetch(ctx, 2, TEST_IOD_SINGLE, "dkey", "akey", 1));
	assert_success(sts_ctx_fetch(ctx, 3, TEST_IOD_SINGLE, "dkey", "akey", 1));
	assert_csum_error(sts_ctx_fetch(ctx, 4, TEST_IOD_SINGLE, "dkey", "akey", 1));
}

static void
drain_target(void **state)
{
	struct sts_context *ctx = *state;

	ctx->tsc_pool.sp_scrub_thresh = 4;

	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey1", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey2", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey3", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey4", 1, true);

	ctx->tsc_expected_rc = -DER_SHUTDOWN;
	sts_ctx_do_scrub(ctx);
	assert_int_equal(1, fake_target_drain_call_count);

}

static void
no_drain_target(void **state)
{
	struct sts_context *ctx = *state;

	ctx->tsc_pool.sp_scrub_thresh = 0;
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey0", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey1", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey2", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey3", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey4", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey5", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey6", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey7", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey8", 1, true);
	sts_ctx_update(ctx, 1, TEST_IOD_SINGLE, "dkey", "akey9", 1, true);

	sts_ctx_do_scrub(ctx);

	assert_int_equal(0, fake_target_drain_call_count);
}

static void
multiple_overlapping_extents(void **state)
{
	struct sts_context *ctx = *state;

	/*
	 * this will create 3 overlapping extents all starting at idx 0
	 * but different lengths
	 */
	ctx->tsc_data_len = 2048;
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey", 1, false);
	ctx->tsc_data_len = 1024;
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey", 2, false);
	ctx->tsc_data_len = 512;
	sts_ctx_update(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey", 3, false);

	sts_ctx_do_scrub(ctx);

	ctx->tsc_data_len = 2048;
	assert_success(sts_ctx_fetch(ctx, 1, TEST_IOD_ARRAY_1, "dkey", "akey", 3));
}

static int
sts_setup(void **state)
{
	struct sts_context	*ctx;

	D_ALLOC_PTR(ctx);

	assert_non_null(ctx);
	sts_ctx_init(ctx);
	*state = ctx;

	/* set some defaults */
	ctx->tsc_pool.sp_scrub_mode = DAOS_SCRUB_MODE_LAZY;
	ctx->tsc_pool.sp_scrub_freq_sec = 1;
	ctx->tsc_pool.sp_scrub_thresh = 10;
	fake_target_drain_call_count = 0;
	fake_is_idle_result = true; /* should be idle most of the time */

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
	TS("calculate time between periods", ms_between_periods_tests),
	TS("CSUM_SCRUBBING_00: Only scrub when idle",
	   lazy_scrubbing_only_when_idle),
	TS("CSUM_SCRUBBING_01: SV with no corruption",
	   scrubbing_with_no_corruption_sv),
	TS("CSUM_SCRUBBING_02: Array with no corruption",
	   scrubbing_with_no_corruption_array),
	TS("CSUM_SCRUBBING_03: A single value corrupted",
	   scrubbing_with_sv_corrupted),
	TS("CSUM_SCRUBBING_04: A corrupted extent value",
	   corrupted_extent),
	TS("CSUM_SCRUBBING_05: Multiple corrupted extent values",
	   scrubbing_with_arrays_corrupted),
	TS("CSUM_SCRUBBING_06: Scrub multiple epochs",
	   scrub_multiple_epochs),
	TS("CSUM_SCRUBBING_06: Multiple keys, some corrupted values",
	   scrubbing_with_multiple_akeys),
	TS("CSUM_SCRUBBING_07: Multiple writes to same key, then corruption",
	   scrubbing_with_good_akey_then_bad_akey),
	TS("CSUM_SCRUBBING_08: Extent is deleted during scrub while yielding",
	   extent_deleted_by_aggregation),
	TS("CSUM_SCRUBBING_09.1: whole dkey is deleted during scrub while "
	   "yielding",
	   dkey_deleted_by_aggregation_with_multiple_extents),
	TS("CSUM_SCRUBBING_09.2: whole dkey is deleted during scrub while "
	   "yielding",
	   dkey_deleted_by_aggregation_with_multiple_akeys),
	TS("CSUM_SCRUBBING_09.3: Scrubbing has reference to container so "
	   "vos_cont_destroy will return DER_BUSY.",
	   container_deleted),
	TS("CSUM_SCRUBBING_10: Scrubbing multiple objects",
	   multiple_objects),
	TS("CSUM_SCRUBBING_11: When threshold is 0, no drain happens",
	   no_drain_target),
	TS("CSUM_SCRUBBING_12: Scrubbing multiple overlapping extents",
	   multiple_overlapping_extents),
	TS("CSUM_SCRUBBING_13: Evict pool target when threshold is exceeded",
	   drain_target),
};

int
run_scrubbing_tests(int argc, char *argv[])
{
	int rc = 0;

#if CMOCKA_FILTER_SUPPORTED == 1 /** for cmocka filter(requires cmocka 1.1.5) */
	char	 filter[1024];

	if (argc > 1) {
		snprintf(filter, 1024, "*%s*", argv[1]);
		cmocka_set_test_filter(filter);
	}
#endif

	rc += cmocka_run_group_tests_name(
		"Storage and retrieval of checksums for Single Value Type",
		scrubbing_tests, NULL, NULL);

	return rc;
}

int
main(int argc, char *argv[])
{
	int rc;

	assert_success(daos_debug_init(DAOS_LOG_DEFAULT));
	rc = vos_self_init("/mnt/daos", false, BIO_STANDALONE_TGT_ID);
	if (rc != 0) {
		print_error("Error initializing VOS instance: "DF_RC"\n",
			    DP_RC(rc));
		daos_debug_fini();
		return rc;
	}

	rc = run_scrubbing_tests(argc, argv);

	vos_self_fini();
	daos_debug_fini();

	return rc;
}
