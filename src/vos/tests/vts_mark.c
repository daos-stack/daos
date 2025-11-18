/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of vos/tests/
 *
 * vos/tests/vts_mark.c
 */
#define D_LOGFAC DD_FAC(tests)

#include <daos/common.h>
#include <daos_srv/vos_types.h>
#include "vts_io.h"

static void
vts_mark_update(struct io_test_args *args, enum daos_otype_t otype, daos_epoch_t epoch,
		daos_unit_oid_t *oid, daos_key_t *dkey, char *dkey_buf, daos_key_t *akey,
		char *akey_buf, void *val_buf, daos_iod_t *iod, d_sg_list_t *sgl, daos_recx_t *rex,
		size_t dkey_size, size_t akey_size, size_t val_size, bool new_oid, bool new_dkey)
{
	d_iov_t iov;
	int     rc;

	memset(iod, 0, sizeof(*iod));
	memset(sgl, 0, sizeof(*sgl));
	memset(rex, 0, sizeof(*rex));

	if (new_oid)
		*oid = gen_oid(otype);

	if (new_dkey) {
		dts_key_gen(dkey_buf, dkey_size, "dkey");
		d_iov_set(dkey, dkey_buf, dkey_size);
	}

	dts_key_gen(akey_buf, akey_size, "akey");
	d_iov_set(akey, akey_buf, akey_size);

	dts_buf_render(val_buf, val_size);
	d_iov_set(&iov, val_buf, val_size);

	sgl->sg_iovs   = &iov;
	sgl->sg_nr     = 1;
	rex->rx_idx    = 0;
	rex->rx_nr     = 1;
	iod->iod_name  = *akey;
	iod->iod_type  = DAOS_IOD_SINGLE;
	iod->iod_size  = val_size;
	iod->iod_nr    = 1;
	iod->iod_recxs = rex;

	rc = vos_obj_update(args->ctx.tc_co_hdl, *oid, epoch, 1, 0, dkey, 1, iod, NULL, sgl);
	assert_rc_equal(rc, 0);
}

static inline void
vts_mark_prep_sgl(d_iov_t *iov, void *buf, size_t buf_size, d_sg_list_t *sgl, bool for_read)
{
	if (for_read)
		memset(buf, 0, buf_size);
	else
		dts_buf_render(buf, buf_size);

	d_iov_set(iov, buf, buf_size);
	sgl->sg_iovs   = iov;
	sgl->sg_nr_out = 1;
	sgl->sg_nr     = 1;
}

static void
vts_mark_1(void **state)
{
	struct io_test_args *args  = *state;
	daos_epoch_t         epoch = 1000;
	daos_unit_oid_t      oid;
	daos_key_t           dkey;
	daos_key_t           akey;
	d_iov_t              iov;
	daos_iod_t           iod;
	d_sg_list_t          sgl;
	daos_recx_t          rex;
	char                 dkey_buf[UPDATE_DKEY_SIZE];
	char                 akey_buf[UPDATE_AKEY_SIZE];
	char                 update_buf[UPDATE_BUF_SIZE];
	char                 fetch_buf[UPDATE_BUF_SIZE];
	int                  rc;

	vts_mark_update(args, DAOS_OT_DKEY_LEXICAL, ++epoch, &oid, &dkey, dkey_buf, &akey, akey_buf,
			update_buf, &iod, &sgl, &rex, UPDATE_DKEY_SIZE, UPDATE_AKEY_SIZE,
			UPDATE_BUF_SIZE, true, true);

	/* dkey is NULL, but akey_nr is not zero, then mark ops should fail. */
	rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++epoch, 1, oid, NULL, 1, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	/* akeys is empty, but akey_nr is not zero, then mark ops should fail. */
	rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++epoch, 1, oid, &dkey, 1, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++epoch, 1, oid, NULL, 0, NULL);
	assert_rc_equal(rc, 0);

	/* Read from corrupted object should fail. */
	vts_mark_prep_sgl(&iov, fetch_buf, UPDATE_BUF_SIZE, &sgl, true);
	rc = vos_obj_fetch(args->ctx.tc_co_hdl, oid, epoch, 0, &dkey, 1, &iod, &sgl);
	assert_rc_equal(rc, -DER_DATA_LOSS);

	/* It is OK to mark the object as corruption repeatedly. */
	rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++epoch, 1, oid, NULL, 0, NULL);
	assert_rc_equal(rc, 0);

	/* Update corrupted object should fail. */
	vts_mark_prep_sgl(&iov, update_buf, UPDATE_BUF_SIZE, &sgl, false);
	rc = vos_obj_update(args->ctx.tc_co_hdl, oid, ++epoch, 1, 0, &dkey, 1, &iod, NULL, &sgl);
	assert_rc_equal(rc, -DER_DATA_LOSS);

	/* Punch corrupted object should fail. */
	rc = vos_obj_punch(args->ctx.tc_co_hdl, oid, ++epoch, 1, 0, NULL, 0, NULL, NULL);
	assert_rc_equal(rc, -DER_DATA_LOSS);

	/* Mark corruption against non-exist object will create the object and succeed. */
	oid = gen_oid(DAOS_OT_MULTI_UINT64);
	rc  = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++epoch, 1, oid, NULL, 0, NULL);
	assert_rc_equal(rc, 0);

	/* Read from object[1] should fail since it is marked as corrupted. */
	vts_mark_prep_sgl(&iov, fetch_buf, UPDATE_BUF_SIZE, &sgl, true);
	rc = vos_obj_fetch(args->ctx.tc_co_hdl, oid, epoch, 0, &dkey, 1, &iod, &sgl);
	assert_rc_equal(rc, -DER_DATA_LOSS);
}

static void
vts_mark_2(void **state)
{
	struct io_test_args *args  = *state;
	daos_epoch_t         epoch = 2000;
	daos_unit_oid_t      oid;
	daos_key_t           dkey;
	daos_key_t           akey;
	d_iov_t              iov;
	daos_iod_t           iod;
	d_sg_list_t          sgl;
	daos_recx_t          rex;
	char                 dkey_buf[UPDATE_DKEY_SIZE];
	char                 akey_buf[UPDATE_AKEY_SIZE];
	char                 update_buf[UPDATE_BUF_SIZE];
	char                 fetch_buf[UPDATE_BUF_SIZE];
	int                  rc;

	vts_mark_update(args, DAOS_OT_DKEY_LEXICAL, ++epoch, &oid, &dkey, dkey_buf, &akey, akey_buf,
			update_buf, &iod, &sgl, &rex, UPDATE_DKEY_SIZE, UPDATE_AKEY_SIZE,
			UPDATE_BUF_SIZE, true, true);

	/* Mark corruption against invalid dkey should fail. */
	d_iov_set(&dkey, dkey_buf, 0);
	rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++epoch, 1, oid, &dkey, 0, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	/* Mark corruption against invalid dkey should fail. */
	d_iov_set(&dkey, NULL, UPDATE_DKEY_SIZE);
	rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++epoch, 1, oid, &dkey, 0, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	d_iov_set(&dkey, dkey_buf, UPDATE_DKEY_SIZE);
	rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++epoch, 1, oid, &dkey, 0, NULL);
	assert_rc_equal(rc, 0);

	/* Read from corrupted dkey should fail. */
	vts_mark_prep_sgl(&iov, fetch_buf, UPDATE_BUF_SIZE, &sgl, true);
	rc = vos_obj_fetch(args->ctx.tc_co_hdl, oid, epoch, 0, &dkey, 1, &iod, &sgl);
	assert_rc_equal(rc, -DER_DATA_LOSS);

	/* Update corrupted dkey should fail. */
	vts_mark_prep_sgl(&iov, update_buf, UPDATE_BUF_SIZE, &sgl, false);
	rc = vos_obj_update(args->ctx.tc_co_hdl, oid, ++epoch, 1, 0, &dkey, 1, &iod, NULL, &sgl);
	assert_rc_equal(rc, -DER_DATA_LOSS);

	/* Punch corrupted dkey should fail. */
	rc = vos_obj_punch(args->ctx.tc_co_hdl, oid, ++epoch, 1, 0, &dkey, 0, NULL, NULL);
	assert_rc_equal(rc, -DER_DATA_LOSS);

	/* Update different dkey under the same object should succeed. */
	vts_mark_update(args, DAOS_OT_DKEY_LEXICAL, ++epoch, &oid, &dkey, dkey_buf, &akey, akey_buf,
			update_buf, &iod, &sgl, &rex, UPDATE_DKEY_SIZE, UPDATE_AKEY_SIZE,
			UPDATE_BUF_SIZE, false, true);

	/* Read from non-corrupted dkey should succeed.  */
	vts_mark_prep_sgl(&iov, fetch_buf, UPDATE_BUF_SIZE, &sgl, true);
	rc = vos_obj_fetch(args->ctx.tc_co_hdl, oid, epoch, 0, &dkey, 1, &iod, &sgl);
	assert_rc_equal(rc, 0);
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Mark corruption against non-exist dkey will create the dkey and succeed. */
	dts_key_gen(dkey_buf, UPDATE_DKEY_SIZE, "dkey_new");
	d_iov_set(&dkey, dkey_buf, UPDATE_DKEY_SIZE);
	rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++epoch, 1, oid, &dkey, 0, NULL);
	assert_rc_equal(rc, 0);

	/* Read from new dkey should fail since it is marked as corrupted. */
	vts_mark_prep_sgl(&iov, fetch_buf, UPDATE_BUF_SIZE, &sgl, true);
	rc = vos_obj_fetch(args->ctx.tc_co_hdl, oid, epoch, 0, &dkey, 1, &iod, &sgl);
	assert_rc_equal(rc, -DER_DATA_LOSS);
}

static void
vts_mark_3(void **state)
{
	struct io_test_args *args  = *state;
	daos_epoch_t         epoch = 3000;
	daos_unit_oid_t      oid;
	daos_key_t           dkey;
	daos_key_t           akeys[3];
	d_iov_t              iov;
	daos_iod_t           iod;
	d_sg_list_t          sgl;
	daos_recx_t          rex;
	char                 dkey_buf[UPDATE_DKEY_SIZE];
	char                 akey_bufs[3][UPDATE_AKEY_SIZE];
	char                 update_buf[UPDATE_BUF_SIZE];
	char                 fetch_buf[UPDATE_BUF_SIZE];
	int                  rc;

	vts_mark_update(args, DAOS_OT_DKEY_LEXICAL, ++epoch, &oid, &dkey, dkey_buf, &akeys[0],
			akey_bufs[0], update_buf, &iod, &sgl, &rex, UPDATE_DKEY_SIZE,
			UPDATE_AKEY_SIZE, UPDATE_BUF_SIZE, true, true);

	dts_key_gen(akey_bufs[1], UPDATE_AKEY_SIZE, "akey");

	/* Mark corruption against invalid akey should fail. */
	d_iov_set(&akeys[1], akey_bufs[1], 0);
	rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++epoch, 1, oid, &dkey, 3, akeys);
	assert_rc_equal(rc, -DER_INVAL);

	/* Mark corruption against invalid akey should fail. */
	d_iov_set(&akeys[1], NULL, UPDATE_DKEY_SIZE);
	rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++epoch, 1, oid, &dkey, 3, akeys);
	assert_rc_equal(rc, -DER_INVAL);

	/* Mark corruption against non-exist akeys[1] will create the akeys[1] and succeed. */
	d_iov_set(&akeys[1], akey_bufs[1], UPDATE_DKEY_SIZE);
	rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++epoch, 1, oid, &dkey, 2, akeys);
	assert_rc_equal(rc, 0);

	/* Read from corrupted akeys[0] should fail. */
	vts_mark_prep_sgl(&iov, fetch_buf, UPDATE_BUF_SIZE, &sgl, true);
	rc = vos_obj_fetch(args->ctx.tc_co_hdl, oid, epoch, 0, &dkey, 1, &iod, &sgl);
	assert_rc_equal(rc, -DER_DATA_LOSS);

	/* Update corrupted akeys[0] should fail. */
	vts_mark_prep_sgl(&iov, update_buf, UPDATE_BUF_SIZE, &sgl, false);
	rc = vos_obj_update(args->ctx.tc_co_hdl, oid, ++epoch, 1, 0, &dkey, 1, &iod, NULL, &sgl);
	assert_rc_equal(rc, -DER_DATA_LOSS);

	/* Punch corrupted akeys[1] should fail. */
	rc = vos_obj_punch(args->ctx.tc_co_hdl, oid, ++epoch, 1, 0, &dkey, 1, &akeys[1], NULL);
	assert_rc_equal(rc, -DER_DATA_LOSS);

	/* Update different akeys[2] under the same dkey should succeed. */
	vts_mark_update(args, DAOS_OT_DKEY_LEXICAL, ++epoch, &oid, &dkey, dkey_buf, &akeys[2],
			akey_bufs[2], update_buf, &iod, &sgl, &rex, UPDATE_DKEY_SIZE,
			UPDATE_AKEY_SIZE, UPDATE_BUF_SIZE, false, false);

	/* Read from non-corrupted akeys[2] should succeed.  */
	vts_mark_prep_sgl(&iov, fetch_buf, UPDATE_BUF_SIZE, &sgl, true);
	rc = vos_obj_fetch(args->ctx.tc_co_hdl, oid, epoch, 0, &dkey, 1, &iod, &sgl);
	assert_rc_equal(rc, 0);
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Read from akeys[1] should fail since it is marked as corrupted. */
	vts_mark_prep_sgl(&iov, fetch_buf, UPDATE_BUF_SIZE, &sgl, true);
	iod.iod_name = akeys[1];
	rc           = vos_obj_fetch(args->ctx.tc_co_hdl, oid, epoch, 0, &dkey, 1, &iod, &sgl);
	assert_rc_equal(rc, -DER_DATA_LOSS);
}

static void
vts_mark_discard(struct io_test_args *args, daos_epoch_t *epoch, enum daos_otype_t otype,
		 uint32_t key_size, bool bad_obj, bool bad_dkey, bool bad_akey, bool flat_kv)
{
	daos_epoch_range_t range;
	daos_unit_oid_t    oid;
	daos_key_t         dkey;
	daos_key_t         akey;
	d_iov_t            iov;
	daos_iod_t         iod;
	d_sg_list_t        sgl;
	daos_recx_t        rex;
	char               dkey_buf[UPDATE_DKEY_SIZE];
	char               akey_buf[UPDATE_AKEY_SIZE];
	char               update_buf[UPDATE_BUF_SIZE];
	char               fetch_buf[UPDATE_BUF_SIZE];
	int                rc;

	vts_mark_update(args, otype, ++(*epoch), &oid, &dkey, dkey_buf, &akey, akey_buf, update_buf,
			&iod, &sgl, &rex, key_size != 0 ? key_size : UPDATE_DKEY_SIZE,
			key_size != 0 ? key_size : UPDATE_AKEY_SIZE, UPDATE_BUF_SIZE, true, true);

	if (bad_obj) {
		rc =
		    vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++(*epoch), 1, oid, NULL, 0, NULL);
		assert_rc_equal(rc, 0);
	}

	if (bad_dkey) {
		rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++(*epoch), 1, oid, &dkey, 0,
					     NULL);
		assert_rc_equal(rc, 0);
	}

	if (bad_akey) {
		rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++(*epoch), 1, oid, &dkey, 1,
					     &akey);
		if (flat_kv) {
			assert_rc_equal(rc, -DER_NO_PERM);
			rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++(*epoch), 1, oid, &dkey,
						     0, NULL);
			assert_rc_equal(rc, 0);
		} else {
			assert_rc_equal(rc, 0);
		}
	}

	range.epr_lo = 0;
	range.epr_hi = ++(*epoch);

	/* Try to aggregate, that should fail since some object/{d,a}key is corrupted. */
	rc = vos_aggregate(args->ctx.tc_co_hdl, &range, NULL, NULL,
			   VOS_AGG_FL_FORCE_SCAN | VOS_AGG_FL_FORCE_MERGE);
	assert_rc_equal(rc, -DER_DATA_LOSS);

	sleep(3);

	/* Read (with checking existence) from corrupted target should get DER_DATA_LOSS. */
	vts_mark_prep_sgl(&iov, fetch_buf, UPDATE_BUF_SIZE, &sgl, true);
	rc = vos_obj_fetch(args->ctx.tc_co_hdl, oid, *epoch, VOS_OF_FETCH_CHECK_EXISTENCE, &dkey, 1,
			   &iod, &sgl);
	assert_rc_equal(rc, -DER_DATA_LOSS);

	if (bad_obj)
		/* Discard object, that should succeed even if some object is corrupted. */
		rc = vos_discard(args->ctx.tc_co_hdl, NULL, &range, NULL, NULL);
	else
		/* Discard the dkeys, that should succeed even if some {d,a}key is corrupted. */
		rc = vos_discard(args->ctx.tc_co_hdl, &oid, &range, NULL, NULL);
	assert_rc_equal(rc, 0);

	sleep(3);

	/* Read (with checking existence) from discarded target should get DER_NONEXIST. */
	vts_mark_prep_sgl(&iov, fetch_buf, UPDATE_BUF_SIZE, &sgl, true);
	rc = vos_obj_fetch(args->ctx.tc_co_hdl, oid, *epoch, VOS_OF_FETCH_CHECK_EXISTENCE, &dkey, 1,
			   &iod, &sgl);
	assert_rc_equal(rc, -DER_NONEXIST);
}

static void
vts_mark_4(void **state)
{
	struct io_test_args *args  = *state;
	daos_epoch_t         epoch = 4000;

	/* Mark object with multi-level KV as corrupted. */
	vts_mark_discard(args, &epoch, DAOS_OT_MULTI_LEXICAL, 0, true, false, false, false);

	/* Mark {d,a}key with flat KV as corrupted. */
	vts_mark_discard(args, &epoch, DAOS_OT_KV_HASHED, 0, false, false, true, true);

	/* Mark integer akey as corrupted. */
	vts_mark_discard(args, &epoch, DAOS_OT_MULTI_UINT64, 8, false, false, true, false);
}

static void
vts_mark_delete(struct io_test_args *args, daos_epoch_t *epoch, bool bad_obj, bool bad_dkey,
		bool bad_akey, bool del_obj, bool del_dkey, bool del_akey)
{
	daos_unit_oid_t oid;
	daos_key_t      dkey;
	daos_key_t      akey;
	d_iov_t         iov;
	daos_iod_t      iod;
	d_sg_list_t     sgl;
	daos_recx_t     rex;
	char            dkey_buf[UPDATE_DKEY_SIZE];
	char            akey_buf[UPDATE_AKEY_SIZE];
	char            update_buf[UPDATE_BUF_SIZE];
	char            fetch_buf[UPDATE_BUF_SIZE];
	int             rc;

	vts_mark_update(args, DAOS_OT_DKEY_LEXICAL, ++(*epoch), &oid, &dkey, dkey_buf, &akey,
			akey_buf, update_buf, &iod, &sgl, &rex, UPDATE_DKEY_SIZE, UPDATE_AKEY_SIZE,
			UPDATE_BUF_SIZE, true, true);

	if (bad_obj) {
		rc =
		    vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++(*epoch), 1, oid, NULL, 0, NULL);
		assert_rc_equal(rc, 0);
	}

	if (bad_dkey) {
		rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++(*epoch), 1, oid, &dkey, 0,
					     NULL);
		assert_rc_equal(rc, 0);
	}

	if (bad_akey) {
		rc = vos_obj_mark_corruption(args->ctx.tc_co_hdl, ++(*epoch), 1, oid, &dkey, 1,
					     &akey);
		assert_rc_equal(rc, 0);
	}

	if (del_akey) {
		rc = vos_obj_del_key(args->ctx.tc_co_hdl, oid, &dkey, NULL);
		assert_rc_equal(rc, 0);
	}

	if (del_dkey) {
		rc = vos_obj_del_key(args->ctx.tc_co_hdl, oid, &dkey, NULL);
		assert_rc_equal(rc, 0);
	}

	if (del_obj) {
		rc = vos_obj_delete(args->ctx.tc_co_hdl, oid);
		assert_rc_equal(rc, 0);
	}

	/* Read (with checking existence) from deleted target should get DER_NONEXIST. */
	vts_mark_prep_sgl(&iov, fetch_buf, UPDATE_BUF_SIZE, &sgl, true);
	rc = vos_obj_fetch(args->ctx.tc_co_hdl, oid, *epoch, VOS_OF_FETCH_CHECK_EXISTENCE, &dkey, 1,
			   &iod, &sgl);
	assert_rc_equal(rc, -DER_NONEXIST);
}

static void
vts_mark_5(void **state)
{
	struct io_test_args *args  = *state;
	daos_epoch_t         epoch = 5000;

	/* Mark the dkey as corrupted under corrupted object, that should be OK. */
	vts_mark_delete(args, &epoch, true, true, false, true, false, false);

	/* Mark akey as corrupted under corrupted dkey, that should be OK. */
	vts_mark_delete(args, &epoch, false, true, true, false, true, false);

	/* Only mark akey as corrupted. */
	vts_mark_delete(args, &epoch, false, false, true, false, false, true);
}

static int
mark_test_teardown(void **state)
{
	test_args_reset((struct io_test_args *)*state, VPOOL_SIZE, 0, VPOOL_SIZE, 0);
	return 0;
}

static const struct CMUnitTest mark_tests[] = {
    {"VOS701: MARK corruption against object", vts_mark_1, NULL, mark_test_teardown},
    {"VOS702: MARK corruption against dkey", vts_mark_2, NULL, mark_test_teardown},
    {"VOS703: MARK corruption against akey", vts_mark_3, NULL, mark_test_teardown},
    {"VOS704: discard corrupted object", vts_mark_4, NULL, mark_test_teardown},
    {"VOS705: delete corrupted target (for ddb)", vts_mark_5, NULL, mark_test_teardown},
};

int
run_mark_tests(const char *cfg)
{
	char test_name[DTS_CFG_MAX];

	dts_create_config(test_name, "MARK Test %s", cfg);
	return cmocka_run_group_tests_name(test_name, mark_tests, setup_io, teardown_io);
}
