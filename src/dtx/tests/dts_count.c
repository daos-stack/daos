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

#include "vos_internal.h"

/* mocks */

#define DTX_CMT_BLOB_MAGIC 0x2502191c
#define DBD_COUNT          0x3
#define DBD_BLOB_DF_CAP    0x8

static struct vos_pool         Pool;
static struct vos_container    Cont;
static struct vos_cont_df      ContDf;
static struct vos_dtx_blob_df *Dbds;
static umem_off_t              DbdsOff[DBD_COUNT];
static daos_handle_t           Coh;

/* setup & teardown */

static int
test_setup(void **unused)
{
	int i;

	memset(&Pool, 0, sizeof(Pool));
	memset(&Cont, 0, sizeof(Cont));
	memset(&ContDf, 0, sizeof(ContDf));

	D_ALLOC(Dbds, DBD_COUNT * (sizeof(struct vos_dtx_blob_df) +
				   DBD_BLOB_DF_CAP * sizeof(struct vos_dtx_cmt_ent_df)));
	for (i = 0; i < DBD_COUNT; i++) {
		Dbds[i].dbd_magic = DTX_CMT_BLOB_MAGIC;
		Dbds[i].dbd_cap   = DBD_BLOB_DF_CAP;
		DbdsOff[i]        = umem_ptr2off(&Pool.vp_umm, &Dbds[i]);
		Dbds[i].dbd_next =
		    (i == DBD_COUNT - 1) ? UMOFF_NULL : umem_ptr2off(&Pool.vp_umm, &Dbds[i + 1]);
		Dbds[i].dbd_prev = (i == 0) ? UMOFF_NULL : umem_ptr2off(&Pool.vp_umm, &Dbds[i - 1]);
		Dbds[i].dbd_count = (i + 1) * 2;
		assert_true(Dbds[i].dbd_count <= DBD_BLOB_DF_CAP);
	}

	Cont.vc_pool                 = &Pool;
	Cont.vc_cont_df              = &ContDf;
	ContDf.cd_dtx_committed_head = DbdsOff[0];
	ContDf.cd_dtx_committed_tail = DbdsOff[DBD_COUNT - 1];
	Coh.cookie                   = (uint64_t)&Cont;

	return 0;
}

static int
test_teardown(void **unused)
{
	D_FREE(Dbds);

	return 0;
}

/* tests */

static void
test_asserts(void **unused)
{
	daos_handle_t hdl_null = {0};

	/* Missing argument. */
	expect_assert_failure(vos_dtx_get_cmt_cnt(hdl_null));

	/* Corrupted dbd */
	Dbds[1].dbd_magic = 42;
	expect_assert_failure(vos_dtx_get_cmt_cnt(Coh));
}

static void
test_count(void **unused)
{
	uint32_t cnt;

	cnt = vos_dtx_get_cmt_cnt(Coh);
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
