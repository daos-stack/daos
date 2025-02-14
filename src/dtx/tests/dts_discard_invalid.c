/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos_srv/vos.h>

#include "ilog.h"
#include "vos_layout.h"
#include "vos_internal.h"

/* mocks */

static struct vos_pool           Pool;
static struct vos_container      Cont;
static daos_handle_t             Coh;
static bool                      In_tx = false;

static struct vos_dtx_act_ent    Dae;
static struct vos_dtx_act_ent_df Dae_df;
static struct vos_dtx_act_ent_df Dae_df_exp;

#define RECORDS_MAX 26
static umem_off_t Records[RECORDS_MAX];
static umem_off_t Records_df[RECORDS_MAX];
static umem_off_t Records_df_exp[RECORDS_MAX];

#define DTX_ID_PTR             ((struct dtx_id *)0x907)
#define VC_DTX_ACTIVE_HDL      0x456
#define DBTREE_LOOKUP_ERROR_RC (-DER_NONEXIST)

int
__wrap_dbtree_lookup(daos_handle_t coh, d_iov_t *key, d_iov_t *val_out)
{
	assert_int_equal(coh.cookie, VC_DTX_ACTIVE_HDL);
	assert_non_null(key);
	assert_int_equal(key->iov_len, key->iov_buf_len);
	assert_int_equal(key->iov_len, sizeof(struct dtx_id));
	assert_ptr_equal(key->iov_buf, DTX_ID_PTR);
	assert_non_null(val_out);
	assert_int_equal(val_out->iov_len, 0);
	assert_int_equal(val_out->iov_buf_len, 0);
	assert_null(val_out->iov_buf);
	val_out->iov_buf = (void *)mock();
	if (val_out->iov_buf != NULL) {
		val_out->iov_len = val_out->iov_buf_len = sizeof(struct vos_dtx_act_ent);
		return 0;
	}
	return DBTREE_LOOKUP_ERROR_RC;
}

#define REC_UMEM_OFFSET 0x1267
#define DTX_LID         0x356
#define EPOCH           0x557

bool
__wrap_ilog_is_valid(struct umem_instance *umm, umem_off_t rec, uint32_t dtx_lid,
		     daos_epoch_t epoch)
{
	assert_ptr_equal(umm, &Pool.vp_umm);
	check_expected(umem_off2offset(rec));
	assert_int_equal(dtx_lid, DTX_LID);
	assert_int_equal(epoch, EPOCH);
	return mock();
}

bool
__wrap_vos_irec_is_valid(const struct vos_irec_df *svt, uint32_t dtx_lid)
{
	check_expected(svt);
	assert_int_equal(dtx_lid, DTX_LID);
	return mock();
}

bool
__wrap_evt_desc_is_valid(const struct evt_desc *evt, uint32_t dtx_lid)
{
	check_expected(evt);
	assert_int_equal(dtx_lid, DTX_LID);
	return mock();
}

int
tx_begin(struct umem_instance *umm, struct umem_tx_stage_data *txd)
{
	assert_ptr_equal(umm, &Pool.vp_umm);
	assert_null(txd);
	int rc = mock();
	if (rc == 0) {
		In_tx = true;
	}
	return rc;
}

int
tx_commit(struct umem_instance *umm, void *data)
{
	assert_ptr_equal(umm, &Pool.vp_umm);
	assert_null(data);
	assert_true(In_tx);
	In_tx = false;
	return mock();
}

int
tx_abort(struct umem_instance *umm, int error)
{
	assert_ptr_equal(umm, &Pool.vp_umm);
	check_expected(error);
	assert_true(In_tx);
	In_tx = false;
	if (error) {
		return error;
	}
	return mock();
}

int
tx_add_ptr(struct umem_instance *umm, void *ptr, size_t size)
{
	assert_ptr_equal(umm, &Pool.vp_umm);
	check_expected(ptr);
	check_expected(size);
	return mock();
}

/* tests */

static void
test_missing_things(void **unused)
{
	daos_handle_t hdl_null  = {0};
	int           discarded = 0;
	int           rc;

	/* Missing arguments. */
	expect_assert_failure(vos_dtx_discard_invalid(hdl_null, NULL, NULL));
	expect_assert_failure(vos_dtx_discard_invalid(Coh, NULL, NULL));
	expect_assert_failure(vos_dtx_discard_invalid(Coh, DTX_ID_PTR, NULL));
	expect_assert_failure(vos_dtx_discard_invalid(Coh, NULL, &discarded));

	/* DAE not in the DTX active table. */
	will_return(__wrap_dbtree_lookup, NULL);
	rc = vos_dtx_discard_invalid(Coh, DTX_ID_PTR, &discarded);
	assert_int_equal(rc, DBTREE_LOOKUP_ERROR_RC);
}

struct rec_valid {
	enum vos_dtx_record_types type;
	bool                      valid;
};

static bool
prep_records_common(struct rec_valid tmpl[], int num, umem_off_t *rec, umem_off_t *rec_df,
		    umem_off_t *rec_df_exp)
{
	bool discarded = false;

	for (int i = 0; i < num; ++i) {
		umem_off_t off = REC_UMEM_OFFSET + i;
		rec[i]         = off;
		dtx_type2umoff_flag(&rec[i], tmpl[i].type);
		rec_df[i] = rec[i];

		switch (tmpl[i].type) {
		case DTX_RT_ILOG:
			expect_value(__wrap_ilog_is_valid, umem_off2offset(rec), off);
			will_return(__wrap_ilog_is_valid, tmpl[i].valid);
			break;
		case DTX_RT_SVT:
			expect_value(__wrap_vos_irec_is_valid, svt, off);
			will_return(__wrap_vos_irec_is_valid, tmpl[i].valid);
			break;
		case DTX_RT_EVT:
			expect_value(__wrap_evt_desc_is_valid, evt, off);
			will_return(__wrap_evt_desc_is_valid, tmpl[i].valid);
			break;
		default:
			fail_msg("Unknown record type: %d", tmpl[i].type);
		}

		if (tmpl[i].valid) {
			rec_df_exp[i] = rec[i];
		} else {
			rec_df_exp[i] = UMOFF_NULL;
			discarded     = true;
		}
	}

	return discarded;
}

static bool
prep_records_inline(struct rec_valid tmpl[], int num)
{
	Dae.dae_base.dae_rec_cnt = num;

	bool discarded = prep_records_common(tmpl, num, Dae.dae_base.dae_rec_inline,
					     Dae_df.dae_rec_inline, Dae_df_exp.dae_rec_inline);
	if (discarded) {
		expect_value(tx_add_ptr, ptr, &Dae_df.dae_rec_inline);
		expect_value(tx_add_ptr, size, sizeof(umem_off_t) * num);
	}

	return discarded;
}

static bool
prep_records_noninline(struct rec_valid tmpl[], int num)
{
	/* link both volatile and durable format noninline records */
	Dae.dae_records   = Records;
	DAE_REC_OFF(&Dae) = umem_ptr2off(&Pool.vp_umm, &Records_df);

	/* noninline records come always on top off the inline records */
	Dae.dae_base.dae_rec_cnt = DTX_INLINE_REC_CNT + num;

	bool discarded = prep_records_common(tmpl, num, Records, Records_df, Records_df_exp);
	if (discarded) {
		expect_value(tx_add_ptr, ptr, &Records_df);
		expect_value(tx_add_ptr, size, sizeof(umem_off_t) * num);
	}

	return discarded;
}

#define TX_ERROR_RC 0x156

static void
test_tx_begin_fail(void **unused)
{
	int discarded = 0;
	int rc;

	/* tx_begin() fails. */
	will_return(__wrap_dbtree_lookup, &Dae);
	will_return(tx_begin, TX_ERROR_RC);
	rc = vos_dtx_discard_invalid(Coh, DTX_ID_PTR, &discarded);
	assert_int_equal(rc, TX_ERROR_RC);
}

static void
test_tx_abort_fail(void **unused)
{
	int discarded = 0;
	int rc;

	/* tx_abort() (when nothing to commit) fails. */
	will_return(__wrap_dbtree_lookup, &Dae);
	will_return(tx_begin, 0);
	expect_value(tx_abort, error, 0);
	will_return(tx_abort, TX_ERROR_RC);
	rc = vos_dtx_discard_invalid(Coh, DTX_ID_PTR, &discarded);
	assert_int_equal(rc, TX_ERROR_RC);
}

struct rec_valid One_rec[] = {{.type = DTX_RT_ILOG, .valid = false}};

static void
test_tx_add_ptr_inline_fail(void **unused)
{
	int discarded = 0;
	int rc;

	/* tx_add_ptr() for inline records fails. */
	will_return(__wrap_dbtree_lookup, &Dae);
	will_return(tx_begin, 0);
	prep_records_inline(One_rec, ARRAY_SIZE(One_rec));
	will_return(tx_add_ptr, TX_ERROR_RC);
	expect_value(tx_abort, error, TX_ERROR_RC);
	rc = vos_dtx_discard_invalid(Coh, DTX_ID_PTR, &discarded);
	assert_int_equal(rc, TX_ERROR_RC);
}

static void
test_tx_add_ptr_noninline_fail(void **unused)
{
	int discarded = 0;
	int rc;

	/* tx_add_ptr() for non-inline records fails. */
	will_return(__wrap_dbtree_lookup, &Dae);
	will_return(tx_begin, 0);
	prep_records_noninline(One_rec, ARRAY_SIZE(One_rec));
	will_return(tx_add_ptr, TX_ERROR_RC);
	expect_value(tx_abort, error, TX_ERROR_RC);
	rc = vos_dtx_discard_invalid(Coh, DTX_ID_PTR, &discarded);
	assert_int_equal(rc, TX_ERROR_RC);
}

static void
test_tx_commit_fail(void **unused)
{
	int discarded = 0;
	int rc;

	/* tx_commit() fails. */
	will_return(__wrap_dbtree_lookup, &Dae);
	will_return(tx_begin, 0);
	prep_records_noninline(One_rec, ARRAY_SIZE(One_rec));
	will_return(tx_add_ptr, 0);
	will_return(tx_commit, TX_ERROR_RC);
	rc = vos_dtx_discard_invalid(Coh, DTX_ID_PTR, &discarded);
	assert_int_equal(rc, TX_ERROR_RC);
}

static void
reset_dfs();

#define DTX_RT_MIN DTX_RT_ILOG
#define DTX_RT_MAX DTX_RT_EVT
#define DTX_RT_NUM 3

static void
test_discard_inline_all(void **unused)
{
	struct rec_valid recs[] = {
	    {DTX_RT_ILOG, false},
	    {DTX_RT_SVT, false},
	    {DTX_RT_EVT, false},
	    {DTX_RT_ILOG, false},
	};

	int discarded = 0;
	int rc;

	/* discard all inline records at once */
	will_return(__wrap_dbtree_lookup, &Dae);
	will_return(tx_begin, 0);
	prep_records_inline(recs, ARRAY_SIZE(recs));
	will_return(tx_add_ptr, 0);
	will_return(tx_commit, 0);
	rc = vos_dtx_discard_invalid(Coh, DTX_ID_PTR, &discarded);
	assert_int_equal(rc, 0);
	assert_int_equal(discarded, ARRAY_SIZE(recs));
	assert_memory_equal(&Dae_df, &Dae_df_exp, sizeof(Dae_df));
	assert_memory_equal(&Records_df, &Records_df_exp, sizeof(Records_df));
}

typedef void (*execute_fn)(struct rec_valid *recs, int num);

static void
prep_discard_one_common(execute_fn execute)
{
	struct rec_valid recs[4];

	/* pick the type of the record to be discarded */
	for (enum vos_dtx_record_types type = DTX_RT_MIN; type <= DTX_RT_MAX; ++type) {
		enum vos_dtx_record_types other_type = (type + 1) % DTX_RT_NUM + DTX_RT_MIN;
		/* pick which entry will be discarded */
		for (int i = 0; i < ARRAY_SIZE(recs); ++i) {
			/* initialize the array describing the scenario */
			for (int j = 0; j < ARRAY_SIZE(recs); ++j) {
				if (j == i) {
					recs[j].type  = type;
					recs[j].valid = false;
				} else {
					recs[j].type  = other_type;
					recs[j].valid = true;
				}
			}
			/* reset durable format mocks */
			reset_dfs();
			execute(recs, ARRAY_SIZE(recs));
		}
	}
}

static void
discard_inline_one_execute(struct rec_valid *recs, int num)
{
	int discarded = 0;
	int rc;

	will_return(__wrap_dbtree_lookup, &Dae);
	will_return(tx_begin, 0);
	prep_records_inline(recs, num);
	will_return(tx_add_ptr, 0);
	will_return(tx_commit, 0);
	rc = vos_dtx_discard_invalid(Coh, DTX_ID_PTR, &discarded);
	assert_int_equal(rc, 0);
	assert_int_equal(discarded, 1);
	assert_memory_equal(&Dae_df, &Dae_df_exp, sizeof(Dae_df));
	assert_memory_equal(&Records_df, &Records_df_exp, sizeof(Records_df));
}

static void
test_discard_inline_one(void **unused)
{
	/* discard just one inline record */
	prep_discard_one_common(discard_inline_one_execute);
}

static void
test_discard_noninline_all(void **unused)
{
	struct rec_valid recs[] = {
	    {DTX_RT_ILOG, false},
	    {DTX_RT_SVT, false},
	    {DTX_RT_EVT, false},
	    {DTX_RT_ILOG, false},
	};

	int discarded = 0;
	int rc;

	/* discard all noninline records at once */
	will_return(__wrap_dbtree_lookup, &Dae);
	will_return(tx_begin, 0);
	prep_records_noninline(recs, ARRAY_SIZE(recs));
	will_return(tx_add_ptr, 0);
	will_return(tx_commit, 0);
	rc = vos_dtx_discard_invalid(Coh, DTX_ID_PTR, &discarded);
	assert_int_equal(rc, 0);
	assert_int_equal(discarded, ARRAY_SIZE(recs));
	assert_memory_equal(&Dae_df, &Dae_df, sizeof(Dae_df));
	assert_memory_equal(&Records_df, &Records_df_exp, sizeof(umem_off_t) * ARRAY_SIZE(recs));
}

static void
discard_noninline_one_execute(struct rec_valid *recs, int num)
{
	int discarded = 0;
	int rc;

	will_return(__wrap_dbtree_lookup, &Dae);
	will_return(tx_begin, 0);
	prep_records_noninline(recs, num);
	will_return(tx_add_ptr, 0);
	will_return(tx_commit, 0);
	rc = vos_dtx_discard_invalid(Coh, DTX_ID_PTR, &discarded);
	assert_int_equal(rc, 0);
	assert_int_equal(discarded, 1);
	assert_memory_equal(&Dae_df, &Dae_df_exp, sizeof(Dae_df));
	assert_memory_equal(&Records_df, &Records_df_exp, sizeof(umem_off_t) * num);
}

static void
test_discard_noninline_one(void **unused)
{
	/* discard just one noninline record */
	prep_discard_one_common(discard_noninline_one_execute);
}

#define RAND_SEED            2025
#define RAND_RECORDS_NUM_MAX (RECORDS_MAX + DTX_INLINE_REC_CNT)

static void
test_discard_rand(void **unused)
{
	int  discarded     = 0;
	int  discarded_exp = 0;
	/* tx_add_ptr() it is called on condition at least one record in a group is about to be
	 * discarded */
	bool call_tx_add_ptr;
	int  rc;

	srand(RAND_SEED);

	int               num  = rand() % RAND_RECORDS_NUM_MAX;
	struct rec_valid *recs = calloc(num, sizeof(struct rec_valid));
	for (int i = 0; i < num; ++i) {
		recs[i].type  = rand() % DTX_RT_MAX + DTX_RT_MIN;
		recs[i].valid = (rand() % 2 == 0 ? true : false);

		if (!recs[i].valid) {
			++discarded_exp;
		}
	}

	printf("srand(%d), num=%d, discarded=%d\n", RAND_SEED, num, discarded_exp);

	will_return(__wrap_dbtree_lookup, &Dae);
	will_return(tx_begin, 0);

	/* Note: The inline records are processed first hence they have to be initialized first as
	 * well. */
	call_tx_add_ptr = prep_records_inline(recs, min(num, DTX_INLINE_REC_CNT));
	if (call_tx_add_ptr) {
		will_return(tx_add_ptr, 0);
	}

	if (num > DTX_INLINE_REC_CNT) {
		call_tx_add_ptr =
		    prep_records_noninline(&recs[DTX_INLINE_REC_CNT], num - DTX_INLINE_REC_CNT);
		if (call_tx_add_ptr) {
			will_return(tx_add_ptr, 0);
		}
	}

	will_return(tx_commit, 0);
	rc = vos_dtx_discard_invalid(Coh, DTX_ID_PTR, &discarded);
	assert_int_equal(rc, 0);
	assert_int_equal(discarded, discarded_exp);
	assert_memory_equal(&Dae_df, &Dae_df_exp, sizeof(Dae_df));
	if (num > DTX_INLINE_REC_CNT) {
		assert_memory_equal(&Records_df, &Records_df_exp,
				    sizeof(umem_off_t) * (num - DTX_INLINE_REC_CNT));
	}

	free(recs);
}

/* setup & teardown */

static umem_ops_t umm_ops = {.mo_tx_begin   = tx_begin,
			     .mo_tx_commit  = tx_commit,
			     .mo_tx_abort   = tx_abort,
			     .mo_tx_add_ptr = tx_add_ptr};

static void
reset_dfs()
{
	/* durable format mocks primed with a pattern intentionally to detect UMOFF_NULL (discard)
	 * when set */
	memset(&Dae_df, 0xef, sizeof(Dae_df));
	memset(&Dae_df_exp, 0xef, sizeof(Dae_df));
	memset(&Records_df, 0xef, sizeof(Records_df));
	memset(&Records_df_exp, 0xef, sizeof(Records_df));
}

static int
setup_cont(void **unused)
{
	/* reset globals */
	memset(&Pool, 0, sizeof(Pool));
	memset(&Cont, 0, sizeof(Cont));
	memset(&Dae, 0, sizeof(Dae));
	memset(&Records, 0, sizeof(Records));
	In_tx = false;

	reset_dfs();

	Pool.vp_umm.umm_ops           = &umm_ops;
	Cont.vc_pool                  = &Pool;
	Cont.vc_dtx_active_hdl.cookie = VC_DTX_ACTIVE_HDL;
	Coh.cookie                    = (uint64_t)&Cont;
	Dae.dae_df_off                = umem_ptr2off(&Pool.vp_umm, &Dae_df);
	DAE_LID(&Dae)                 = DTX_LID;
	DAE_EPOCH(&Dae)               = EPOCH;

	return 0;
}

static int
teardown_cont(void **unused)
{
	/* nop */
	return 0;
}

/* compilation unit's entry point */
#define TEST(name, func)                                                                           \
	{                                                                                          \
		name ": vos_dtx_discard_invalid - " #func, test_##func, setup_cont, teardown_cont  \
	}

static const struct CMUnitTest discard_invalid_tests_all[] = {
    TEST("DTX400", missing_things),
    TEST("DTX401", tx_begin_fail),
    TEST("DTX402", tx_abort_fail),
    TEST("DTX403", tx_add_ptr_inline_fail),
    TEST("DTX404", tx_add_ptr_noninline_fail),
    TEST("DTX405", tx_commit_fail),
    TEST("DTX406", discard_inline_all),
    TEST("DTX407", discard_inline_one),
    TEST("DTX408", discard_noninline_all),
    TEST("DTX409", discard_noninline_one),
    TEST("DTX410", discard_rand),
};

int
run_discard_invalid_tests(void)
{
	const char *test_name = "vos_dtx_discard_invalid";

	return cmocka_run_group_tests_name(test_name, discard_invalid_tests_all, NULL, NULL);
}
