/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * Tests of the WAL inspection commands (DAOS-17817) against a real VOS pool.
 *
 * Unlike the other ddb_tests groups, these groups initialize VOS once in their group setup and
 * hold that reference until the group teardown. This lets the same tests run against a PMEM VOS
 * (the default 'ddb' utest suite) and against a MD-on-SSD VOS (the 'ddb_md_on_ssd' utest suite,
 * where run_utest.py provides an AIO bdev with the META and WAL roles) without ever
 * re-initializing SPDK: the ddb_run_wal_open()/ddb_run_wal_close() calls only increase/decrease
 * the VOS reference count.
 *
 * In MD-on-SSD mode, the SPDK re-initialization guard (ddb_spdk_reinit_wa.c) only lets the first
 * NVMe-backed session of the process proceed, whether it succeeds or not. Each group therefore
 * runs in its own process (ddb_tests --suites f, then --suites g) and holds the one and only
 * NVMe-backed wal_open attempt of its process:
 * - group 'f' (ddb_wal_tests_run) opens a WAL session on a healthy pool;
 * - group 'g' (ddb_wal_bad_hdr_tests_run) stages a WAL header which is checksum-valid but
 *   inconsistent on its pool and checks that wal_open refuses it without aborting.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include <daos/tests_lib.h>
#include <daos_srv/bio.h>
#include <daos_srv/smd.h>
#include <daos_srv/vos.h>
#include <bio_wal.h>
#include <vos_internal.h>
#include <ddb.h>
#include <ddb_common.h>

#include "ddb_cmocka.h"
#include "ddb_test_driver.h"

#define WAL_TEST_DB_PATH   "/mnt/daos"
#define WAL_TEST_POOL_UUID "17817000-1957-4000-8000-000000000001"
#define WAL_BAD_HDR_UUID   "17817000-1957-4000-8000-000000000002"
#define WAL_TEST_POOL_SIZE (256ULL << 20)

struct wal_test_ctx {
	uuid_t                   wt_pool_uuid;
	char                     wt_pool_dir[128];
	char                     wt_vos_path[128];
	bool                     wt_md_on_ssd;
	/* Group 'g' only: BIO session of the fixture and the header it has replaced on the blob */
	struct bio_meta_context *wt_mc;
	struct wal_header        wt_orig_hdr;
};

static void
wal_test_ctx_init(struct ddb_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->dc_io_ft.ddb_print_message = dvt_fake_print;
	ctx->dc_io_ft.ddb_print_error   = dvt_fake_print;
	ctx->dc_poh                     = DAOS_HDL_INVAL;
	dvt_fake_print_reset();
}

/*
 * -----------------------------------------------
 * Setup & Teardown
 * -----------------------------------------------
 */

/* Initialize VOS and create the test pool with a container and one committed update */
static struct wal_test_ctx *
wal_test_pool_setup(const char *pool_uuid_str)
{
	struct wal_test_ctx *tctx;
	daos_handle_t        poh, coh;
	int                  fd;
	int                  rc;

	D_ALLOC_PTR(tctx);
	assert_non_null(tctx);

	assert_success(vos_self_init(WAL_TEST_DB_PATH, false, 0));
	tctx->wt_md_on_ssd = bio_nvme_configured(SMD_DEV_TYPE_META);
	print_message("WAL tests running in %s mode\n", tctx->wt_md_on_ssd ? "MD-on-SSD" : "PMEM");

	assert_success(uuid_parse(pool_uuid_str, tctx->wt_pool_uuid));
	rc = snprintf(tctx->wt_pool_dir, sizeof(tctx->wt_pool_dir), "%s/%s", WAL_TEST_DB_PATH,
		      pool_uuid_str);
	assert_true(rc > 0 && rc < sizeof(tctx->wt_pool_dir));
	rc = snprintf(tctx->wt_vos_path, sizeof(tctx->wt_vos_path), "%s/%s/vos-0", WAL_TEST_DB_PATH,
		      pool_uuid_str);
	assert_true(rc > 0 && rc < sizeof(tctx->wt_vos_path));
	rc = mkdir(tctx->wt_pool_dir, 0700);
	assert_true(rc == 0 || errno == EEXIST);

	fd = open(tctx->wt_vos_path, O_CREAT | O_TRUNC | O_RDWR, 0666);
	assert_true(fd >= 0);
	assert_success(fallocate(fd, 0, 0, WAL_TEST_POOL_SIZE));
	close(fd);

	/*
	 * VOS_POF_EXTERNAL_CHKPT disables the inline checkpointing: in MD-on-SSD mode, the
	 * transactions below stay in the WAL for the dump commands to find.
	 */
	assert_success(vos_pool_create(tctx->wt_vos_path, tctx->wt_pool_uuid, 0 /* scm_sz */,
				       0 /* data_sz */, 0 /* meta_sz */, VOS_POF_EXTERNAL_CHKPT,
				       0 /* version */, NULL));
	assert_success(
	    vos_pool_open(tctx->wt_vos_path, tctx->wt_pool_uuid, VOS_POF_EXTERNAL_CHKPT, &poh));
	assert_success(vos_cont_create(poh, g_uuids[0]));
	assert_success(vos_cont_open(poh, g_uuids[0], &coh));
	dvt_vos_insert_single(coh, g_oids[0], g_dkeys_str[0], g_akeys_str[0], "wal_test_data", 1);
	assert_success(vos_cont_close(coh));
	assert_success(vos_pool_close(poh));

	return tctx;
}

static void
wal_test_pool_teardown(struct wal_test_ctx *tctx)
{
	assert_success(vos_pool_destroy(tctx->wt_vos_path, tctx->wt_pool_uuid));
	vos_self_fini();

	D_FREE(tctx);
}

static int
wal_tests_setup(void **state)
{
	*state = wal_test_pool_setup(WAL_TEST_POOL_UUID);

	return 0;
}

static int
wal_tests_teardown(void **state)
{
	struct wal_test_ctx *tctx = *state;

	if (tctx == NULL) {
		fail_msg("Test context not setup correctly");
		return -DER_UNKNOWN;
	}

	wal_test_pool_teardown(tctx);

	return 0;
}

/*
 * Write hdr, with its checksum recomputed, at the first block of the WAL blob of the session mc.
 * The in-memory copy of the header held by the session is left untouched.
 */
static void
wal_test_hdr_write(struct bio_meta_context *mc, struct wal_header *hdr)
{
	struct hash_ft *algo = mc->mc_csum_algo;
	bio_addr_t      addr = {0};
	d_iov_t         iov;

	assert_non_null(algo);
	assert_success(algo->cf_reset(mc->mc_csum_ctx));
	assert_success(
	    algo->cf_update(mc->mc_csum_ctx, (uint8_t *)hdr, sizeof(*hdr) - sizeof(hdr->wh_csum)));
	assert_success(
	    algo->cf_finish(mc->mc_csum_ctx, (uint8_t *)&hdr->wh_csum, sizeof(hdr->wh_csum)));

	bio_addr_set(&addr, DAOS_MEDIA_NVME, 0);
	d_iov_set(&iov, hdr, sizeof(*hdr));
	assert_success(bio_write(mc->mc_wal, addr, &iov));
}

static int
wal_bad_hdr_tests_setup(void **state)
{
	struct wal_test_ctx *tctx;
	struct wal_header    hdr;

	tctx = wal_test_pool_setup(WAL_BAD_HDR_UUID);
	if (tctx->wt_md_on_ssd) {
		/*
		 * The fixture keeps its own BIO session open for the whole group: it holds the
		 * original header in memory and, being read-only, never writes it back, while the
		 * forged header staged on the blob is what wal_open() gets to verify.
		 */
		assert_success(bio_mc_open(vos_xsctxt_get(), tctx->wt_pool_uuid, 0, &tctx->wt_mc));
		assert_non_null(tctx->wt_mc);
		tctx->wt_orig_hdr = tctx->wt_mc->mc_wal_info.si_header;

		/*
		 * A checkpoint ID beyond the committed ID, with a block count making it look like
		 * a real checkpoint: bio_mc_open() asserts on such a header.
		 */
		hdr             = tctx->wt_orig_hdr;
		hdr.wh_ckp_id   = hdr.wh_commit_id + 1;
		hdr.wh_ckp_blks = hdr.wh_commit_blks > 0 ? hdr.wh_commit_blks : 1;
		wal_test_hdr_write(tctx->wt_mc, &hdr);
	}
	*state = tctx;

	return 0;
}

static int
wal_bad_hdr_tests_teardown(void **state)
{
	struct wal_test_ctx *tctx = *state;
	struct wal_header    hdr;

	if (tctx == NULL) {
		fail_msg("Test context not setup correctly");
		return -DER_UNKNOWN;
	}

	if (tctx->wt_mc != NULL) {
		hdr = tctx->wt_orig_hdr;
		wal_test_hdr_write(tctx->wt_mc, &hdr);
		assert_success(bio_mc_close(tctx->wt_mc));
		tctx->wt_mc = NULL;
	}
	wal_test_pool_teardown(tctx);

	return 0;
}

/*
 * -----------------------------------------------
 * Test Functions
 * -----------------------------------------------
 */

/* wal_close without a WAL session is an error, whatever the VOS mode. */
static void
wal_close_no_session_test(void **state)
{
	struct ddb_ctx ctx;

	wal_test_ctx_init(&ctx);

	assert_rc_equal(ddb_run_wal_close(&ctx), -DER_NO_HDL);
	assert_printed_contains("No WAL session is open");
	assert_false(ddb_wal_is_open(&ctx));
}

/* A PMEM VOS target has no WAL: wal_open must be refused and leave the context untouched. */
static void
wal_open_pmem_test(void **state)
{
	struct wal_test_ctx    *tctx = *state;
	struct ddb_ctx          ctx;
	struct wal_open_options wal_opt = {.path = tctx->wt_vos_path};

	if (tctx->wt_md_on_ssd) {
		print_message("Skipping PMEM-only test in MD-on-SSD mode\n");
		skip();
	}

	wal_test_ctx_init(&ctx);

	assert_rc_equal(ddb_run_wal_open(&ctx, &wal_opt), -DER_NOTSUPPORTED);
	assert_printed_contains("is not a MD-on-SSD target");
	assert_false(ddb_wal_is_open(&ctx));
	assert_false(ctx.dc_write_mode);

	/* wal_close is still an error, and the pool commands are not blocked */
	assert_rc_equal(ddb_run_wal_close(&ctx), -DER_NO_HDL);
	assert_rc_equal(ddb_run_wal_open(&ctx, &wal_opt), -DER_NOTSUPPORTED);
}

/*
 * The one and only real WAL session of the process (MD-on-SSD only): open, check the mutual
 * exclusion with the pool-lifecycle commands, close, then check the SPDK re-initialization guard.
 */
static void
wal_session_test(void **state)
{
	struct wal_test_ctx    *tctx = *state;
	struct ddb_ctx          ctx;
	struct wal_open_options wal_opt  = {.path = tctx->wt_vos_path};
	struct open_options     open_opt = {.path = tctx->wt_vos_path};
	daos_handle_t           poh;

	if (!tctx->wt_md_on_ssd) {
		print_message("Skipping MD-on-SSD-only test in PMEM mode\n");
		skip();
	}

	wal_test_ctx_init(&ctx);

	/* Open the WAL (no replay) */
	assert_success(ddb_run_wal_open(&ctx, &wal_opt));
	assert_true(ddb_wal_is_open(&ctx));
	assert_false(ctx.dc_write_mode);
	assert_printed_contains("opened (no replay)");

	/* A WAL session excludes another WAL session and the pool-lifecycle commands */
	dvt_fake_print_reset();
	assert_rc_equal(ddb_run_wal_open(&ctx, &wal_opt), -DER_BUSY);
	assert_printed_contains("A WAL session is open");
	assert_rc_equal(ddb_run_open(&ctx, &open_opt), -DER_BUSY);
	assert_true(daos_handle_is_inval(ctx.dc_poh));
	assert_true(ddb_wal_is_open(&ctx));

	/* Close the WAL session */
	assert_success(ddb_run_wal_close(&ctx));
	assert_false(ddb_wal_is_open(&ctx));
	assert_rc_equal(ddb_run_wal_close(&ctx), -DER_NO_HDL);

	/* The pool is left intact and can still be opened (and replayed) by VOS */
	assert_success(
	    vos_pool_open(tctx->wt_vos_path, tctx->wt_pool_uuid, VOS_POF_EXTERNAL_CHKPT, &poh));
	assert_success(vos_pool_close(poh));

	/* Only one NVMe-backed session per process: the guard refuses the next one */
	dvt_fake_print_reset();
	assert_rc_equal(ddb_run_wal_open(&ctx, &wal_opt), -DER_NO_SERVICE);
	assert_printed_contains("SPDK cannot be re-initialized");
	assert_false(ddb_wal_is_open(&ctx));
}

/*
 * A WAL header with a valid checksum but an inconsistent content is refused by wal_open with an
 * error instead of an abort (MD-on-SSD only). This is the one NVMe-backed wal_open attempt of the
 * process, so the control check, once the original header is restored, goes through BIO directly.
 */
static void
wal_open_bad_hdr_test(void **state)
{
	struct wal_test_ctx    *tctx = *state;
	struct ddb_ctx          ctx;
	struct wal_open_options wal_opt = {.path = tctx->wt_vos_path};
	struct wal_header       hdr;

	if (!tctx->wt_md_on_ssd) {
		print_message("Skipping MD-on-SSD-only test in PMEM mode\n");
		skip();
	}

	wal_test_ctx_init(&ctx);

	assert_rc_equal(ddb_run_wal_open(&ctx, &wal_opt), -DER_DF_INVAL);
	assert_printed_contains("Inconsistent meta or WAL header for '");
	assert_printed_contains("wal_repair_hdr");
	assert_false(ddb_wal_is_open(&ctx));
	assert_false(ctx.dc_write_mode);

	/* The forged header was the only cause */
	hdr = tctx->wt_orig_hdr;
	wal_test_hdr_write(tctx->wt_mc, &hdr);
	assert_success(bio_mc_hdr_verify(vos_xsctxt_get(), tctx->wt_pool_uuid, 0));
}

/*
 * -----------------------------------------------
 * Execute
 * -----------------------------------------------
 */
#define TEST(x) {#x, x, NULL, NULL}

static const struct CMUnitTest wal_tests[] = {
    TEST(wal_close_no_session_test),
    TEST(wal_open_pmem_test),
    TEST(wal_session_test),
};

static const struct CMUnitTest wal_bad_hdr_tests[] = {
    TEST(wal_open_bad_hdr_test),
};

int
ddb_wal_tests_run(void)
{
	return cmocka_run_group_tests_name("DDB WAL commands tests", wal_tests, wal_tests_setup,
					   wal_tests_teardown);
}

int
ddb_wal_bad_hdr_tests_run(void)
{
	return cmocka_run_group_tests_name("DDB WAL corrupted header tests", wal_bad_hdr_tests,
					   wal_bad_hdr_tests_setup, wal_bad_hdr_tests_teardown);
}
