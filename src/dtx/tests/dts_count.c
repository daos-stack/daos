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
#define DBD_COUNT          0x3
#define DBD_BLOB_DF_CAP    0x8
#define CELL_SIZE                                                                                  \
	sizeof(struct vos_dtx_blob_df) + DBD_BLOB_DF_CAP * sizeof(struct vos_dtx_cmt_ent_df)

static struct vos_pool         mock_pool;
static struct vos_container    mock_cont;
static struct vos_cont_df      mock_cont_df;
static struct vos_dtx_blob_df *mock_dbds[DBD_COUNT];
static umem_off_t              mock_dbds_off[DBD_COUNT];
static daos_handle_t           mock_coh;

/* setup & teardown */

static int
test_setup(void **unused)
{
	int i;

	memset(&mock_pool, 0, sizeof(mock_pool));
	memset(&mock_cont, 0, sizeof(mock_cont));
	memset(&mock_cont_df, 0, sizeof(mock_cont_df));

	for (i = 0; i < DBD_COUNT; i++) {
		D_ALLOC(mock_dbds[i], CELL_SIZE);
		assert_non_null(mock_dbds[i]);
	}

	for (i = 0; i < DBD_COUNT; i++) {
		mock_dbds[i]->dbd_magic = DTX_CMT_BLOB_MAGIC;
		mock_dbds[i]->dbd_cap   = DBD_BLOB_DF_CAP;
		if (i == DBD_COUNT - 1)
			mock_dbds[i]->dbd_next = UMOFF_NULL;
		else
			mock_dbds[i]->dbd_next = umem_ptr2off(&mock_pool.vp_umm, mock_dbds[i + 1]);
		if (i == 0)
			mock_dbds[i]->dbd_prev = UMOFF_NULL;
		else
			mock_dbds[i]->dbd_prev = umem_ptr2off(&mock_pool.vp_umm, mock_dbds[i - 1]);
		mock_dbds[i]->dbd_count = (i + 1) * 2;
		assert_true(mock_dbds[i]->dbd_count <= DBD_BLOB_DF_CAP);

		mock_dbds_off[i] = umem_ptr2off(&mock_pool.vp_umm, mock_dbds[i]);
	}

	mock_cont.vc_pool                  = &mock_pool;
	mock_cont.vc_cont_df               = &mock_cont_df;
	mock_cont_df.cd_dtx_committed_head = mock_dbds_off[0];
	mock_cont_df.cd_dtx_committed_tail = mock_dbds_off[DBD_COUNT - 1];
	mock_coh.cookie                    = (uint64_t)&mock_cont;

	return 0;
}

static int
test_teardown(void **unused)
{
	int i;

	for (i = 0; i < DBD_COUNT; i++)
		D_FREE(mock_dbds[i]);

	return 0;
}

/* tests */

static void
test_asserts(void **unused)
{
	daos_handle_t           hdl_null = {0};
	uint32_t                cnt;
	int                     rc;

	/* Invalid arguments. */
	rc = vos_dtx_get_cmt_cnt(hdl_null, &cnt);
	assert_rc_equal(rc, -DER_INVAL);
	rc = vos_dtx_get_cmt_cnt(mock_coh, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	/* Corrupted dbd */
	mock_dbds[1]->dbd_magic = 42;
	rc                      = vos_dtx_get_cmt_cnt(mock_coh, &cnt);
	assert_rc_equal(rc, -DER_INVAL);
}

static void
test_count(void **unused)
{
	uint32_t cnt;
	int      rc;

	/* Container with several DTX entries table */
	rc = vos_dtx_get_cmt_cnt(mock_coh, &cnt);
	assert_rc_equal(rc, 0);
	assert_int_equal(cnt, DBD_COUNT * (DBD_COUNT + 1));
}

/* compilation unit's entry point */
#define TEST(name, func)                                                                           \
	{                                                                                          \
		name ": vos_dtx_get_cmt_cnt - " #func, func, test_setup, test_teardown             \
	}

static const struct CMUnitTest vos_dtx_count_tests_all[] = {
    TEST("DTX600", test_asserts),
    TEST("DTX601", test_count),
};

int
run_dtx_count_tests(void)
{
	const char *test_name = "vos_dtx_get_cmt_cnt";

	return cmocka_run_group_tests_name(test_name, vos_dtx_count_tests_all, NULL, NULL);
}
