/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <daos_types.h>
#include <daos/mem.h>
#include <daos_errno.h>
#include <daos_srv/vos.h>
#include <daos/tests_lib.h>

#include "vos_internal.h"

/* mocks */

#define DTX_CMT_BLOB_MAGIC 0x2502191c
#define DBD_BLOBS_CAP      0x3
#define DBD_BLOB_DF_CAP    0x8
#define CMT_TIME_START     0x1000
#define CMT_TIME_END       0x10000
#define CMT_TIME_STEP      0x10
#define CELL_SIZE                                                                                  \
	sizeof(struct vos_dtx_blob_df) + DBD_BLOB_DF_CAP * sizeof(struct vos_dtx_cmt_ent_df)

static const daos_epoch_t      epoch_offsets[] = {0x20000, 0x40000};

static struct vos_pool         mock_pool;
static struct vos_container    mock_cont;
static struct vos_cont_df      mock_cont_df;
static struct vos_dtx_blob_df *mock_dbds[DBD_BLOBS_CAP];
static umem_off_t              mock_dbds_off[DBD_BLOBS_CAP];
static daos_handle_t           mock_coh;

/* Helpers */

static void
prep_dtx_entries(void)
{
	int      i;
	uint64_t cmt_time = CMT_TIME_START;

	for (i = 0; i < DBD_BLOBS_CAP; i++) {
		int j;

		for (j = 0; j < DBD_BLOB_DF_CAP; j++) {
			struct vos_dtx_cmt_ent_df *dce_df;

			dce_df               = &mock_dbds[i]->dbd_committed_data[j];
			dce_df->dce_cmt_time = cmt_time;
			dce_df->dce_epoch    = cmt_time + epoch_offsets[j % 2];
			cmt_time += CMT_TIME_STEP;
		}
		mock_dbds[i]->dbd_count = DBD_BLOB_DF_CAP;
	}
	mock_cont.vc_dtx_committed_count = DBD_BLOBS_CAP * DBD_BLOB_DF_CAP;
	mock_pool.vp_dtx_committed_count = DBD_BLOBS_CAP * DBD_BLOB_DF_CAP;
}

/* setup & teardown */

static int
test_setup(void **unused)
{
	int i;

	memset(&mock_pool, 0, sizeof(mock_pool));
	memset(&mock_cont, 0, sizeof(mock_cont));
	memset(&mock_cont_df, 0, sizeof(mock_cont_df));

	for (i = 0; i < DBD_BLOBS_CAP; i++) {
		D_ALLOC(mock_dbds[i], CELL_SIZE);
		assert_non_null(mock_dbds[i]);
		mock_dbds_off[i] = umem_ptr2off(&mock_pool.vp_umm, mock_dbds[i]);
	}

	for (i = 0; i < DBD_BLOBS_CAP; i++) {
		mock_dbds[i]->dbd_magic = DTX_CMT_BLOB_MAGIC;
		mock_dbds[i]->dbd_cap   = DBD_BLOB_DF_CAP;
		mock_dbds[i]->dbd_next =
		    (i == DBD_BLOBS_CAP - 1) ? UMOFF_NULL : mock_dbds_off[i + 1];
		mock_dbds[i]->dbd_prev  = (i == 0) ? UMOFF_NULL : mock_dbds_off[i - 1];
		mock_dbds[i]->dbd_count = (i + 1) * 2;
		assert_true(mock_dbds[i]->dbd_count <= DBD_BLOB_DF_CAP);
	}

	mock_cont.vc_pool                  = &mock_pool;
	mock_cont.vc_cont_df               = &mock_cont_df;
	mock_cont_df.cd_dtx_committed_head = mock_dbds_off[0];
	mock_cont_df.cd_dtx_committed_tail = mock_dbds_off[DBD_BLOBS_CAP - 1];
	mock_coh.cookie                    = (uint64_t)&mock_cont;

	return 0;
}

static int
test_teardown(void **unused)
{
	int i;

	for (i = 0; i < DBD_BLOBS_CAP; i++)
		D_FREE(mock_dbds[i]);

	return 0;
}

/* tests */

static void
test_errors(void **unused)
{
	daos_handle_t hdl_null = {0};
	uint64_t      cmt_cnt;
	int           rc;

	/* Invalid arguments. */
	rc = vos_dtx_get_cmt_stat(hdl_null, &cmt_cnt, NULL);
	assert_rc_equal(rc, -DER_INVAL);
	rc = vos_dtx_get_cmt_stat(mock_coh, NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	/* Corrupted dbd */
	mock_dbds[1]->dbd_magic = 42;
	rc                      = vos_dtx_get_cmt_stat(mock_coh, &cmt_cnt, NULL);
	assert_rc_equal(rc, -DER_INVAL);
}

static void
test_cmt_cnt(void **unused)
{
	uint64_t cmt_cnt;
	int      rc;

	prep_dtx_entries();

	rc = vos_dtx_get_cmt_stat(mock_coh, &cmt_cnt, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(cmt_cnt, DBD_BLOB_DF_CAP * DBD_BLOBS_CAP);
}

static void
test_time_stat(void **unused)
{
	uint64_t             cmt_cnt;
	struct dtx_time_stat dts;
	int                  rc;

	prep_dtx_entries();

	rc = vos_dtx_get_cmt_stat(mock_coh, &cmt_cnt, &dts);
	assert_rc_equal(rc, 0);
	assert_int_equal(cmt_cnt, DBD_BLOB_DF_CAP * DBD_BLOBS_CAP);

	assert_int_equal(dts.dts_cmt_time[0], mock_dbds[0]->dbd_committed_data[0].dce_cmt_time);
	assert_int_equal(dts.dts_cmt_time[1], mock_dbds[2]->dbd_committed_data[7].dce_cmt_time);
	assert_int_equal(dts.dts_cmt_time[2], (dts.dts_cmt_time[0] + dts.dts_cmt_time[1]) / 2);

	assert_int_equal(dts.dts_epoch[0], mock_dbds[0]->dbd_committed_data[0].dce_epoch);
	assert_int_equal(dts.dts_epoch[1], mock_dbds[2]->dbd_committed_data[7].dce_epoch);
	assert_int_equal(dts.dts_epoch[2], (dts.dts_epoch[0] + dts.dts_epoch[1]) / 2);
}

/* clang-format off */
/* compilation unit's entry point */
#define TEST(name, func)                                                                           \
{                                                                                                  \
	name ": vos_dtx_get_cmt_stat - " #func, func, test_setup, test_teardown                    \
}
/* clang-format on */

static const struct CMUnitTest vos_dtx_get_cmt_stat_tests_all[] = {
    TEST("DTX600", test_errors),
    TEST("DTX601", test_cmt_cnt),
    TEST("DTX602", test_time_stat),
};

int
run_dtx_cmt_stat_tests(void)
{
	const char *test_name = "vos_dtx_get_cmt_stat";

	return cmocka_run_group_tests_name(test_name, vos_dtx_get_cmt_stat_tests_all, NULL, NULL);
}
