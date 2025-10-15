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
#define DBD_BLOBS_CAP      0x3
#define DBD_BLOB_DF_CAP    0x8
#define CMT_TIME_START     0x1000
#define CMT_TIME_END       0x10000
#define CMT_TIME_STEP      0x10
#define EPOCH_OFFSET       0x20000
#define MOCK_UMM_TX_MAX    0x100
#define CELL_SIZE                                                                                  \
	sizeof(struct vos_dtx_blob_df) + DBD_BLOB_DF_CAP * sizeof(struct vos_dtx_cmt_ent_df)

struct mocked_umm_tx {
	bool   in_tx;
	size_t count;
	struct {
		void  *target;
		void  *snapshot;
		size_t size;
	} tx[MOCK_UMM_TX_MAX];
};

static struct vos_pool         mock_pool;
static struct vos_container    mock_cont;
static struct vos_cont_df      mock_cont_df;
static struct vos_tls          mock_tls;
static struct vos_dtx_blob_df *mock_dbds[DBD_BLOBS_CAP];
static umem_off_t              mock_dbds_off[DBD_BLOBS_CAP];
static struct btr_instance     mock_btr;
static daos_handle_t           mock_coh;
static struct mocked_umm_tx    mock_umm_tx;

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
	assert_int_equal(toh.cookie, mock_cont.vc_dtx_committed_hdl.cookie);
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

	assert_ptr_equal(umm, &mock_pool.vp_umm);
	assert_null(txd);
	assert_int_equal(mock_umm_tx.count, 0);
	rc                = mock();
	mock_umm_tx.in_tx = (rc == 0);
	return rc;
}

static int
tx_commit(struct umem_instance *umm, void *data)
{
	int i;
	int rc;

	assert_ptr_equal(umm, &mock_pool.vp_umm);
	assert_null(data);
	assert_true(mock_umm_tx.in_tx);
	rc = mock();
	for (i = 0; i < mock_umm_tx.count; i++) {
		if (rc != 0)
			memcpy(mock_umm_tx.tx[i].target, mock_umm_tx.tx[i].snapshot,
			       mock_umm_tx.tx[i].size);
		D_FREE(mock_umm_tx.tx[i].snapshot);
	}
	mock_umm_tx.count = 0;
	mock_umm_tx.in_tx = false;
	return rc;
}

static int
tx_abort(struct umem_instance *umm, int error)
{
	int i;

	assert_ptr_equal(umm, &mock_pool.vp_umm);
	check_expected(error);
	assert_true(mock_umm_tx.in_tx);
	for (i = 0; i < mock_umm_tx.count; i++) {
		memcpy(mock_umm_tx.tx[i].target, mock_umm_tx.tx[i].snapshot,
		       mock_umm_tx.tx[i].size);
		D_FREE(mock_umm_tx.tx[i].snapshot);
	}
	mock_umm_tx.count = 0;
	mock_umm_tx.in_tx = false;
	if (error) {
		return error;
	}
	return mock();
}

static int
tx_add_ptr(struct umem_instance *umm, void *ptr, size_t ptr_size)
{
	assert_ptr_equal(umm, &mock_pool.vp_umm);
	check_expected_ptr(ptr);
	check_expected(ptr_size);
	D_ALLOC(mock_umm_tx.tx[mock_umm_tx.count].snapshot, ptr_size);
	assert_non_null(mock_umm_tx.tx[mock_umm_tx.count].snapshot);
	memcpy(mock_umm_tx.tx[mock_umm_tx.count].snapshot, ptr, ptr_size);
	mock_umm_tx.tx[mock_umm_tx.count].target = ptr;
	mock_umm_tx.tx[mock_umm_tx.count].size   = ptr_size;
	mock_umm_tx.count++;
	return mock();
}

static int
tx_free(struct umem_instance *umm, umem_off_t umoff)
{
	assert_ptr_equal(umm, &mock_pool.vp_umm);
	check_expected(umoff);
	assert_true(mock_umm_tx.in_tx);
	return mock();
}

/* Helpers */

static void
prep_dtx_entries(void)
{
	int          i;
	uint64_t     cmt_time;

	cmt_time = CMT_TIME_START;
	for (i = 0; i < DBD_BLOBS_CAP; i++) {
		int j;

		for (j = 0; j < DBD_BLOB_DF_CAP; j++) {
			struct vos_dtx_cmt_ent_df *dce_df;

			dce_df            = &mock_dbds[i]->dbd_committed_data[j];
			dce_df->dce_cmt_time = cmt_time;
			dce_df->dce_epoch    = cmt_time + EPOCH_OFFSET;
			cmt_time += CMT_TIME_STEP;
		}
		mock_dbds[i]->dbd_count = DBD_BLOB_DF_CAP;
	}
	mock_cont.vc_dtx_committed_count = DBD_BLOBS_CAP * DBD_BLOB_DF_CAP;
	mock_pool.vp_dtx_committed_count = DBD_BLOBS_CAP * DBD_BLOB_DF_CAP;
}

static void
check_rollback(void)
{
	int          i;
	uint64_t     cmt_time;

	cmt_time = CMT_TIME_START;
	for (i = 0; i < DBD_BLOBS_CAP; i++) {
		int j;

		for (j = 0; j < DBD_BLOB_DF_CAP; j++) {
			assert_int_equal(mock_dbds[i]->dbd_committed_data[j].dce_cmt_time,
					 cmt_time);
			assert_int_equal(mock_dbds[i]->dbd_committed_data[j].dce_epoch,
					 cmt_time + EPOCH_OFFSET);
			cmt_time += CMT_TIME_STEP;
		}
		assert_int_equal(mock_dbds[i]->dbd_count, DBD_BLOB_DF_CAP);
	}
	assert_int_equal(mock_cont_df.cd_newest_aggregated, 0);
	assert_int_equal(mock_cont_df.cd_dtx_committed_head,
			 umem_ptr2off(&mock_pool.vp_umm, mock_dbds[0]));
	assert_int_equal(mock_cont_df.cd_dtx_committed_tail,
			 umem_ptr2off(&mock_pool.vp_umm, mock_dbds[DBD_BLOBS_CAP - 1]));
	assert_int_equal(mock_cont.vc_dtx_committed_count, DBD_BLOBS_CAP * DBD_BLOB_DF_CAP);
	assert_int_equal(mock_pool.vp_dtx_committed_count, DBD_BLOBS_CAP * DBD_BLOB_DF_CAP);
	assert_int_equal(mock_cont.vc_cmt_dtx_reindex_pos,
			 umem_ptr2off(&mock_pool.vp_umm, mock_dbds[0]));
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
	int i;

	memset(&mock_pool, 0, sizeof(mock_pool));
	memset(&mock_cont, 0, sizeof(mock_cont));
	memset(&mock_cont_df, 0, sizeof(mock_cont_df));
	memset(&mock_tls, 0, sizeof(mock_tls));
	memset(&mock_umm_tx, 0, sizeof(mock_umm_tx));

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
		mock_dbds[i]->dbd_count = 0;
		assert_true(mock_dbds[i]->dbd_count <= DBD_BLOB_DF_CAP);
	}

	mock_pool.vp_umm.umm_ops              = &umm_ops;
	mock_cont.vc_pool                     = &mock_pool;
	mock_cont.vc_cont_df                  = &mock_cont_df;
	mock_cont.vc_cmt_dtx_reindex_pos      = mock_dbds_off[0];
	mock_cont.vc_dtx_committed_hdl.cookie = (uint64_t)&mock_btr;
	mock_cont_df.cd_dtx_committed_head    = mock_dbds_off[0];
	mock_cont_df.cd_dtx_committed_tail    = mock_dbds_off[DBD_BLOBS_CAP - 1];
	mock_coh.cookie                       = (uint64_t)&mock_cont;

	return 0;
}

static int
test_empty_setup(void **unused)
{
	int i;

	memset(&mock_pool, 0, sizeof(mock_pool));
	memset(&mock_cont, 0, sizeof(mock_cont));
	memset(&mock_cont_df, 0, sizeof(mock_cont_df));
	memset(&mock_tls, 0, sizeof(mock_tls));
	memset(&mock_umm_tx, 0, sizeof(mock_umm_tx));

	for (i = 0; i < DBD_BLOBS_CAP; i++) {
		mock_dbds[i]     = NULL;
		mock_dbds_off[i] = umem_ptr2off(&mock_pool.vp_umm, mock_dbds[i]);
	}
	mock_pool.vp_umm.umm_ops              = &umm_ops;
	mock_cont.vc_pool                     = &mock_pool;
	mock_cont.vc_cont_df                  = &mock_cont_df;
	mock_cont.vc_cmt_dtx_reindex_pos      = UMOFF_NULL;
	mock_cont.vc_dtx_committed_hdl.cookie = (uint64_t)&mock_btr;
	mock_cont_df.cd_dtx_committed_head    = UMOFF_NULL;
	mock_cont_df.cd_dtx_committed_tail    = UMOFF_NULL;
	mock_coh.cookie                       = (uint64_t)&mock_cont;

	return 0;
}

static int
test_teardown(void **unused)
{
	int i;

	assert_int_equal(mock_umm_tx.count, 0);
	for (i = 0; i < DBD_BLOBS_CAP; i++)
		D_FREE(mock_dbds[i]);

	return 0;
}

static int
test_empty_teardown(void **unused)
{
	assert_int_equal(mock_umm_tx.count, 0);

	return 0;
}

/* tests */

static void
test_asserts(void **unused)
{
	daos_handle_t hdl_null = {0};

	/* Invalid Telemetry global variable. */
	will_return(__wrap_vos_tls_get, NULL);
	expect_assert_failure(vos_dtx_aggregate(mock_coh, NULL));

	/* Missing argument. */
	will_return(__wrap_vos_tls_get, NULL);
	expect_assert_failure(vos_dtx_aggregate(hdl_null, NULL));

	/* Invalid pool type. */
	will_return(__wrap_vos_tls_get, &mock_tls);
	mock_pool.vp_sysdb = true;
	expect_assert_failure(vos_dtx_aggregate(mock_coh, NULL));
}

/* Can not start a PMEM transtation */
static void
test_tx_begin_error(void **unused)
{
	int rc;

	prep_dtx_entries();

	will_return(__wrap_vos_tls_get, &mock_tls);
	will_return(tx_begin, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(mock_coh, NULL);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback();
}

/* DAOS B-tree delete failure */
static void
test_dbtree_delete_error(void **unused)
{
	int i;
	int rc;

	prep_dtx_entries();

	will_return(__wrap_vos_tls_get, &mock_tls);
	will_return(tx_begin, 0);
	for (i = 0; i < 3; i++)
		will_return(__wrap_dbtree_delete, 0);
	will_return(__wrap_dbtree_delete, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(mock_coh, NULL);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback();
}

/* Update of newest aggregated epoch failure */
static void
test_newest_aggregated_error(void **unused)
{
	int i;
	int rc;

	prep_dtx_entries();

	will_return(__wrap_vos_tls_get, &mock_tls);
	will_return(tx_begin, 0);
	for (i = 0; i < DBD_BLOB_DF_CAP; i++)
		will_return(__wrap_dbtree_delete, 0);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(mock_cont_df.cd_newest_aggregated));
	will_return(tx_add_ptr, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(mock_coh, NULL);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback();
}

/* Update of DTX blob list failure */
static void
test_committed_head_error(void **unused)
{
	int i;
	int rc;

	prep_dtx_entries();

	will_return(__wrap_vos_tls_get, &mock_tls);
	will_return(tx_begin, 0);
	for (i = 0; i < DBD_BLOB_DF_CAP; i++)
		will_return(__wrap_dbtree_delete, 0);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(mock_cont_df.cd_newest_aggregated));
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_dtx_committed_head);
	expect_value(tx_add_ptr, ptr_size, sizeof(umem_off_t));
	will_return(tx_add_ptr, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(mock_coh, NULL);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback();
}

/* Update of DTX blob list failure */
static void
test_committed_prev_error(void **unused)
{
	int i;
	int rc;

	prep_dtx_entries();

	will_return(__wrap_vos_tls_get, &mock_tls);
	will_return(tx_begin, 0);
	for (i = 0; i < DBD_BLOB_DF_CAP; i++)
		will_return(__wrap_dbtree_delete, 0);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(mock_cont_df.cd_newest_aggregated));
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_dtx_committed_head);
	expect_value(tx_add_ptr, ptr_size, sizeof(umem_off_t));
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_dbds[1]->dbd_prev);
	expect_value(tx_add_ptr, ptr_size, sizeof(umem_off_t));
	will_return(tx_add_ptr, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(mock_coh, NULL);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback();
}

/* Pmem free failure */
static void
test_umm_free_error(void **unused)
{
	int i;
	int rc;

	will_return(__wrap_vos_tls_get, &mock_tls);
	will_return(tx_begin, 0);
	prep_dtx_entries();
	for (i = 0; i < DBD_BLOB_DF_CAP; i++)
		will_return(__wrap_dbtree_delete, 0);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(mock_cont_df.cd_newest_aggregated));
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_dtx_committed_head);
	expect_value(tx_add_ptr, ptr_size, sizeof(umem_off_t));
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_dbds[1]->dbd_prev);
	expect_value(tx_add_ptr, ptr_size, sizeof(umem_off_t));
	will_return(tx_add_ptr, 0);

	expect_value(tx_free, umoff, mock_dbds_off[0]);
	will_return(tx_free, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	rc = vos_dtx_aggregate(mock_coh, NULL);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback();
}

/* Update of committed DTX entries failure */
static void
test_committed_data_error(void **unused)
{
	uint64_t     cmt_time;
	int          i;
	int          rc;
	const int    dtx_count = 3;

	will_return(__wrap_vos_tls_get, &mock_tls);
	will_return(tx_begin, 0);
	prep_dtx_entries();
	for (i = 0; i < dtx_count; i++)
		will_return(__wrap_dbtree_delete, -DER_NONEXIST);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(mock_cont_df.cd_newest_aggregated));
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_dbds[0]->dbd_committed_data[0]);
	expect_value(tx_add_ptr, ptr_size,
		     sizeof(mock_dbds[0]->dbd_committed_data[0]) * mock_dbds[0]->dbd_count);
	will_return(tx_add_ptr, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	cmt_time = CMT_TIME_START + (dtx_count - 1) * CMT_TIME_STEP;
	rc       = vos_dtx_aggregate(mock_coh, &cmt_time);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback();
}

/* Update of committed DTX entries count failure */
static void
test_dbd_count_error(void **unused)
{
	uint64_t     cmt_time;
	int          i;
	int          rc;
	const int    dtx_count = 3;

	will_return(__wrap_vos_tls_get, &mock_tls);
	will_return(tx_begin, 0);
	prep_dtx_entries();
	for (i = 0; i < dtx_count; i++)
		will_return(__wrap_dbtree_delete, -DER_NONEXIST);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(mock_cont_df.cd_newest_aggregated));
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_dbds[0]->dbd_committed_data[0]);
	expect_value(tx_add_ptr, ptr_size,
		     sizeof(mock_dbds[0]->dbd_committed_data[0]) * mock_dbds[0]->dbd_count);
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_dbds[0]->dbd_count);
	expect_value(tx_add_ptr, ptr_size, sizeof(mock_dbds[0]->dbd_count));
	will_return(tx_add_ptr, -DER_UNKNOWN);
	expect_value(tx_abort, error, -DER_UNKNOWN);

	cmt_time = CMT_TIME_START + (dtx_count - 1) * CMT_TIME_STEP;
	rc       = vos_dtx_aggregate(mock_coh, &cmt_time);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback();
}

/* Pmem commit transaction failure */
static void
test_umm_commit_error(void **unused)
{
	uint64_t     cmt_time;
	int          i;
	int          rc;
	const int    dtx_count = 3;

	will_return(__wrap_vos_tls_get, &mock_tls);
	will_return(tx_begin, 0);
	prep_dtx_entries();
	for (i = 0; i < dtx_count; i++)
		will_return(__wrap_dbtree_delete, -DER_NONEXIST);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(mock_cont_df.cd_newest_aggregated));
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_dbds[0]->dbd_committed_data[0]);
	expect_value(tx_add_ptr, ptr_size,
		     sizeof(mock_dbds[0]->dbd_committed_data[0]) * mock_dbds[0]->dbd_count);
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_dbds[0]->dbd_count);
	expect_value(tx_add_ptr, ptr_size, sizeof(mock_dbds[0]->dbd_count));
	will_return(tx_add_ptr, 0);
	will_return(tx_commit, -DER_UNKNOWN);

	cmt_time = CMT_TIME_START + (dtx_count - 1) * CMT_TIME_STEP;
	rc       = vos_dtx_aggregate(mock_coh, &cmt_time);
	assert_rc_equal(rc, -DER_UNKNOWN);
	check_rollback();
}

/* Pool without DTX committed transaction */
static void
test_empty(void **unused)
{
	int rc;

	will_return(__wrap_vos_tls_get, &mock_tls);

	rc = vos_dtx_aggregate(mock_coh, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(mock_cont_df.cd_newest_aggregated, 0);
	assert_int_equal(mock_cont_df.cd_dtx_committed_head, UMOFF_NULL);
	assert_int_equal(mock_cont_df.cd_dtx_committed_tail, UMOFF_NULL);
	assert_int_equal(mock_cont.vc_dtx_committed_count, 0);
	assert_int_equal(mock_pool.vp_dtx_committed_count, 0);
	assert_int_equal(mock_cont.vc_cmt_dtx_reindex_pos, UMOFF_NULL);
}

/* Pool with empty DTX entries blob*/
static void
test_empty_blob(void **unused)
{
	int i;
	int rc;

	for (i = 0; i < DBD_BLOBS_CAP; i++) {
		will_return(__wrap_vos_tls_get, &mock_tls);
		will_return(tx_begin, 0);
		expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_dtx_committed_head);
		expect_value(tx_add_ptr, ptr_size, sizeof(umem_off_t));
		will_return(tx_add_ptr, 0);
		if (i == DBD_BLOBS_CAP - 1)
			expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_dtx_committed_tail);
		else
			expect_value(tx_add_ptr, ptr, &mock_dbds[i + 1]->dbd_prev);
		expect_value(tx_add_ptr, ptr_size, sizeof(umem_off_t));
		will_return(tx_add_ptr, 0);
		expect_value(tx_free, umoff, mock_dbds_off[i]);
		will_return(tx_free, 0);
		will_return(tx_commit, 0);
	}

	do {
		rc = vos_dtx_aggregate(mock_coh, NULL);
		assert_true(rc >= 0);
	} while (rc > 0);
	assert_int_equal(mock_cont_df.cd_newest_aggregated, 0);
	assert_int_equal(mock_cont_df.cd_dtx_committed_head, UMOFF_NULL);
	assert_int_equal(mock_cont_df.cd_dtx_committed_tail, UMOFF_NULL);
	assert_int_equal(mock_cont.vc_dtx_committed_count, 0);
	assert_int_equal(mock_pool.vp_dtx_committed_count, 0);
	assert_int_equal(mock_cont.vc_cmt_dtx_reindex_pos, UMOFF_NULL);
}

/* Aggregation of all the committed DTX transactions of one blob */
static void
test_one_blob(void **unused)
{
	int i;
	int rc;

	prep_dtx_entries();

	will_return(__wrap_vos_tls_get, &mock_tls);
	will_return(tx_begin, 0);
	for (i = 0; i < DBD_BLOB_DF_CAP; i++)
		will_return(__wrap_dbtree_delete, 0);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(mock_cont_df.cd_newest_aggregated));
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_dtx_committed_head);
	expect_value(tx_add_ptr, ptr_size, sizeof(umem_off_t));
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_dbds[1]->dbd_prev);
	expect_value(tx_add_ptr, ptr_size, sizeof(umem_off_t));
	will_return(tx_add_ptr, 0);
	expect_value(tx_free, umoff, mock_dbds_off[0]);
	will_return(tx_free, 0);
	will_return(tx_commit, 0);
	expect_value(__wrap_d_tm_dec_gauge, metric, mock_tls.vtl_committed);
	expect_value(__wrap_d_tm_dec_gauge, value, DBD_BLOB_DF_CAP);

	rc = vos_dtx_aggregate(mock_coh, NULL);
	assert_rc_equal(rc, 1);
	assert_int_equal(mock_cont_df.cd_newest_aggregated,
			 EPOCH_OFFSET + CMT_TIME_START + (DBD_BLOB_DF_CAP - 1) * CMT_TIME_STEP);
	assert_int_equal(mock_cont_df.cd_dtx_committed_head, mock_dbds_off[1]);
	assert_int_equal(mock_cont_df.cd_dtx_committed_tail, mock_dbds_off[DBD_BLOBS_CAP - 1]);
	assert_int_equal(mock_cont.vc_dtx_committed_count, DBD_BLOB_DF_CAP * (DBD_BLOBS_CAP - 1));
	assert_int_equal(mock_pool.vp_dtx_committed_count, DBD_BLOB_DF_CAP * (DBD_BLOBS_CAP - 1));
	assert_int_equal(mock_cont.vc_cmt_dtx_reindex_pos, mock_dbds_off[1]);
}

/* Aggregation of the first 10 committed DTX transactions */
static void
test_10_entries(void **unused)
{
	uint64_t     cmt_time;
	int          i;
	int          rc;

	prep_dtx_entries();

	/* First DTX entries blob */
	will_return(__wrap_vos_tls_get, &mock_tls);
	will_return(tx_begin, 0);
	for (i = 0; i < DBD_BLOB_DF_CAP; i++)
		will_return(__wrap_dbtree_delete, 0);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(mock_cont_df.cd_newest_aggregated));
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_dtx_committed_head);
	expect_value(tx_add_ptr, ptr_size, sizeof(umem_off_t));
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_dbds[1]->dbd_prev);
	expect_value(tx_add_ptr, ptr_size, sizeof(umem_off_t));
	will_return(tx_add_ptr, 0);
	expect_value(tx_free, umoff, mock_dbds_off[0]);
	will_return(tx_free, 0);
	will_return(tx_commit, 0);
	expect_value(__wrap_d_tm_dec_gauge, metric, mock_tls.vtl_committed);
	expect_value(__wrap_d_tm_dec_gauge, value, DBD_BLOB_DF_CAP);

	/* Second DTX entries blob */
	will_return(__wrap_vos_tls_get, &mock_tls);
	will_return(tx_begin, 0);
	for (i = 0; i < 3; i++)
		will_return(__wrap_dbtree_delete, 0);
	expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_newest_aggregated);
	expect_value(tx_add_ptr, ptr_size, sizeof(mock_cont_df.cd_newest_aggregated));
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_dbds[1]->dbd_committed_data[0]);
	expect_value(tx_add_ptr, ptr_size,
		     sizeof(mock_dbds[1]->dbd_committed_data[0]) * mock_dbds[1]->dbd_count);
	will_return(tx_add_ptr, 0);
	expect_value(tx_add_ptr, ptr, &mock_dbds[1]->dbd_count);
	expect_value(tx_add_ptr, ptr_size, sizeof(mock_dbds[1]->dbd_count));
	will_return(tx_add_ptr, 0);
	will_return(tx_commit, 0);
	expect_value(__wrap_d_tm_dec_gauge, metric, mock_tls.vtl_committed);
	expect_value(__wrap_d_tm_dec_gauge, value, 3);

	cmt_time = CMT_TIME_START + (DBD_BLOB_DF_CAP + 2) * CMT_TIME_STEP;
	do {
		rc = vos_dtx_aggregate(mock_coh, &cmt_time);
		assert_true(rc >= 0);
	} while (rc > 0);
	assert_int_equal(mock_cont_df.cd_newest_aggregated, EPOCH_OFFSET + cmt_time);
	assert_int_equal(mock_cont_df.cd_dtx_committed_head, mock_dbds_off[1]);
	assert_int_equal(mock_cont_df.cd_dtx_committed_tail, mock_dbds_off[DBD_BLOBS_CAP - 1]);
	assert_int_equal(mock_cont.vc_dtx_committed_count, DBD_BLOB_DF_CAP * 2 - 3);
	assert_int_equal(mock_pool.vp_dtx_committed_count, DBD_BLOB_DF_CAP * 2 - 3);
	assert_int_equal(mock_cont.vc_cmt_dtx_reindex_pos, mock_dbds_off[1]);
}

/* Aggregation of all the committed DTX transactions */
static void
test_all_entries(void **unused)
{
	int i;
	int rc;

	prep_dtx_entries();

	for (i = 0; i < DBD_BLOBS_CAP; i++) {
		int j;

		will_return(__wrap_vos_tls_get, &mock_tls);
		will_return(tx_begin, 0);
		for (j = 0; j < DBD_BLOB_DF_CAP; j++)
			will_return(__wrap_dbtree_delete, 0);
		expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_newest_aggregated);
		expect_value(tx_add_ptr, ptr_size, sizeof(mock_cont_df.cd_newest_aggregated));
		will_return(tx_add_ptr, 0);
		expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_dtx_committed_head);
		expect_value(tx_add_ptr, ptr_size, sizeof(umem_off_t));
		will_return(tx_add_ptr, 0);
		if (i == DBD_BLOBS_CAP - 1)
			expect_value(tx_add_ptr, ptr, &mock_cont_df.cd_dtx_committed_tail);
		else
			expect_value(tx_add_ptr, ptr, &mock_dbds[i + 1]->dbd_prev);
		expect_value(tx_add_ptr, ptr_size, sizeof(umem_off_t));
		will_return(tx_add_ptr, 0);
		expect_value(tx_free, umoff, mock_dbds_off[i]);
		will_return(tx_free, 0);
		will_return(tx_commit, 0);
		expect_value(__wrap_d_tm_dec_gauge, metric, mock_tls.vtl_committed);
		expect_value(__wrap_d_tm_dec_gauge, value, DBD_BLOB_DF_CAP);
	}

	do {
		rc = vos_dtx_aggregate(mock_coh, NULL);
		assert_true(rc >= 0);
	} while (rc > 0);
	assert_int_equal(mock_cont_df.cd_newest_aggregated,
			 EPOCH_OFFSET + CMT_TIME_START + (3 * DBD_BLOB_DF_CAP - 1) * CMT_TIME_STEP);
	assert_int_equal(mock_cont_df.cd_dtx_committed_head, UMOFF_NULL);
	assert_int_equal(mock_cont_df.cd_dtx_committed_tail, UMOFF_NULL);
	assert_int_equal(mock_cont.vc_dtx_committed_count, 0);
	assert_int_equal(mock_pool.vp_dtx_committed_count, 0);
	assert_int_equal(mock_cont.vc_cmt_dtx_reindex_pos, UMOFF_NULL);
}

/* clang-format off */
/* compilation unit's entry point */
#define TEST(name, func, setup_func, teardown_func)                                                \
{                                                                                                  \
	name ": vos_dtx_aggregate - " #func, func, setup_func, teardown_func                       \
}
/* clang-format on */

static const struct CMUnitTest vos_dtx_aggregate_tests_all[] = {
    TEST("DTX500", test_asserts, test_setup, test_teardown),
    TEST("DTX501", test_tx_begin_error, test_setup, test_teardown),
    TEST("DTX502", test_dbtree_delete_error, test_setup, test_teardown),
    TEST("DTX503", test_newest_aggregated_error, test_setup, test_teardown),
    TEST("DTX504", test_committed_head_error, test_setup, test_teardown),
    TEST("DTX505", test_committed_prev_error, test_setup, test_teardown),
    TEST("DTX506", test_umm_free_error, test_setup, test_teardown),
    TEST("DTX507", test_committed_data_error, test_setup, test_teardown),
    TEST("DTX508", test_dbd_count_error, test_setup, test_teardown),
    TEST("DTX509", test_umm_commit_error, test_setup, test_teardown),
    TEST("DTX550", test_empty, test_empty_setup, test_empty_teardown),
    TEST("DTX551", test_empty_blob, test_setup, test_teardown),
    TEST("DTX552", test_one_blob, test_setup, test_teardown),
    TEST("DTX553", test_10_entries, test_setup, test_teardown),
    TEST("DTX554", test_all_entries, test_setup, test_teardown),
};

int
run_dtx_aggregate_tests(void)
{
	const char *test_name = "vos_dtx_aggregate";

	return cmocka_run_group_tests_name(test_name, vos_dtx_aggregate_tests_all, NULL, NULL);
}
