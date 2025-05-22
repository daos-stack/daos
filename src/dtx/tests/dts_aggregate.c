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
#include <daos/tests_lib.h>
#include <daos_errno.h>
#include <daos_srv/vos.h>
#include <daos/btree.h>

#include "ilog.h"
#include "vos_layout.h"
#include "vos_internal.h"

/* mocks */

#define DTX_CMT_BLOB_MAGIC 0x2502191c
#define DBD_BLOB_DF_CAP    0x8
#define EPOCH_START        0x1000
#define EPOCH_STEP         0x10
#define MOCK_UMM_TX_MAX    0x100

struct mock_umm_tx {
	bool   in_tx;
	size_t count;
	struct {
		void  *target;
		void  *snapshot;
		size_t size;
	} tx[MOCK_UMM_TX_MAX];
};

static struct vos_pool         Pool;
static struct vos_container    Cont;
static struct vos_cont_df      ContDf;
static struct vos_tls          Tls;
static struct vos_dtx_blob_df *Dbd = NULL;
static umem_off_t              DbdOff;
static struct btr_instance     Btr;
static daos_handle_t           Coh;
static struct mock_umm_tx      MockUmmTx;

struct vos_tls *
__wrap_vos_tls_get(bool standalone)
{
	return mock_ptr_type(struct vos_tls *);
}

void
__wrap_lrua_array_aggregate(struct lru_array *array)
{
	/* nop */
	return;
}

int
__wrap_dbtree_delete(daos_handle_t toh, dbtree_probe_opc_t opc, d_iov_t *key, void *args)
{
	assert_int_equal(toh.cookie, Cont.vc_dtx_committed_hdl.cookie);
	assert_int_equal(opc, BTR_PROBE_EQ);
	assert_non_null(key);
	assert_null(args);
	return mock();
}
void
__wrap_d_tm_dec_gauge(struct d_tm_node_t *metric, uint64_t value)
{
	check_expected_ptr(metric);
	check_expected(value);
}

static int
tx_begin(struct umem_instance *umm, struct umem_tx_stage_data *txd)
{
	int rc;

	assert_ptr_equal(umm, &Pool.vp_umm);
	assert_null(txd);
	assert_int_equal(MockUmmTx.count, 0);
	rc = mock();
	if (rc == 0) {
		MockUmmTx.in_tx = true;
	}
	return rc;
}

static int
tx_commit(struct umem_instance *umm, void *data)
{
	int i;
	int rc;

	assert_ptr_equal(umm, &Pool.vp_umm);
	assert_null(data);
	assert_true(MockUmmTx.in_tx);
	rc = mock();
	for (i = 0; i < MockUmmTx.count; i++) {
		if (rc != -DER_SUCCESS)
			memcpy(MockUmmTx.tx[i].target, MockUmmTx.tx[i].snapshot,
			       MockUmmTx.tx[i].size);
		D_FREE(MockUmmTx.tx[i].snapshot);
	}
	MockUmmTx.count = 0;
	MockUmmTx.in_tx = false;
	return rc;
}

static int
tx_abort(struct umem_instance *umm, int error)
{
	int i;

	assert_ptr_equal(umm, &Pool.vp_umm);
	check_expected(error);
	assert_true(MockUmmTx.in_tx);
	for (i = 0; i < MockUmmTx.count; i++) {
		memcpy(MockUmmTx.tx[i].target, MockUmmTx.tx[i].snapshot, MockUmmTx.tx[i].size);
		D_FREE(MockUmmTx.tx[i].snapshot);
	}
	MockUmmTx.count = 0;
	MockUmmTx.in_tx = false;
	if (error) {
		return error;
	}
	return mock();
}

static int
tx_add_ptr(struct umem_instance *umm, void *ptr, size_t ptr_size)
{
	assert_ptr_equal(umm, &Pool.vp_umm);
	check_expected_ptr(ptr);
	check_expected(ptr_size);
	D_ALLOC(MockUmmTx.tx[MockUmmTx.count].snapshot, ptr_size);
	assert_non_null(MockUmmTx.tx[MockUmmTx.count].snapshot);
	memcpy(MockUmmTx.tx[MockUmmTx.count].snapshot, ptr, ptr_size);
	MockUmmTx.tx[MockUmmTx.count].target = ptr;
	MockUmmTx.tx[MockUmmTx.count].size   = ptr_size;
	MockUmmTx.count++;
	return mock();
}

static int
tx_free(struct umem_instance *umm, umem_off_t umoff)
{
	assert_ptr_equal(umm, &Pool.vp_umm);
	check_expected(umoff);
	assert_true(MockUmmTx.in_tx);
	return mock();
}

/* Helpers */

static void
prep_dtx_entries(size_t count)
{
	int          i;
	daos_epoch_t epoch = EPOCH_START;

	for (i = 0; i < count; i++) {
		struct vos_dtx_cmt_ent_df *dce_df;

		dce_df            = &Dbd->dbd_committed_data[i];
		dce_df->dce_epoch = epoch;

		epoch += EPOCH_STEP;
	}
	Dbd->dbd_count              = count;
	Cont.vc_dtx_committed_count = count;
	Pool.vp_dtx_committed_count = count;
}

static void
check_rollback(size_t count)
{
	int i;

	assert_int_equal(Dbd->dbd_count, count);
	for (i = 0; i < count; i++)
		assert_int_equal(Dbd->dbd_committed_data[i].dce_epoch,
				 EPOCH_START + i * EPOCH_STEP);
	assert_int_equal(ContDf.cd_newest_aggregated, 0);
	assert_int_equal(ContDf.cd_dtx_committed_head, umem_ptr2off(&Pool.vp_umm, Dbd));
	assert_int_equal(ContDf.cd_dtx_committed_tail, umem_ptr2off(&Pool.vp_umm, Dbd));
	assert_int_equal(Cont.vc_dtx_committed_count, count);
	assert_int_equal(Pool.vp_dtx_committed_count, count);
	assert_int_equal(Cont.vc_cmt_dtx_reindex_pos, umem_ptr2off(&Pool.vp_umm, Dbd));
}

/* setup & teardown */

static umem_ops_t umm_ops = {.mo_tx_begin   = tx_begin,
			     .mo_tx_commit  = tx_commit,
			     .mo_tx_abort   = tx_abort,
			     .mo_tx_add_ptr = tx_add_ptr,
			     .mo_tx_free    = tx_free};

static int
test_setup(void **unused)
{
	memset(&Pool, 0, sizeof(Pool));
	memset(&Cont, 0, sizeof(Cont));
	memset(&ContDf, 0, sizeof(ContDf));
	memset(&Tls, 0, sizeof(Tls));
	memset(&MockUmmTx, 0, sizeof(MockUmmTx));

	D_ALLOC(Dbd, sizeof(struct vos_dtx_blob_df) +
			 DBD_BLOB_DF_CAP * sizeof(struct vos_dtx_cmt_ent_df));
	assert_non_null(Dbd);
	Dbd->dbd_magic = DTX_CMT_BLOB_MAGIC;
	Dbd->dbd_cap   = DBD_BLOB_DF_CAP;
	Dbd->dbd_next  = UMOFF_NULL;
	DbdOff         = umem_ptr2off(&Pool.vp_umm, Dbd);

	Pool.vp_umm.umm_ops              = &umm_ops;
	Cont.vc_pool                     = &Pool;
	Cont.vc_cont_df                  = &ContDf;
	Cont.vc_cmt_dtx_reindex_pos      = DbdOff;
	Cont.vc_dtx_committed_hdl.cookie = (uint64_t)&Btr;
	ContDf.cd_dtx_committed_head     = DbdOff;
	ContDf.cd_dtx_committed_tail     = DbdOff;
	Coh.cookie                       = (uint64_t)&Cont;

	return 0;
}

static int
test_teardown(void **unused)
{
	assert_int_equal(MockUmmTx.count, 0);
	D_FREE(Dbd);

	return 0;
}

/* tests */

static void
test_asserts(void **unused)
{
	daos_handle_t hdl_null = {0};

	/* Missing argument. */
	expect_assert_failure(vos_dtx_aggregate(hdl_null, NULL));

	/* Invalid Telemetry global variable. */
	will_return(__wrap_vos_tls_get, NULL);
	expect_assert_failure(vos_dtx_aggregate(Coh, NULL));

	/* Invalid pool type. */
	Pool.vp_sysdb  = true;
	Dbd->dbd_count = 1;
	will_return(__wrap_vos_tls_get, &Tls);
	expect_assert_failure(vos_dtx_aggregate(Coh, NULL));
}

/* Can not start a PMEM transtation */
static void
test_tx_begin_error(void **unused)
{
	const size_t dtx_count = 5;
	int          rc;

	will_return(__wrap_vos_tls_get, &Tls);
	will_return(tx_begin, -DER_UNKNOWN);
	prep_dtx_entries(dtx_count);

	rc = vos_dtx_aggregate(Coh, NULL);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback(dtx_count);
}

/* DAOS B-tree delete failure */
static void
test_dbtree_delete_error(void **unused)
{
	const size_t dtx_count = 5;
	int          i;
	int          rc;

	will_return(__wrap_vos_tls_get, &Tls);
	will_return(tx_begin, -DER_SUCCESS);
	prep_dtx_entries(dtx_count);
	for (i = 0; i < 3; i++) {
		will_return(__wrap_dbtree_delete, -DER_SUCCESS);
	}
	will_return(__wrap_dbtree_delete, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(Coh, NULL);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback(dtx_count);
}

/* Update of newest aggregated epoch failure */
static void
test_newest_aggregated_error(void **unused)
{
	const size_t dtx_count = 5;
	int          i;
	int          rc;

	will_return(__wrap_vos_tls_get, &Tls);
	will_return(tx_begin, -DER_SUCCESS);
	prep_dtx_entries(dtx_count);
	for (i = 0; i < dtx_count; i++) {
		will_return(__wrap_dbtree_delete, -DER_SUCCESS);
	}
	expect_value(tx_add_ptr, ptr, &ContDf.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_newest_aggregated));
	will_return(tx_add_ptr, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(Coh, NULL);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback(dtx_count);
}

/* Update of DTX blob list failure */
static void
test_committed_tail_error(void **unused)
{
	const size_t dtx_count = 5;
	int          i;
	int          rc;

	will_return(__wrap_vos_tls_get, &Tls);
	will_return(tx_begin, -DER_SUCCESS);
	prep_dtx_entries(dtx_count);
	for (i = 0; i < dtx_count; i++) {
		will_return(__wrap_dbtree_delete, -DER_SUCCESS);
	}
	expect_value(tx_add_ptr, ptr, &ContDf.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_newest_aggregated));
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &ContDf.cd_dtx_committed_tail);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_dtx_committed_tail));
	will_return(tx_add_ptr, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(Coh, NULL);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback(dtx_count);
}

/* Update of DTX blob list failure */
static void
test_committed_head_error(void **unused)
{
	const size_t dtx_count = 5;
	int          i;
	int          rc;

	will_return(__wrap_vos_tls_get, &Tls);
	will_return(tx_begin, -DER_SUCCESS);
	prep_dtx_entries(dtx_count);
	for (i = 0; i < dtx_count; i++) {
		will_return(__wrap_dbtree_delete, -DER_SUCCESS);
	}
	expect_value(tx_add_ptr, ptr, &ContDf.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_newest_aggregated));
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &ContDf.cd_dtx_committed_tail);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_dtx_committed_tail));
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &ContDf.cd_dtx_committed_head);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_dtx_committed_head));
	will_return(tx_add_ptr, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(Coh, NULL);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback(dtx_count);
}

/* Pmem free failure */
static void
test_umm_free_error(void **unused)
{
	const size_t dtx_count = 5;
	int          i;
	int          rc;

	will_return(__wrap_vos_tls_get, &Tls);
	will_return(tx_begin, -DER_SUCCESS);
	prep_dtx_entries(dtx_count);
	for (i = 0; i < dtx_count; i++) {
		will_return(__wrap_dbtree_delete, -DER_SUCCESS);
	}
	expect_value(tx_add_ptr, ptr, &ContDf.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_newest_aggregated));
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &ContDf.cd_dtx_committed_tail);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_dtx_committed_tail));
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &ContDf.cd_dtx_committed_head);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_dtx_committed_head));
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_free, umoff, ContDf.cd_dtx_committed_head);
	will_return(tx_free, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(Coh, NULL);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback(dtx_count);
}

/* Update of committed DTX entries failure */
static void
test_committed_data_error(void **unused)
{
	const daos_epoch_t ep_max    = EPOCH_START + 2 * EPOCH_STEP;
	const size_t       dtx_count = 5;
	int                i;
	int                rc;

	will_return(__wrap_vos_tls_get, &Tls);
	will_return(tx_begin, -DER_SUCCESS);
	prep_dtx_entries(dtx_count);
	for (i = 0; i < 3; i++) {
		will_return(__wrap_dbtree_delete, -DER_NONEXIST);
	}
	expect_value(tx_add_ptr, ptr, &ContDf.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_newest_aggregated));
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &Dbd->dbd_committed_data[0]);
	expect_value(tx_add_ptr, ptr_size, sizeof(Dbd->dbd_committed_data[0]) * dtx_count);
	will_return(tx_add_ptr, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(Coh, &ep_max);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback(dtx_count);
}

/* Update of committed DTX entries count failure */
static void
test_dbd_count_error(void **unused)
{
	const daos_epoch_t ep_max    = EPOCH_START + 2 * EPOCH_STEP;
	const size_t       dtx_count = 5;
	int                i;
	int                rc;

	will_return(__wrap_vos_tls_get, &Tls);
	will_return(tx_begin, -DER_SUCCESS);
	prep_dtx_entries(dtx_count);
	for (i = 0; i < 3; i++) {
		will_return(__wrap_dbtree_delete, -DER_NONEXIST);
	}
	expect_value(tx_add_ptr, ptr, &ContDf.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_newest_aggregated));
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &Dbd->dbd_committed_data[0]);
	expect_value(tx_add_ptr, ptr_size, sizeof(Dbd->dbd_committed_data[0]) * dtx_count);
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &Dbd->dbd_count);
	expect_value(tx_add_ptr, ptr_size, sizeof(Dbd->dbd_count));
	will_return(tx_add_ptr, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(Coh, &ep_max);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback(dtx_count);
}

/* Pmem commit transaction failure */
static void
test_umm_commit_error(void **unused)
{
	const daos_epoch_t ep_max    = EPOCH_START + 2 * EPOCH_STEP;
	const size_t       dtx_count = 5;
	int                i;
	int                rc;

	will_return(__wrap_vos_tls_get, &Tls);
	will_return(tx_begin, -DER_SUCCESS);
	prep_dtx_entries(dtx_count);
	for (i = 0; i < 3; i++) {
		will_return(__wrap_dbtree_delete, -DER_NONEXIST);
	}
	expect_value(tx_add_ptr, ptr, &ContDf.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_newest_aggregated));
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &Dbd->dbd_committed_data[0]);
	expect_value(tx_add_ptr, ptr_size, sizeof(Dbd->dbd_committed_data[0]) * dtx_count);
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &Dbd->dbd_count);
	expect_value(tx_add_ptr, ptr_size, sizeof(Dbd->dbd_count));
	will_return(tx_add_ptr, -DER_SUCCESS);
	will_return(tx_commit, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(Coh, &ep_max);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback(dtx_count);
}

/* Pool without DTX committed transaction */
static void
test_empty(void **unused)
{
	int rc;

	will_return(__wrap_vos_tls_get, &Tls);

	/* No committed DTX entries to aggregate. */
	rc = vos_dtx_aggregate(Coh, NULL);
	assert_rc_equal(rc, -DER_SUCCESS);
}

/* Aggregation of all the 5 committed DTX transactions */
static void
test_5_entries(void **unused)
{
	const size_t dtx_count = 5;
	int          i;
	int          rc;

	will_return(__wrap_vos_tls_get, &Tls);
	will_return(tx_begin, -DER_SUCCESS);
	prep_dtx_entries(dtx_count);
	for (i = 0; i < dtx_count; i++) {
		will_return(__wrap_dbtree_delete, -DER_SUCCESS);
	}
	expect_value(tx_add_ptr, ptr, &ContDf.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_newest_aggregated));
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &ContDf.cd_dtx_committed_tail);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_dtx_committed_tail));
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &ContDf.cd_dtx_committed_head);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_dtx_committed_head));
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_free, umoff, ContDf.cd_dtx_committed_head);
	will_return(tx_free, -DER_SUCCESS);
	will_return(tx_commit, -DER_SUCCESS);
	expect_value(__wrap_d_tm_dec_gauge, metric, Tls.vtl_committed);
	expect_value(__wrap_d_tm_dec_gauge, value, dtx_count);

	rc = vos_dtx_aggregate(Coh, NULL);
	assert_rc_equal(rc, -DER_SUCCESS);
	assert_int_equal(ContDf.cd_newest_aggregated, EPOCH_START + (dtx_count - 1) * EPOCH_STEP);
	assert_int_equal(ContDf.cd_dtx_committed_head, UMOFF_NULL);
	assert_int_equal(ContDf.cd_dtx_committed_tail, UMOFF_NULL);
	assert_int_equal(Cont.vc_dtx_committed_count, 0);
	assert_int_equal(Pool.vp_dtx_committed_count, 0);
	assert_int_equal(Cont.vc_cmt_dtx_reindex_pos, UMOFF_NULL);
}

/* Aggregation of 3 committed DTX transactions */
static void
test_3_entries(void **unused)
{
	const daos_epoch_t ep_max    = EPOCH_START + 2 * EPOCH_STEP;
	const size_t       dtx_count = 5;
	int                i;
	int                rc;

	will_return(__wrap_vos_tls_get, &Tls);
	will_return(tx_begin, -DER_SUCCESS);
	prep_dtx_entries(dtx_count);
	for (i = 0; i < 3; i++) {
		will_return(__wrap_dbtree_delete, -DER_NONEXIST);
	}
	expect_value(tx_add_ptr, ptr, &ContDf.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(ContDf.cd_newest_aggregated));
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &Dbd->dbd_committed_data[0]);
	expect_value(tx_add_ptr, ptr_size, sizeof(Dbd->dbd_committed_data[0]) * dtx_count);
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &Dbd->dbd_count);
	expect_value(tx_add_ptr, ptr_size, sizeof(Dbd->dbd_count));
	will_return(tx_add_ptr, -DER_SUCCESS);
	will_return(tx_commit, -DER_SUCCESS);
	expect_value(__wrap_d_tm_dec_gauge, metric, Tls.vtl_committed);
	expect_value(__wrap_d_tm_dec_gauge, value, 3);

	rc = vos_dtx_aggregate(Coh, &ep_max);
	assert_rc_equal(rc, -DER_SUCCESS);
	assert_int_equal(Dbd->dbd_count, 2);
	assert_int_equal(Dbd->dbd_committed_data[0].dce_epoch, EPOCH_START + 3 * EPOCH_STEP);
	assert_int_equal(Dbd->dbd_committed_data[1].dce_epoch, EPOCH_START + 4 * EPOCH_STEP);
	assert_int_equal(ContDf.cd_newest_aggregated, EPOCH_START + 2 * EPOCH_STEP);
	assert_int_equal(ContDf.cd_dtx_committed_head, DbdOff);
	assert_int_equal(ContDf.cd_dtx_committed_tail, DbdOff);
	assert_int_equal(Cont.vc_dtx_committed_count, 2);
	assert_int_equal(Pool.vp_dtx_committed_count, 2);
	assert_int_equal(Cont.vc_cmt_dtx_reindex_pos, DbdOff);
}

/* Aggregation of 0 committed DTX transactions */
static void
test_0_entry(void **unused)
{
	const daos_epoch_t ep_max    = EPOCH_START - 1;
	const size_t       dtx_count = 5;
	int                rc;

	will_return(__wrap_vos_tls_get, &Tls);
	will_return(tx_begin, -DER_SUCCESS);
	prep_dtx_entries(dtx_count);
	expect_value(tx_add_ptr, ptr, &Dbd->dbd_committed_data[0]);
	expect_value(tx_add_ptr, ptr_size, sizeof(Dbd->dbd_committed_data[0]) * dtx_count);
	will_return(tx_add_ptr, -DER_SUCCESS);
	expect_value(tx_add_ptr, ptr, &Dbd->dbd_count);
	expect_value(tx_add_ptr, ptr_size, sizeof(Dbd->dbd_count));
	will_return(tx_add_ptr, -DER_SUCCESS);
	will_return(tx_commit, -DER_SUCCESS);
	expect_value(__wrap_d_tm_dec_gauge, metric, Tls.vtl_committed);
	expect_value(__wrap_d_tm_dec_gauge, value, 0);

	rc = vos_dtx_aggregate(Coh, &ep_max);
	assert_rc_equal(rc, -DER_SUCCESS);
	check_rollback(dtx_count);
}

/* clang-format off */
/* compilation unit's entry point */
#define TEST(name, func) {name ": vos_dtx_aggregate - " #func, func, test_setup, test_teardown}
/* clang-format on */

static const struct CMUnitTest discard_invalid_tests_all[] = {
    TEST("DTX500", test_asserts),
    TEST("DTX501", test_tx_begin_error),
    TEST("DTX502", test_dbtree_delete_error),
    TEST("DTX503", test_newest_aggregated_error),
    TEST("DTX504", test_committed_tail_error),
    TEST("DTX505", test_committed_head_error),
    TEST("DTX506", test_umm_free_error),
    TEST("DTX507", test_committed_data_error),
    TEST("DTX508", test_dbd_count_error),
    TEST("DTX509", test_umm_commit_error),
    TEST("DTX550", test_empty),
    TEST("DTX551", test_5_entries),
    TEST("DTX552", test_3_entries),
    TEST("DTX554", test_0_entry),
};

int
run_dtx_aggregate_tests(void)
{
	const char *test_name = "vos_dtx_discard_invalid";

	return cmocka_run_group_tests_name(test_name, discard_invalid_tests_all, NULL, NULL);
}
