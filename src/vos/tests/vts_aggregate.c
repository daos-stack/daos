/**
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of vos/tests/
 *
 * vos/tests/vts_aggregate.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include "vts_io.h"
#include <vos_internal.h>
#include <daos_srv/container.h>

#define VERBOSE_MSG(...)			\
{						\
	if (false)				\
		print_message(__VA_ARGS__);	\
}

static bool slow_test;

static void
cleanup(void)
{
	daos_fail_loc_set(DAOS_VOS_GC_CONT_NULL | DAOS_FAIL_ALWAYS);
	gc_wait();
}

void
update_value(struct io_test_args *arg, daos_unit_oid_t oid, daos_epoch_t epoch,
	     uint64_t flags, char *dkey, char *akey, daos_iod_type_t type,
	     daos_size_t iod_size, daos_recx_t *recx, char *buf)
{
	daos_iod_t	iod = { 0 };
	d_sg_list_t	sgl = { 0 };
	daos_key_t	dkey_iov, akey_iov;
	daos_size_t	buf_len;
	int		rc;

	assert_true(dkey != NULL && akey != NULL);
	assert_true(strlen(dkey) && strlen(akey));
	assert_true(!(arg->ta_flags & TF_ZERO_COPY));

	arg->oid = oid;
	d_iov_set(&dkey_iov, dkey, strlen(dkey));
	d_iov_set(&akey_iov, akey, strlen(akey));

	rc = d_sgl_init(&sgl, 1);
	assert_rc_equal(rc, 0);

	if (type == DAOS_IOD_SINGLE)
		buf_len = iod_size;
	else
		buf_len = recx->rx_nr * iod_size;
	assert_true(buf_len > 0);

	sgl.sg_iovs[0].iov_buf = buf;
	sgl.sg_iovs[0].iov_buf_len = buf_len;
	sgl.sg_iovs[0].iov_len = buf_len;

	iod.iod_name = akey_iov;
	iod.iod_nr = 1;
	iod.iod_type = type;
	iod.iod_size = iod_size;
	iod.iod_recxs = (type == DAOS_IOD_SINGLE) ? NULL : recx;

	if (arg->ta_flags & TF_PUNCH) {
		memset(buf, 0, buf_len);
		iod.iod_size = 0;
	} else if ((arg->ta_flags & TF_USE_VAL) == 0) {
		dts_buf_render(buf, buf_len);
		if (rand() % 2 == 0)
			arg->ta_flags |= TF_ZERO_COPY;
	}

	rc = io_test_obj_update(arg, epoch, flags, &dkey_iov, &iod, &sgl, NULL, true);

	assert_rc_equal(rc, 0);

	d_sgl_fini(&sgl, false);
	arg->ta_flags &= ~TF_ZERO_COPY;
}

void
fetch_value(struct io_test_args *arg, daos_unit_oid_t oid, daos_epoch_t epoch,
	    uint64_t flags, char *dkey, char *akey, daos_iod_type_t type,
	    daos_size_t iod_size, daos_recx_t *recx, char *buf)
{
	daos_iod_t	iod = { 0 };
	d_sg_list_t	sgl = { 0 };
	daos_key_t	dkey_iov, akey_iov;
	daos_size_t	buf_len;
	int		rc;

	assert_true(dkey != NULL && akey != NULL);
	assert_true(strlen(dkey) && strlen(akey));
	assert_true(!(arg->ta_flags & TF_ZERO_COPY));

	arg->oid = oid;
	d_iov_set(&dkey_iov, dkey, strlen(dkey));
	d_iov_set(&akey_iov, akey, strlen(akey));

	rc = d_sgl_init(&sgl, 1);
	assert_rc_equal(rc, 0);

	if (type == DAOS_IOD_SINGLE)
		buf_len = iod_size;
	else
		buf_len = recx->rx_nr * iod_size;
	assert_true(buf_len > 0);

	sgl.sg_iovs[0].iov_buf = buf;
	sgl.sg_iovs[0].iov_buf_len = buf_len;
	sgl.sg_iovs[0].iov_len = buf_len;

	iod.iod_name = akey_iov;
	iod.iod_nr = 1;
	iod.iod_type = type;
	iod.iod_size = DAOS_REC_ANY;
	iod.iod_recxs = recx;

	memset(buf, 0, buf_len);
	if (rand() % 2 == 0)
		arg->ta_flags |= TF_ZERO_COPY;

	rc = io_test_obj_fetch(arg, epoch, flags, &dkey_iov, &iod, &sgl, true);
	assert_rc_equal(rc, 0);
	assert_true(iod.iod_size == 0 || iod.iod_size == iod_size);

	d_sgl_fini(&sgl, false);
	arg->ta_flags &= ~TF_ZERO_COPY;
}

static int
counting_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	    vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	int	*nr = cb_arg;

	switch (type) {
	case VOS_ITER_SINGLE:
	case VOS_ITER_RECX:
		(*nr)++;
		break;
	default:
		assert_true(false); /* TODO */
		break;
	}

	return 0;
}

static int
phy_recs_nr(struct io_test_args *arg, daos_unit_oid_t oid,
	    daos_epoch_range_t *epr, char *dkey, char *akey,
	    daos_iod_type_t type)
{
	struct vos_iter_anchors	anchors = { 0 };
	daos_key_t		dkey_iov, akey_iov;
	vos_iter_param_t	iter_param = { 0 };
	vos_iter_type_t		iter_type;
	int			rc, nr = 0;

	assert_true(dkey != NULL && akey != NULL);
	assert_true(strlen(dkey) && strlen(akey));
	d_iov_set(&dkey_iov, dkey, strlen(dkey));
	d_iov_set(&akey_iov, akey, strlen(akey));

	iter_param.ip_hdl = arg->ctx.tc_co_hdl;
	iter_param.ip_oid = oid;
	iter_param.ip_dkey = dkey_iov;
	iter_param.ip_akey = akey_iov;
	iter_param.ip_epr = *epr;
	if (epr->epr_lo == epr->epr_hi)
		iter_param.ip_epc_expr = VOS_IT_EPC_EQ;
	else if (epr->epr_hi != DAOS_EPOCH_MAX)
		iter_param.ip_epc_expr = VOS_IT_EPC_RR;
	else
		iter_param.ip_epc_expr = VOS_IT_EPC_GE;
	iter_param.ip_flags = VOS_IT_RECX_ALL;

	iter_type = (type == DAOS_IOD_SINGLE) ?
		VOS_ITER_SINGLE : VOS_ITER_RECX;

	rc = vos_iterate(&iter_param, iter_type, false, &anchors,
			 counting_cb, NULL, &nr, NULL);
	assert_rc_equal(rc, 0);

	return nr;
}
static int
lookup_object(struct io_test_args *arg, daos_unit_oid_t oid)
{
	struct vos_object	*obj = NULL;
	int			 rc;
	daos_epoch_range_t	 epr = {0, DAOS_EPOCH_MAX};

	/** Do a hold because we may have only deleted one incarnation of the
	 *  tree.   If this returns 0, we need to release the object though
	 *  this is only presently used to check existence
	 */
	rc = vos_obj_hold(vos_obj_cache_current(true),
			  vos_hdl2cont(arg->ctx.tc_co_hdl), oid, &epr, 0,
			  VOS_OBJ_VISIBLE, DAOS_INTENT_DEFAULT, &obj, 0);
	if (rc == 0)
		vos_obj_release(vos_obj_cache_current(true), obj, false);
	return rc;
}

struct agg_tst_dataset {
	daos_unit_oid_t			 td_oid;
	daos_iod_type_t			 td_type;
	daos_epoch_range_t		 td_upd_epr;
	daos_epoch_range_t		 td_agg_epr;
	daos_recx_t			*td_recx;
	unsigned int			 td_recx_nr;
	daos_size_t			 td_iod_size;
	char				*td_expected_view;
	int				 td_expected_recs;
	bool				 td_discard;
	bool				 td_delete;
};

#define PARITY_BIT (1ULL << 63)
static daos_size_t
get_view_len(struct agg_tst_dataset *ds, daos_recx_t *recx)
{
	daos_size_t	view_len;

	if (ds->td_type == DAOS_IOD_SINGLE) {
		view_len = ds->td_iod_size;
	} else {
		uint64_t	start = PARITY_BIT - 1, end = 0, tmp;
		int		i;

		assert_true(ds->td_recx_nr > 0);
		for (i = 0; i < ds->td_recx_nr; i++) {
			if (ds->td_recx[i].rx_idx & PARITY_BIT)
				continue; /* Ignore "parity" for now */

			if (start > ds->td_recx[i].rx_idx)
				start = ds->td_recx[i].rx_idx;
			tmp = ds->td_recx[i].rx_idx + ds->td_recx[i].rx_nr;
			if (end < tmp)
				end = tmp;
		}
		recx->rx_idx = start;
		recx->rx_nr = end - start;
		view_len = ds->td_iod_size * recx->rx_nr;
	}
	assert_true(view_len > 0);

	return view_len;
}

static void
generate_view(struct io_test_args *arg, daos_unit_oid_t oid, char *dkey,
	      char *akey, struct agg_tst_dataset *ds)
{
	daos_epoch_range_t	*epr_u, *epr_a;
	daos_epoch_t		 view_epoch;
	daos_size_t		 view_len;
	daos_recx_t		 recx = { 0 };

	epr_u = &ds->td_upd_epr;
	epr_a = &ds->td_agg_epr;
	view_len = get_view_len(ds, &recx);

	VERBOSE_MSG("Generate logical view: OID:"DF_UOID", DKEY:%s, AKEY:%s, "
		    "U_ERP:["DF_U64", "DF_U64"], A_EPR["DF_U64", "DF_U64"], "
		    "discard:%d, expected_nr:%d\n", DP_UOID(oid), dkey, akey,
		    epr_u->epr_lo, epr_u->epr_hi, epr_a->epr_lo, epr_a->epr_hi,
		    ds->td_discard, ds->td_expected_recs);

	/* Setup expected logical view from aggregate/discard epr_hi */
	D_ALLOC(ds->td_expected_view, view_len);
	assert_non_null(ds->td_expected_view);

	/* All updates below discard epr will be discarded */
	if (ds->td_discard && epr_u->epr_lo >= epr_a->epr_lo) {
		memset(ds->td_expected_view, 0, view_len);
		return;
	}

	view_epoch = ds->td_discard ? (epr_a->epr_lo - 1) : epr_a->epr_hi;

	fetch_value(arg, oid, view_epoch, 0, dkey, akey, ds->td_type,
		    ds->td_iod_size, &recx, ds->td_expected_view);
}

static void
verify_view(struct io_test_args *arg, daos_unit_oid_t oid, char *dkey,
	    char *akey, struct agg_tst_dataset *ds)
{
	daos_epoch_range_t	*epr_a;
	char			*buf_f;
	daos_size_t		 view_len;
	daos_recx_t		 recx = {0};
	int			 nr;

	VERBOSE_MSG("Verify logical view\n");
	assert_true(ds->td_expected_view != NULL);

	epr_a = &ds->td_agg_epr;
	/* Verify expected physical records in aggregated/discard epr */
	if (ds->td_expected_recs != -1) {
		nr = phy_recs_nr(arg, oid, epr_a, dkey, akey, ds->td_type);
		assert_int_equal(ds->td_expected_recs, nr);
	}

	/* Verify expected logical view from aggregate/discard epr_hi */
	view_len = get_view_len(ds, &recx);

	D_ALLOC(buf_f, view_len);
	assert_non_null(buf_f);

	fetch_value(arg, oid, epr_a->epr_hi, 0, dkey, akey, ds->td_type,
		    ds->td_iod_size, &recx, buf_f);

	assert_memory_equal(buf_f, ds->td_expected_view, view_len);

	D_FREE(buf_f);
	D_FREE(ds->td_expected_view);
	ds->td_expected_view = NULL;
}

static void
generate_recx(daos_recx_t *recx_tot, daos_recx_t *recx)
{
	uint64_t	max_nr;

	recx->rx_idx = recx_tot->rx_idx + rand() % recx_tot->rx_nr;
	max_nr = recx_tot->rx_idx + recx_tot->rx_nr - recx->rx_idx;
	recx->rx_nr = rand() % max_nr + 1;
}

static void
generate_akeys(struct io_test_args *arg, daos_unit_oid_t oid, int nr)
{
	char	 dkey[UPDATE_DKEY_SIZE] = { 0 };
	char	 akey[UPDATE_AKEY_SIZE] = { 0 };
	char	*buf_u;
	int	 i;

	D_ALLOC(buf_u, 10);
	assert_non_null(buf_u);

	dts_key_gen(dkey, UPDATE_DKEY_SIZE, UPDATE_DKEY);
	for (i = 0; i < nr; i++) {
		dts_key_gen(akey, UPDATE_AKEY_SIZE, UPDATE_AKEY);
		update_value(arg, oid, 1, 0, dkey, akey, DAOS_IOD_SINGLE,
			     10, NULL, buf_u);
	}
	D_FREE(buf_u);
}

static void
aggregate_basic_lb(struct io_test_args *arg, struct agg_tst_dataset *ds, int punch_nr,
		   daos_epoch_t punch_epoch[], daos_epoch_t punch_bound[], unsigned int agg_flags)
{
	daos_unit_oid_t		 oid;
	char			 dkey[UPDATE_DKEY_SIZE] = { 0 };
	char			 akey[UPDATE_AKEY_SIZE] = { 0 };
	daos_epoch_range_t	*epr_u, *epr_a;
	daos_epoch_t		 epoch;
	char			*buf_u;
	daos_recx_t		 recx = { 0 }, *recx_p;
	daos_size_t		 view_len;
	int			 punch_idx = 0, recx_idx = 0, rc = 0;
	int			 punch_or_delete = TF_PUNCH;

	if (ds->td_delete)
		punch_or_delete = TF_DELETE;

	if (daos_unit_oid_is_null(ds->td_oid))
		oid = dts_unit_oid_gen(0, 0);
	else
		oid = ds->td_oid;
	dts_key_gen(dkey, UPDATE_DKEY_SIZE, UPDATE_DKEY);
	dts_key_gen(akey, UPDATE_AKEY_SIZE, UPDATE_AKEY);

	epr_u = &ds->td_upd_epr;
	epr_a = &ds->td_agg_epr;
	VERBOSE_MSG("Update epr ["DF_U64", "DF_U64"]\n",
		    epr_u->epr_lo, epr_u->epr_hi);

	view_len = get_view_len(ds, &recx);
	D_ALLOC(buf_u, view_len);
	assert_non_null(buf_u);

	for (epoch = epr_u->epr_lo; epoch <= epr_u->epr_hi; epoch++) {
		if (punch_idx < punch_nr && punch_epoch[punch_idx] == epoch) {
			arg->ta_flags |= punch_or_delete;
			arg->epr_lo = punch_bound == NULL ? 0 : punch_bound[punch_idx];
			punch_idx++;
		} else if (punch_nr < 0 && (rand() % 2) &&
			   epoch != epr_u->epr_lo) {
			arg->ta_flags |= punch_or_delete;
		}

		if (ds->td_type == DAOS_IOD_SINGLE) {
			recx_p = NULL;
		} else {
			assert_true(recx_idx < ds->td_recx_nr);
			recx_p = &ds->td_recx[recx_idx];
			recx_idx++;
		}

		update_value(arg, oid, epoch, 0, dkey, akey, ds->td_type,
			     ds->td_iod_size, recx_p, buf_u);
		arg->ta_flags &= ~punch_or_delete;
	}
	D_FREE(buf_u);

	generate_view(arg, oid, dkey, akey, ds);

	VERBOSE_MSG("%s epr ["DF_U64", "DF_U64"]\n", ds->td_discard ?
		    "Discard" : "Aggregate", epr_a->epr_lo, epr_a->epr_hi);

	if (ds->td_discard)
		rc = vos_discard(arg->ctx.tc_co_hdl, NULL /* objp */, epr_a, NULL, NULL);
	else
		rc = vos_aggregate(arg->ctx.tc_co_hdl, epr_a, NULL, NULL, agg_flags);
	if (rc != -DER_CSUM) {
		/* Skip delete verification for now */
		assert_rc_equal(rc, 0);
		verify_view(arg, oid, dkey, akey, ds);
	} else {
		/*
		 * not calling verify_view so must free ds->td_expected_view
		 * here
		 */
		D_FREE(ds->td_expected_view);
	}
}

static void
aggregate_basic(struct io_test_args *arg, struct agg_tst_dataset *ds,
		int punch_nr, daos_epoch_t punch_epoch[])
{
	aggregate_basic_lb(arg, ds, punch_nr, punch_epoch, NULL, VOS_AGG_FL_FORCE_MERGE);
}

static inline int
get_ds_index(int oid_idx, int dkey_idx, int akey_idx, int nr)
{
	return oid_idx * nr * nr + dkey_idx * nr + akey_idx;
}

static void
generate_or_verify(struct io_test_args *arg, daos_unit_oid_t oid, char *dkey,
		   char *akey, struct agg_tst_dataset *ds_arr, int ds_idx,
		   bool verify)
{
	struct agg_tst_dataset	*ds = &ds_arr[ds_idx];

	/* It's possible that some keys are not touched by random updates */
	if (ds->td_type != DAOS_IOD_SINGLE &&
	    ds->td_type != DAOS_IOD_ARRAY) {
		VERBOSE_MSG("Skip uninitialized ds. ds_idx:%d\n", ds_idx);
		return;
	}

	if (verify)
		verify_view(arg, oid, dkey, akey, ds);
	else
		generate_view(arg, oid, dkey, akey, ds);

}

static void
multi_view(struct io_test_args *arg, daos_unit_oid_t oids[],
	   char dkeys[][UPDATE_DKEY_SIZE], char akeys[][UPDATE_DKEY_SIZE],
	   int nr, struct agg_tst_dataset *ds_arr, bool verify)
{
	daos_unit_oid_t	oid;
	char		*dkey, *akey;
	int		 oid_idx, dkey_idx, akey_idx, ds_idx;

	for (oid_idx = 0; oid_idx < nr; oid_idx++) {
		oid = oids[oid_idx];

		for (dkey_idx = 0; dkey_idx < nr; dkey_idx++) {
			dkey = dkeys[dkey_idx];

			for (akey_idx = 0; akey_idx < nr; akey_idx++) {
				akey = akeys[akey_idx];
				ds_idx = get_ds_index(oid_idx, dkey_idx,
						      akey_idx, nr);

				generate_or_verify(arg, oid, dkey, akey,
						   ds_arr, ds_idx, verify);
			}
		}
	}
}

#define AT_SV_IOD_SIZE_SMALL	32			/* SCM record */
#define AT_SV_IOD_SIZE_LARGE	(VOS_BLK_SZ + 500)	/* NVMe record */
#define AT_OBJ_KEY_NR		3

static void
aggregate_multi(struct io_test_args *arg, struct agg_tst_dataset *ds_sample)

{
	daos_unit_oid_t		 oid, oids[AT_OBJ_KEY_NR];
	char			 dkeys[AT_OBJ_KEY_NR][UPDATE_DKEY_SIZE];
	char			 akeys[AT_OBJ_KEY_NR][UPDATE_AKEY_SIZE];
	char			*dkey, *akey, *buf_u;
	struct agg_tst_dataset	*ds, *ds_arr;
	daos_epoch_t		 epoch;
	daos_epoch_range_t	*epr_u, *epr_a;
	daos_size_t		 view_len;
	daos_recx_t		 recx, *recx_p;
	int			 oid_idx, dkey_idx, akey_idx;
	int			 i, ds_nr, ds_idx, rc;

	epr_u = &ds_sample->td_upd_epr;
	epr_a = &ds_sample->td_agg_epr;

	for (i = 0; i < AT_OBJ_KEY_NR; i++) {
		oids[i] = dts_unit_oid_gen(0, 0);
		dts_key_gen(dkeys[i], UPDATE_DKEY_SIZE, UPDATE_DKEY);
		dts_key_gen(akeys[i], UPDATE_AKEY_SIZE, UPDATE_AKEY);
	}

	assert_true(ds_sample->td_type == DAOS_IOD_SINGLE ||
		    ds_sample->td_type == DAOS_IOD_ARRAY);
	ds_nr = AT_OBJ_KEY_NR * AT_OBJ_KEY_NR * AT_OBJ_KEY_NR;
	D_ALLOC_ARRAY(ds_arr, ds_nr);
	assert_non_null(ds_arr);

	for (i = 0; i < ds_nr; i++) {
		ds = &ds_arr[i];
		*ds = *ds_sample;
		/* Clear iod_type, update epr and expected recs*/
		ds->td_type = DAOS_IOD_NONE;
		memset(&ds->td_upd_epr, 0, sizeof(*epr_u));
		ds->td_expected_recs = 0;
	}

	/* Set maximum value for random iod_size */
	if (ds_sample->td_iod_size == 0)
		ds_sample->td_iod_size = AT_SV_IOD_SIZE_LARGE;

	view_len = get_view_len(ds_sample, &recx);
	D_ALLOC(buf_u, view_len);
	assert_non_null(buf_u);

	VERBOSE_MSG("Generate random updates over multiple objs/keys.\n");
	for (epoch = epr_u->epr_lo; epoch <= epr_u->epr_hi; epoch++) {
		oid_idx = rand() % AT_OBJ_KEY_NR;
		dkey_idx = rand() % AT_OBJ_KEY_NR;
		akey_idx = rand() % AT_OBJ_KEY_NR;

		oid = oids[oid_idx];
		dkey = dkeys[dkey_idx];
		akey = akeys[akey_idx];

		ds_idx = get_ds_index(oid_idx, dkey_idx, akey_idx,
				      AT_OBJ_KEY_NR);
		ds = &ds_arr[ds_idx];
		ds->td_type = ds_sample->td_type;

		/* First update can't be punched record */
		if ((rand() % 2) && (ds->td_iod_size != 0))
			arg->ta_flags |= TF_PUNCH;

		if (ds->td_iod_size == 0)
			ds->td_iod_size = (rand() % ds_sample->td_iod_size) + 1;

		if (ds->td_type == DAOS_IOD_SINGLE) {
			recx_p = NULL;
			/*
			 * Amend expected recs, set expected recs to 1 when it's
			 * aggregation and any updates located in aggregate EPR.
			 */
			if (!ds->td_discard && epoch >= epr_a->epr_lo &&
			    epoch <= epr_a->epr_hi)
				ds->td_expected_recs = 1;
		} else {
			assert_true(ds->td_recx_nr == 1);
			recx_p = &recx;
			generate_recx(&ds->td_recx[0], recx_p);
			ds->td_expected_recs = ds->td_discard ? 0 : -1;
		}

		/* Amend update epr */
		if (ds->td_upd_epr.epr_lo == 0)
			ds->td_upd_epr.epr_lo = epoch;
		ds->td_upd_epr.epr_hi = epoch;

		update_value(arg, oid, epoch, 0, dkey, akey, ds->td_type,
			     ds->td_iod_size, recx_p, buf_u);
		arg->ta_flags &= ~TF_PUNCH;

	}
	D_FREE(buf_u);

	multi_view(arg, oids, dkeys, akeys, AT_OBJ_KEY_NR, ds_arr, false);

	VERBOSE_MSG("%s multiple objs/keys\n", ds_sample->td_discard ?
		    "Discard" : "Aggregate");

	if (ds_sample->td_discard)
		rc = vos_discard(arg->ctx.tc_co_hdl, NULL /* objp */, epr_a, NULL, NULL);
	else
		rc = vos_aggregate(arg->ctx.tc_co_hdl, epr_a, NULL, NULL, 0);
	assert_rc_equal(rc, 0);

	multi_view(arg, oids, dkeys, akeys, AT_OBJ_KEY_NR, ds_arr, true);
	D_FREE(ds_arr);
}

/*
 * Discard on single akey->SV with specified epoch.
 */
static void
discard_1(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	int			 i;

	ds.td_type = DAOS_IOD_SINGLE;
	ds.td_recx_nr = 0;
	ds.td_expected_recs = 0;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 10;
	ds.td_agg_epr.epr_lo = ds.td_agg_epr.epr_hi = 5;
	ds.td_discard = true;

	for (i = 0; i < 2; i++) {
		ds.td_iod_size = i == 0 ? AT_SV_IOD_SIZE_SMALL :
					  AT_SV_IOD_SIZE_LARGE;

		VERBOSE_MSG("Discard epoch "DF_U64", iod_size:"DF_U64"\n",
			    ds.td_agg_epr.epr_lo, ds.td_iod_size);
		aggregate_basic(arg, &ds, 0, NULL);
	}

	cleanup();
}
/*
 * Discard on single akey-SV with epr [A, B].
 */
static void
discard_2(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	int			 i;

	ds.td_type = DAOS_IOD_SINGLE;
	ds.td_recx_nr = 0;
	ds.td_expected_recs = 0;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 10;
	ds.td_agg_epr.epr_lo = 4;
	ds.td_agg_epr.epr_hi = 6;
	ds.td_discard = true;

	for (i = 0; i < 2; i++) {
		ds.td_iod_size = i == 0 ? AT_SV_IOD_SIZE_SMALL :
					  AT_SV_IOD_SIZE_LARGE;

		VERBOSE_MSG("Discard epr ["DF_U64", "DF_U64"], "
			    "iod_size:"DF_U64"\n", ds.td_agg_epr.epr_lo,
			    ds.td_agg_epr.epr_hi, ds.td_iod_size);
		aggregate_basic(arg, &ds, 0, NULL);
	}

	cleanup();
}

/*
 * Discard on single akey-SV with epr [0, DAOS_EPOCH_MAX].
 */
static void
discard_3(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	int			 i, rc;

	ds.td_type = DAOS_IOD_SINGLE;
	ds.td_recx_nr = 0;
	ds.td_expected_recs = 0;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 10;
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = DAOS_EPOCH_MAX;
	ds.td_discard = true;

	for (i = 0; i < 2; i++) {
		ds.td_iod_size = i == 0 ? AT_SV_IOD_SIZE_SMALL :
					  AT_SV_IOD_SIZE_LARGE;

		VERBOSE_MSG("Discard epr [0, MAX], iod_size:"DF_U64"\n",
			    ds.td_iod_size);
		aggregate_basic(arg, &ds, 0, NULL);
	}

	/* Object should have been deleted by discard */
	rc = lookup_object(arg, arg->oid);
	assert_rc_equal(rc, -DER_NONEXIST);

	cleanup();
}

/*
 * Discard on single akey-SV with epr [A, B], punch records involved.
 */
static void
discard_4(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_epoch_t		 punch_epoch[2];
	int			 i, punch_nr;

	ds.td_type = DAOS_IOD_SINGLE;
	ds.td_recx_nr = 0;
	ds.td_expected_recs = 0;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 10;
	ds.td_agg_epr.epr_lo = 5;
	ds.td_agg_epr.epr_hi = 6;
	ds.td_discard = true;

	punch_nr = 2;
	punch_epoch[0] = 4;
	punch_epoch[1] = 9;

	for (i = 0; i < 2; i++) {
		ds.td_iod_size = i == 0 ? AT_SV_IOD_SIZE_SMALL :
					  AT_SV_IOD_SIZE_LARGE;

		VERBOSE_MSG("Discard punch records, iod_size:"DF_U64"\n",
			    ds.td_iod_size);
		aggregate_basic(arg, &ds, punch_nr, punch_epoch);
	}

	cleanup();
}

/*
 * Discard on single akey-SV with epr [A, DAOS_EPOCH_MAX], random punch,
 * random yield.
 */
static void
discard_5(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	int			 i;

	ds.td_type = DAOS_IOD_SINGLE;
	ds.td_recx_nr = 0;
	ds.td_expected_recs = 0;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 200;
	ds.td_agg_epr.epr_lo = 50;
	ds.td_agg_epr.epr_hi = DAOS_EPOCH_MAX;
	ds.td_discard = true;

	daos_fail_loc_set(DAOS_VOS_AGG_RANDOM_YIELD | DAOS_FAIL_ALWAYS);
	for (i = 0; i < 2; i++) {
		ds.td_iod_size = i == 0 ? AT_SV_IOD_SIZE_SMALL :
					  AT_SV_IOD_SIZE_LARGE;

		VERBOSE_MSG("Discard with random punch & yield. "
			    "iod_size:"DF_U64"\n", ds.td_iod_size);
		aggregate_basic(arg, &ds, -1, NULL);
	}

	cleanup();
}

/*
 * Discard SV on multiple objects, keys.
 */
static void
discard_6(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };

	ds.td_type = DAOS_IOD_SINGLE;
	ds.td_iod_size = 0;	/* random iod_size */
	ds.td_recx_nr = 0;
	ds.td_expected_recs = 0;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 1000;
	ds.td_agg_epr.epr_lo = 850;
	ds.td_agg_epr.epr_hi = DAOS_EPOCH_MAX;
	ds.td_discard = true;

	aggregate_multi(arg, &ds);

	cleanup();
}

/*
 * Discard on single akey->EV with specified epoch.
 */
static void
discard_7(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[10];
	daos_recx_t		 recx_tot;
	int			 i;

	recx_tot.rx_idx = 0;
	recx_tot.rx_nr = 20;
	for (i = 0; i < 10; i++)
		generate_recx(&recx_tot, &recx_arr[i]);

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 1024;
	ds.td_expected_recs = 0;
	ds.td_recx_nr = 10;
	ds.td_recx = &recx_arr[0];
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 10;
	ds.td_agg_epr.epr_lo = ds.td_agg_epr.epr_hi = 5;
	ds.td_discard = true;

	VERBOSE_MSG("Discard epoch "DF_U64"\n", ds.td_agg_epr.epr_lo);
	aggregate_basic(arg, &ds, 0, NULL);

	cleanup();
}

/*
 * Discard on single akey->EV with epr [A, B].
 */
static void
discard_8(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[10];
	daos_recx_t		 recx_tot;
	int			 i;

	recx_tot.rx_idx = 0;
	recx_tot.rx_nr = 20;
	for (i = 0; i < 10; i++)
		generate_recx(&recx_tot, &recx_arr[i]);

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 1024;
	ds.td_expected_recs = 0;
	ds.td_recx_nr = 10;
	ds.td_recx = &recx_arr[0];
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 10;
	ds.td_agg_epr.epr_lo = 3;
	ds.td_agg_epr.epr_hi = 7;
	ds.td_discard = true;

	VERBOSE_MSG("Discard epr ["DF_U64", "DF_U64"]\n",
		    ds.td_agg_epr.epr_lo, ds.td_agg_epr.epr_hi);
	aggregate_basic(arg, &ds, 0, NULL);

	cleanup();
}

/*
 * Discard on single akey->EV with epr [0, DAOS_EPOCH_MAX].
 */
static void
discard_9(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[10];
	daos_recx_t		 recx_tot;
	int			 i, rc;

	recx_tot.rx_idx = 0;
	recx_tot.rx_nr = 20;
	for (i = 0; i < 10; i++)
		generate_recx(&recx_tot, &recx_arr[i]);

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 1024;
	ds.td_expected_recs = 0;
	ds.td_recx_nr = 10;
	ds.td_recx = &recx_arr[0];
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 10;
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = DAOS_EPOCH_MAX;
	ds.td_discard = true;

	VERBOSE_MSG("Discard epr [0, MAX]\n");
	aggregate_basic(arg, &ds, 0, NULL);

	/* Object should have been deleted by discard */
	rc = lookup_object(arg, arg->oid);
	assert_rc_equal(rc, -DER_NONEXIST);

	cleanup();
}

/*
 * Discard on single akey->EV with epr [A, B], punch records involved.
 */
static void
discard_10(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[10];
	daos_recx_t		 recx_tot;
	daos_epoch_t		 punch_epoch[3];
	int			 i, punch_nr;

	recx_tot.rx_idx = 0;
	recx_tot.rx_nr = 20;
	for (i = 0; i < 10; i++)
		generate_recx(&recx_tot, &recx_arr[i]);

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 1024;
	ds.td_expected_recs = 0;
	ds.td_recx_nr = 10;
	ds.td_recx = &recx_arr[0];
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 10;
	ds.td_agg_epr.epr_lo = 3;
	ds.td_agg_epr.epr_hi = 7;
	ds.td_discard = true;

	punch_nr = 3;
	punch_epoch[0] = 3;
	punch_epoch[1] = 4;
	punch_epoch[2] = 7;

	VERBOSE_MSG("Discard punch records\n");
	aggregate_basic(arg, &ds, punch_nr, punch_epoch);

	cleanup();
}

/*
 * Discard on single akey->EV with epr [A, DAOS_EPOCH_MAX], random punch,
 * random yield.
 */
static void
discard_11(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[200];
	daos_recx_t		 recx_tot;
	int			 i;

	recx_tot.rx_idx = 0;
	recx_tot.rx_nr = 20;
	for (i = 0; i < 200; i++)
		generate_recx(&recx_tot, &recx_arr[i]);

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 1024;
	ds.td_expected_recs = 0;
	ds.td_recx_nr = 200;
	ds.td_recx = &recx_arr[0];
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 200;
	ds.td_agg_epr.epr_lo = 100;
	ds.td_agg_epr.epr_hi = DAOS_EPOCH_MAX;
	ds.td_discard = true;

	VERBOSE_MSG("Discard with random punch, random yield.\n");

	daos_fail_loc_set(DAOS_VOS_AGG_RANDOM_YIELD | DAOS_FAIL_ALWAYS);
	aggregate_basic(arg, &ds, -1, NULL);

	cleanup();
}

/*
 * Discard EV on multiple objects, keys.
 */
static void
discard_12(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_tot;

	recx_tot.rx_idx = 0;
	recx_tot.rx_nr = 30;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 0;	/* random iod_size */
	ds.td_expected_recs = 0;
	ds.td_recx_nr = 1;
	ds.td_recx = &recx_tot;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 1000;
	ds.td_agg_epr.epr_lo = 750;
	ds.td_agg_epr.epr_hi = DAOS_EPOCH_MAX;
	ds.td_discard = true;

	aggregate_multi(arg, &ds);

	cleanup();
}

/*
 * Discard won't run into infinite loop.
 */
static void
discard_13(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[200];
	daos_recx_t		 recx_tot;
	int			 i;

	ds.td_oid = dts_unit_oid_gen(0, 0);
	/*
	 * Generate enough amount of akeys to ensure vos_iterate()
	 * trigger re-probe on dkey
	 */
	generate_akeys(arg, ds.td_oid, AGG_CREDS_SCAN_TIGHT + 10);

	recx_tot.rx_idx = 0;
	recx_tot.rx_nr = 20;
	for (i = 0; i < 200; i++)
		generate_recx(&recx_tot, &recx_arr[i]);

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 1024;
	ds.td_expected_recs = -1;
	ds.td_recx_nr = 200;
	ds.td_recx = &recx_arr[0];
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 200;
	ds.td_agg_epr.epr_lo = 100;
	ds.td_agg_epr.epr_hi = DAOS_EPOCH_MAX;
	ds.td_discard = true;

	aggregate_basic(arg, &ds, -1, NULL);

	cleanup();
}

enum {
	AGG_OBJ_TYPE,
	AGG_DKEY_TYPE,
	AGG_AKEY_TYPE,
};

/** Type of update */
enum {
	AGG_NONE,
	AGG_PUNCH,
	AGG_UPDATE,
};

static void
do_punch(struct io_test_args *arg, int type, daos_unit_oid_t oid,
	 daos_epoch_t epoch, char *dkey, char *akey)
{
	daos_key_t	*dkey_ptr = NULL;
	daos_key_t	*akey_ptr = NULL;
	int		 num_akeys = 0;
	daos_key_t	 dkey_iov;
	daos_key_t	 akey_iov;
	int		 rc;

	if (type >= AGG_DKEY_TYPE) {
		dkey_ptr = &dkey_iov;
		d_iov_set(&dkey_iov, dkey, strlen(dkey));
	}

	if (type == AGG_AKEY_TYPE) {
		akey_ptr = &akey_iov;
		d_iov_set(&akey_iov, akey, strlen(akey));
		num_akeys = 1;
	}

	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch, 0, 0,
			   dkey_ptr, num_akeys, akey_ptr, NULL);
	assert_rc_equal(rc, 0);
}

#define NUM_INTERNAL 200
static void
agg_punches_test_helper(void **state, int record_type, int type, bool discard,
			int first, int last)
{
	struct io_test_args	*arg = *state;
	daos_unit_oid_t		 oid;
	daos_epoch_range_t	 epr = {1, DAOS_EPOCH_MAX - 1};
	daos_epoch_t		 epoch;
	daos_epoch_t		 middle_epoch = 0;
	int			 i, rc;
	char			 first_val = 'f';
	char			 last_val = 'l';
	char			 middle_val = 'm';
	char			 fetch_val;
	char			 expected;
	char			 dkey[2] = "a";
	char			 akey[2] = "b";
	int			 old_flags = arg->ta_flags;
	daos_recx_t		 recx = {0, 1};

	oid = dts_unit_oid_gen(0, 0);

	arg->ta_flags = TF_USE_VAL;

	if (first != AGG_NONE) {
		update_value(arg, oid, epr.epr_lo++, 0, dkey, akey,
			     record_type, sizeof(first_val), &recx,
			     &first_val);
		if (first == AGG_PUNCH) {
			/* Punch the first update */
			do_punch(arg, type, oid, epr.epr_lo++, dkey, akey);
		}
	}

	/** fake snapshot at epr.epr_lo, if first != AGG_NONE */
	epoch = epr.epr_lo + 1;

	for (i = 1; i <= NUM_INTERNAL; i++) {
		bool	punch = (rand() % 5) == 0;

		if (i == NUM_INTERNAL || punch) {
			do_punch(arg, type, oid, epoch++, dkey, akey);
			continue;
		}
		update_value(arg, oid, epoch++, 0, dkey, akey,
			     record_type, sizeof(middle_val), &recx,
			     &middle_val);
		middle_epoch = epoch;
	}

	if (last == AGG_UPDATE) {
		update_value(arg, oid, epoch++, 0, dkey, akey, record_type,
			     sizeof(last_val), &recx, &last_val);
	}

	/* Set upper bound for aggregation */
	epr.epr_hi = epoch++;

	for (i = 0; i < 2; i++) {
		if (discard)
			rc = vos_discard(arg->ctx.tc_co_hdl, NULL /* objp */, &epr, NULL, NULL);
		else
			rc = vos_aggregate(arg->ctx.tc_co_hdl, &epr, NULL, NULL, 0);

		assert_rc_equal(rc, 0);

		if (first != AGG_NONE) {
			/* regardless of aggregate or discard, the first entry
			 * should exist because it's outside of the epr.
			 */
			fetch_val = 0;
			fetch_value(arg, oid, 1, 0, dkey, akey, record_type,
				    sizeof(first_val), &recx, &fetch_val);
			assert_int_equal(fetch_val, first_val);
			/* Reading at "snapshot" should also work except for
			 * punch, it will be gone.
			 */
			fetch_val = 0;
			fetch_value(arg, oid, epr.epr_lo, 0, dkey, akey,
				    record_type, sizeof(first_val), &recx,
				    &fetch_val);
			assert_int_equal(fetch_val,
					 (first == AGG_PUNCH) ? 0 : first_val);
		}

		/* Intermediate value should be gone regardless but fetch will
		 * get first_val if it's a discard
		 */
		expected = 0;
		fetch_val = 0;
		if (first == AGG_UPDATE && discard)
			expected = first_val;
		fetch_value(arg, oid, middle_epoch, 0, dkey, akey, record_type,
			    sizeof(middle_val), &recx, &fetch_val);
		assert_int_equal(fetch_val, expected);

		/* Final value should be present for aggregation but not
		 * discard
		 */
		fetch_val = 0;
		fetch_value(arg, oid, epr.epr_hi, 0, dkey, akey, record_type,
			    sizeof(last_val), &recx, &fetch_val);
		expected = last_val;
		if (discard) {
			expected = 0;
			if (first == AGG_UPDATE) {
				/** Old value should still be there */
				expected = first_val;
			}
		} else if (last != AGG_UPDATE) {
			expected = 0;
		}

		assert_int_equal(fetch_val, expected);

		/* One more test.  Punch the object at higher epoch, then
		 * aggregate same epoch should get same results as the punch
		 * is out of range.  Test is pointless for discard.
		 */
		if (discard)
			break;

		do_punch(arg, AGG_OBJ_TYPE, oid, epoch++, dkey, akey);
	}

	arg->ta_flags = old_flags;
}

/** Do a punch aggregation test */
static void
agg_punches_test(void **state, int record_type, bool discard)
{
	int	first, last, type;
	int	lstart;

	daos_fail_loc_set(DAOS_VOS_AGG_RANDOM_YIELD | DAOS_FAIL_ALWAYS);
	for (first = AGG_NONE; first <= AGG_UPDATE; first++) {
		lstart = first == AGG_NONE ? AGG_PUNCH : AGG_NONE;
		for (last = lstart; last <= AGG_UPDATE; last++) {
			for (type = AGG_OBJ_TYPE; type <= AGG_AKEY_TYPE;
			     type++) {
				agg_punches_test_helper(state, record_type,
							type, discard, first,
							last);
			}
		}
	}
	/** cleanup() sets the flag to assert if there are items in container garbage collection
	 *  heap which will always be the case for these punch tests.  So let's run garbage
	 *  collection before cleanup in this case.
	 */
	gc_wait();
}
static void
discard_14(void **state)
{
	agg_punches_test(state, DAOS_IOD_SINGLE, true);
	cleanup();
}

static void
discard_15(void **state)
{
	agg_punches_test(state, DAOS_IOD_ARRAY, true);
	cleanup();
}

static void
discard_obj_test(void **state, bool empty)
{
	struct io_test_args	*arg = *state;
	daos_unit_oid_t		 oid;
	daos_epoch_range_t	 epr = {1, DAOS_EPOCH_MAX - 1};
	daos_epoch_t		 epoch;
	int			 rc;
	char			 first_val = 'f';
	char			 middle_val = 'm';
	char			 fetch_val;
	char			 expected;
	char			 dkey[2] = "a";
	char			 akey[2] = "b";
	char			 akey2[2] = "c";
	int			 old_flags = arg->ta_flags;
	daos_recx_t		 recx = {0, 1};

	oid = dts_unit_oid_gen(0, 0);

	arg->ta_flags = TF_USE_VAL;

	if (!empty) {
		update_value(arg, oid, epr.epr_lo, 0, dkey, akey,
			     DAOS_IOD_SINGLE, sizeof(first_val), &recx,
			     &first_val);

		update_value(arg, oid, epr.epr_lo + 1, 0, dkey, akey2,
			     DAOS_IOD_ARRAY, sizeof(first_val), &recx,
			     &first_val);

	}

	epoch = epr.epr_lo + 2;

	update_value(arg, oid, epoch++, 0, dkey, akey,
		     DAOS_IOD_SINGLE, sizeof(middle_val), &recx,
		     &middle_val);

	update_value(arg, oid, epoch++, 0, dkey, akey2,
		     DAOS_IOD_ARRAY, sizeof(middle_val), &recx,
		     &middle_val);

	epr.epr_hi = epoch;

	rc = vos_discard(arg->ctx.tc_co_hdl, &oid, &epr, NULL, NULL);
	assert_int_equal(rc, 0);

	/* Middle value should now be gone.  If empty, it will be 0, otherwise will be first
	 * value
	 */
	expected = 0;
	fetch_val = 0;
	fetch_value(arg, oid, epr.epr_hi, 0, dkey, akey, DAOS_IOD_SINGLE,
		    sizeof(first_val), &recx, &fetch_val);
	assert_int_equal(fetch_val, expected);
	fetch_val = 0;
	fetch_value(arg, oid, epr.epr_hi, 0, dkey, akey2, DAOS_IOD_ARRAY,
		    sizeof(first_val), &recx, &fetch_val);
	assert_int_equal(fetch_val, expected);

	arg->ta_flags = old_flags;
}

static void
discard_16(void **state)
{
	discard_obj_test(state, true);
	cleanup();
}

static void
discard_17(void **state)
{
	discard_obj_test(state, false);
	cleanup();
}

/*
 * Aggregate on single akey-SV with epr [A, B].
 */
static void
aggregate_1(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	int			 i;

	ds.td_type = DAOS_IOD_SINGLE;
	ds.td_recx_nr = 0;
	ds.td_expected_recs = 1;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 10;
	ds.td_agg_epr.epr_lo = 4;
	ds.td_agg_epr.epr_hi = 6;
	ds.td_discard = false;

	for (i = 0; i < 2; i++) {
		ds.td_iod_size = i == 0 ? AT_SV_IOD_SIZE_SMALL :
					  AT_SV_IOD_SIZE_LARGE;

		VERBOSE_MSG("Aggregate epr ["DF_U64", "DF_U64"], "
			    "iod_size:"DF_U64"\n", ds.td_agg_epr.epr_lo,
			    ds.td_agg_epr.epr_hi, ds.td_iod_size);
		aggregate_basic(arg, &ds, 0, NULL);
	}

	cleanup();
}

/*
 * Aggregate on single akey-SV with epr [A, B], punch records involved.
 */
static void
aggregate_2(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_epoch_t		 punch_epoch[2];
	int			 i, punch_nr;

	ds.td_type = DAOS_IOD_SINGLE;
	ds.td_recx_nr = 0;
	ds.td_expected_recs = 1;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 10;
	ds.td_agg_epr.epr_lo = 2;
	ds.td_agg_epr.epr_hi = 6;
	ds.td_discard = false;

	punch_nr = 2;
	punch_epoch[0] = 3;
	punch_epoch[1] = 6;

	for (i = 0; i < 2; i++) {
		ds.td_iod_size = i == 0 ? AT_SV_IOD_SIZE_SMALL :
					  AT_SV_IOD_SIZE_LARGE;

		VERBOSE_MSG("Aggregate punch records, iod_size:"DF_U64"\n",
			    ds.td_iod_size);
		aggregate_basic(arg, &ds, punch_nr, punch_epoch);
	}
	cleanup();
}

/*
 * Aggregate on single akey-SV with epr [A, B], random punch, random yield.
 */
static void
aggregate_3(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	int			 i;

	ds.td_type = DAOS_IOD_SINGLE;
	ds.td_recx_nr = 0;
	ds.td_expected_recs = 1;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 200;
	ds.td_agg_epr.epr_lo = 50;
	ds.td_agg_epr.epr_hi = 150;
	ds.td_discard = false;

	daos_fail_loc_set(DAOS_VOS_AGG_RANDOM_YIELD | DAOS_FAIL_ALWAYS);
	for (i = 0; i < 2; i++) {
		ds.td_iod_size = i == 0 ? AT_SV_IOD_SIZE_SMALL :
					  AT_SV_IOD_SIZE_LARGE;

		VERBOSE_MSG("Aggregate with random punch & yield. "
			    "iod_size:"DF_U64"\n", ds.td_iod_size);
		aggregate_basic(arg, &ds, -1, NULL);
	}
	cleanup();
}

/*
 * Aggregate SV on multiple objects, keys.
 */
static void
aggregate_4(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };

	ds.td_type = DAOS_IOD_SINGLE;
	ds.td_iod_size = 0;	/* random iod_size */
	ds.td_recx_nr = 0;
	ds.td_expected_recs = 1;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 1000;
	ds.td_agg_epr.epr_lo = 850;
	ds.td_agg_epr.epr_hi = 999;
	ds.td_discard = false;

	aggregate_multi(arg, &ds);
	cleanup();
}

/*
 * Aggregate on single akey-EV, single record.
 */
static void
aggregate_5(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_tot, recx_arr[2];
	daos_epoch_t		 punch_epoch[1];
	int			 punch_nr;

	recx_tot.rx_idx = 0;
	recx_tot.rx_nr = 20;
	generate_recx(&recx_tot, &recx_arr[0]);
	generate_recx(&recx_tot, &recx_arr[1]);

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = (rand() % AT_SV_IOD_SIZE_LARGE) + 1;
	ds.td_recx_nr = 2;
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 1;
	ds.td_upd_epr.epr_lo = 5;
	ds.td_upd_epr.epr_hi = 6;
	ds.td_agg_epr.epr_lo = 1;
	ds.td_agg_epr.epr_hi = 5; /* aggregate epr contains 1 record */
	ds.td_discard = false;

	punch_epoch[0] = 5;

	for (punch_nr = 0; punch_nr < 2; punch_nr++) {
		VERBOSE_MSG("Aggregate single record, punch_nr: %d\n",
			    punch_nr);
		aggregate_basic(arg, &ds, punch_nr,
				punch_nr ? punch_epoch : NULL);
	}
	cleanup();
}

/*
 * Aggregate on single akey-EV, disjoint records.
 */
static void
aggregate_6(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[3];
	daos_epoch_t		 punch_epoch[1];
	int			 punch_nr = 1;

	recx_arr[0].rx_idx = 10;
	recx_arr[0].rx_nr = 5;
	recx_arr[1].rx_idx = 1;
	recx_arr[1].rx_nr = 2;
	recx_arr[2].rx_idx = 20;
	recx_arr[2].rx_nr = 11;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 1024;
	ds.td_recx_nr = 3;
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 3;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 3;
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = 4;
	ds.td_discard = false;

	punch_epoch[0] = 1;

	VERBOSE_MSG("Aggregate disjoint records\n");
	aggregate_basic(arg, &ds, punch_nr, punch_epoch);
	cleanup();
}

/*
 * Aggregate on single akey-EV, adjacent records.
 */
static void
aggregate_7(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[5];
	daos_epoch_t		 punch_epoch[2];
	int			 punch_nr = 2;

	recx_arr[1].rx_idx = 5;
	recx_arr[1].rx_nr = 1;
	recx_arr[0].rx_idx = 6;
	recx_arr[0].rx_nr = 2;
	recx_arr[2].rx_idx = 8;
	recx_arr[2].rx_nr = 3;
	recx_arr[3].rx_idx = 11;
	recx_arr[3].rx_nr = 4;
	recx_arr[4].rx_idx = 15;
	recx_arr[4].rx_nr = 5;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = AT_SV_IOD_SIZE_LARGE;
	ds.td_recx_nr = 5;
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 3;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 5;
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = 6;
	ds.td_discard = false;

	punch_epoch[0] = 3;
	punch_epoch[1] = 4;

	VERBOSE_MSG("Aggregate adjacent records\n");
	aggregate_basic(arg, &ds, punch_nr, punch_epoch);
	cleanup();
}

/*
 * Aggregate on single akey-EV, overlapped records.
 */
static void
aggregate_8(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[5];
	daos_epoch_t		 punch_epoch[2];
	int			 punch_nr = 2;

	recx_arr[1].rx_idx = 5;
	recx_arr[1].rx_nr = 1;
	recx_arr[0].rx_idx = 5;
	recx_arr[0].rx_nr = 3;
	recx_arr[2].rx_idx = 7;
	recx_arr[2].rx_nr = 4;
	recx_arr[3].rx_idx = 10;
	recx_arr[3].rx_nr = 5;
	recx_arr[4].rx_idx = 14;
	recx_arr[4].rx_nr = 5;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = AT_SV_IOD_SIZE_LARGE;
	ds.td_recx_nr = 5;
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 3;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 5;
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = 6;
	ds.td_discard = false;

	punch_epoch[0] = 3;
	punch_epoch[1] = 4;

	VERBOSE_MSG("Aggregate overlapped records\n");
	aggregate_basic(arg, &ds, punch_nr, punch_epoch);
	cleanup();
}

/*
 * Aggregate on single akey-EV, fully covered records.
 */
static void
aggregate_9(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[3];
	daos_epoch_t		 punch_epoch[2];
	int			 punch_nr = 2;

	recx_arr[0].rx_idx = 1;
	recx_arr[0].rx_nr = 2;
	recx_arr[1].rx_idx = 1;
	recx_arr[1].rx_nr = 2;
	recx_arr[2].rx_idx = 0;
	recx_arr[2].rx_nr = 4;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 1024;
	ds.td_recx_nr = 3;
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 1;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 3;
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = 4;
	ds.td_discard = false;

	punch_epoch[0] = 1;
	punch_epoch[1] = 3;

	VERBOSE_MSG("Aggregate fully covered records\n");
	aggregate_basic(arg, &ds, punch_nr, punch_epoch);
	cleanup();
}

/*
 * Aggregate on single akey-EV, records spans merge window.
 */
static void
aggregate_10(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[8];
	daos_epoch_t		 punch_epoch[3];
	int			 iod_size = 1024, punch_nr = 3, end_idx;

	end_idx = (VOS_MW_FLUSH_THRESH + iod_size - 1) / iod_size;
	assert_true(end_idx > 5);

	/* record in first window */
	recx_arr[0].rx_idx = 0;
	recx_arr[0].rx_nr = 1;
	/* punch record spans window, fully covered in first window */
	recx_arr[1].rx_idx = end_idx - 3;
	recx_arr[1].rx_nr = 5;
	/* record spans window, fully covered in first window */
	recx_arr[2].rx_idx = end_idx - 4;
	recx_arr[2].rx_nr = 6;
	/* punch record to fill up first window */
	recx_arr[3].rx_idx = 1;
	recx_arr[3].rx_nr = end_idx + 1;
	/* punch record spans window, partial covered in first window */
	recx_arr[4].rx_idx = end_idx - 5;
	recx_arr[4].rx_nr = 10;
	/* record spans window, partial covered in first window */
	recx_arr[5].rx_idx = end_idx - 4;
	recx_arr[5].rx_nr = 10;
	/* record in first window */
	recx_arr[6].rx_idx = end_idx - 3;
	recx_arr[6].rx_nr = 1;
	/* record in the next window */
	recx_arr[7].rx_idx = end_idx + 3;
	recx_arr[7].rx_nr = 1;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = iod_size;
	ds.td_recx_nr = 8;
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 4;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 8;
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = 9;
	ds.td_discard = false;

	punch_epoch[0] = 2;
	punch_epoch[1] = 4;
	punch_epoch[2] = 5;

	VERBOSE_MSG("Aggregate records spanning window end.\n");
	aggregate_basic(arg, &ds, punch_nr, punch_epoch);
	cleanup();
}

/*
 * Aggregate on single akey->EV, random punch, random yield.
 */
static void
aggregate_11(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[200];
	daos_recx_t		 recx_tot;
	int			 i;

	recx_tot.rx_idx = 0;
	recx_tot.rx_nr = 20;
	for (i = 0; i < 200; i++)
		generate_recx(&recx_tot, &recx_arr[i]);

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 1024;
	ds.td_expected_recs = -1;
	ds.td_recx_nr = 200;
	ds.td_recx = &recx_arr[0];
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 200;
	ds.td_agg_epr.epr_lo = 100;
	ds.td_agg_epr.epr_hi = 200;
	ds.td_discard = false;

	VERBOSE_MSG("Aggregate with random punch, random yield.\n");

	daos_fail_loc_set(DAOS_VOS_AGG_RANDOM_YIELD | DAOS_FAIL_ALWAYS);
	aggregate_basic(arg, &ds, -1, NULL);
	cleanup();
}

/*
 * Aggregate on single akey->EV, random punch, small flush threshold.
 */
static void
aggregate_12(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[500];
	daos_recx_t		 recx_tot;
	int			 i;

	recx_tot.rx_idx = 0;
	recx_tot.rx_nr = 1000;
	for (i = 0; i < 500; i++)
		generate_recx(&recx_tot, &recx_arr[i]);

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 10;
	ds.td_expected_recs = -1;
	ds.td_recx_nr = 500;
	ds.td_recx = &recx_arr[0];
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 500;
	ds.td_agg_epr.epr_lo = 100;
	ds.td_agg_epr.epr_hi = 500;
	ds.td_discard = false;

	VERBOSE_MSG("Aggregate with random punch, small flush threshold.\n");

	daos_fail_loc_set(DAOS_VOS_AGG_MW_THRESH | DAOS_FAIL_ALWAYS);
	daos_fail_value_set(50);
	aggregate_basic(arg, &ds, -1, NULL);
	cleanup();
}

/*
 * Aggregate EV on multiple objects, keys.
 */
static void
aggregate_13(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_tot;

	recx_tot.rx_idx = 0;
	recx_tot.rx_nr = 20;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 1024;
	ds.td_expected_recs = -1;
	ds.td_recx_nr = 1;
	ds.td_recx = &recx_tot;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 1000;
	ds.td_agg_epr.epr_lo = 750;
	ds.td_agg_epr.epr_hi = 1000;
	ds.td_discard = false;

	aggregate_multi(arg, &ds);
	cleanup();
}

static void
print_space_info(vos_pool_info_t *pi, char *desc)
{
	struct vos_pool_space	*vps = &pi->pif_space;
	struct vea_attr		*attr = &pi->pif_space.vps_vea_attr;
	struct vea_stat		*stat = &pi->pif_space.vps_vea_stat;

	VERBOSE_MSG("== Pool space information: %s ==\n", desc);
	VERBOSE_MSG("  Total bytes: SCM["DF_U64"], NVMe["DF_U64"]\n",
		    SCM_TOTAL(vps), NVME_TOTAL(vps));
	VERBOSE_MSG("  Free bytes : SCM["DF_U64"], NVMe["DF_U64"]\n",
		    SCM_FREE(vps), NVME_FREE(vps));

	/* NVMe isn't enabled */
	if (attr->va_tot_blks == 0)
		return;

	VERBOSE_MSG("  NVMe allocator statistics:\n");
	VERBOSE_MSG("    free_p: "DF_U64", \tfree_t: "DF_U64", "
		    "\tfrags_large: "DF_U64", \tfrags_small: "DF_U64", "
		    "\tfrags_aging: "DF_U64"\n",
		    stat->vs_free_persistent, stat->vs_free_transient,
		    stat->vs_frags_large, stat->vs_frags_small,
		    stat->vs_frags_aging);
	VERBOSE_MSG("    resrv_hit: "DF_U64", \tresrv_large: "DF_U64", "
		    "\tresrv_small: "DF_U64"\n", stat->vs_resrv_hint,
		    stat->vs_resrv_large, stat->vs_resrv_small);
}

static int
fill_cont(struct io_test_args *arg, daos_unit_oid_t oid, char *dkey,
	  char *akey, daos_size_t total, daos_epoch_t *epc_hi)
{
	char		*buf_u;
	daos_size_t	 iod_size = (1UL << 10), size_max = (1UL << 20);
	daos_size_t	 written = 0;
	daos_recx_t	 recx;
	uint64_t	 idx_max, nr_max;

	if (DAOS_ON_VALGRIND)
		size_max = (1UL << 14);

	D_ALLOC(buf_u, size_max);
	assert_non_null(buf_u);

	idx_max = (total / iod_size) / 5;
	nr_max = size_max / iod_size;
	assert_true(idx_max > nr_max);

	while (written < total) {
		recx.rx_idx = rand() % idx_max;
		recx.rx_nr = (rand() % nr_max) + 1;
		if (recx.rx_nr < (VOS_BLK_SZ / iod_size))
			recx.rx_nr = (VOS_BLK_SZ / iod_size);

		/* Add few random punches */
		if ((rand() % 10) > 7 && written != 0)
			arg->ta_flags |= TF_PUNCH;

		update_value(arg, oid, *epc_hi, 0, dkey, akey, DAOS_IOD_ARRAY,
			     iod_size, &recx, buf_u);
		(*epc_hi)++;
		if (arg->ta_flags & TF_PUNCH)
			arg->ta_flags &= ~TF_PUNCH;
		else
			written += (recx.rx_nr * iod_size);
	}
	D_FREE(buf_u);

	return 0;
}

/*
 * Update & Aggregate EV repeatedly.
 */
static void
aggregate_14(void **state)
{
	struct io_test_args	*arg = *state;
	vos_pool_info_t		 pool_info;
	struct vos_pool_space	*vps = &pool_info.pif_space;
	daos_epoch_t		 epc_hi = 1;
	daos_epoch_range_t	 epr;
	daos_size_t		 fill_size;
	daos_unit_oid_t		 oid;
	char			 dkey[UPDATE_DKEY_SIZE] = { 0 };
	char			 akey[UPDATE_AKEY_SIZE] = { 0 };
	int			 i, repeat_cnt, rc;

	rc = vos_pool_query(arg->ctx.tc_po_hdl, &pool_info);
	assert_rc_equal(rc, 0);
	print_space_info(&pool_info, "INIT");

	fill_size = NVME_FREE(vps) ? : SCM_FREE(vps);
	assert_true(fill_size > 0);

	if (DAOS_ON_VALGRIND) {
		fill_size = min(fill_size, 1ULL << 18);
		repeat_cnt = 2;
	} else if (slow_test) {
		fill_size = min(fill_size, VPOOL_2G);
		repeat_cnt = 5;
	} else {
		fill_size = min(fill_size, VPOOL_1G);
		repeat_cnt = 2;
	}
	fill_size = fill_size / 3;

	oid = dts_unit_oid_gen(0, 0);
	dts_key_gen(dkey, UPDATE_DKEY_SIZE, UPDATE_DKEY);
	dts_key_gen(akey, UPDATE_AKEY_SIZE, UPDATE_AKEY);

	epr.epr_lo = 0;
	for (i = 0; i < repeat_cnt; i++) {
		VERBOSE_MSG("Fill round: %d, size:"DF_U64", epc_hi:"DF_U64"\n",
			    i, fill_size, epc_hi);

		rc = fill_cont(arg, oid, dkey, akey, fill_size, &epc_hi);
		if (rc) {
			print_error("fill container %d failed:%d\n", i, rc);
			break;
		}

		rc = vos_pool_query(arg->ctx.tc_po_hdl, &pool_info);
		assert_rc_equal(rc, 0);
		print_space_info(&pool_info, "FILLED");

		VERBOSE_MSG("Aggregate round: %d\n", i);
		epr.epr_hi = epc_hi;
		rc = vos_aggregate(arg->ctx.tc_co_hdl, &epr, NULL, NULL, 0);
		if (rc) {
			print_error("aggregate %d failed: "DF_RC"\n", i,
				    DP_RC(rc));
			break;
		}

		rc = vos_pool_query(arg->ctx.tc_po_hdl, &pool_info);
		assert_rc_equal(rc, 0);
		print_space_info(&pool_info, "AGGREGATED");

		VERBOSE_MSG("Wait 10 secs for free extents expiring...\n");
		sleep(10);
	}

	rc = vos_pool_query(arg->ctx.tc_po_hdl, &pool_info);
	assert_rc_equal(rc, 0);
	print_space_info(&pool_info, "FINAL");

	assert_int_equal(i, repeat_cnt);
	cleanup();
}

static void
aggregate_15(void **state)
{
	agg_punches_test(state, DAOS_IOD_SINGLE, false);
	cleanup();
}

static void
aggregate_16(void **state)
{
	agg_punches_test(state, DAOS_IOD_ARRAY, false);
	cleanup();
}

/*
 * Aggregate on single akey-EV, disjoint records.
 */
static void
aggregate_17(void **state)
{
	struct io_test_args	*arg = *state;

	arg->ta_flags |= TF_USE_CSUMS;
	aggregate_6(state);
	arg->ta_flags &= ~TF_USE_CSUMS;
	cleanup();
}

/*
 * Aggregate on single akey-EV, fully covered records.
 */
static void
aggregate_18(void **state)
{
	struct io_test_args	*arg = *state;

	arg->ta_flags |= TF_USE_CSUMS;
	aggregate_9(state);
	arg->ta_flags &= ~TF_USE_CSUMS;
	cleanup();
}

/*
 * Aggregate on single akey-EV, records spans merge window.
 */
static void
aggregate_19(void **state)
{
	struct io_test_args	*arg = *state;

	arg->ta_flags |= TF_USE_CSUMS;
	aggregate_10(state);
	arg->ta_flags &= ~TF_USE_CSUMS;
	cleanup();
}

/*
 * Aggregate on single akey->EV, random punch, random yield.
 */
static void
aggregate_20(void **state)
{
	struct io_test_args	*arg = *state;

	arg->ta_flags |= TF_USE_CSUMS;
	aggregate_11(state);
	arg->ta_flags &= ~TF_USE_CSUMS;
	cleanup();
}
/*
 * Aggregate on single akey->EV, random punch, small flush threshold.
 */
static void
aggregate_21(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[500];
	daos_recx_t		 recx_tot;
	int			 i;

	recx_tot.rx_idx = 0;
	recx_tot.rx_nr = 1000;
	for (i = 0; i < 500; i++)
		generate_recx(&recx_tot, &recx_arr[i]);

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 16;
	ds.td_expected_recs = -1;
	ds.td_recx_nr = 500;
	ds.td_recx = &recx_arr[0];
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 500;
	ds.td_agg_epr.epr_lo = 100;
	ds.td_agg_epr.epr_hi = 500;
	ds.td_discard = false;

	VERBOSE_MSG("Aggregate with random punch, small flush threshold.\n");

	daos_fail_loc_set(DAOS_VOS_AGG_MW_THRESH | DAOS_FAIL_ALWAYS);
	daos_fail_value_set(50);
	arg->ta_flags |= TF_USE_CSUMS;
	aggregate_basic(arg, &ds, -1, NULL);
	arg->ta_flags &= ~TF_USE_CSUMS;
	cleanup();
}

static void
aggregate_22(void **state)
{
	struct io_test_args	*arg = *state;
	daos_unit_oid_t		 oid;
	char			 dkey[UPDATE_DKEY_SIZE] = { 0 };
	char			 akey[UPDATE_AKEY_SIZE] = { 0 };
	char			 akey2[UPDATE_AKEY_SIZE] = { 0 };
	char			 akey3[UPDATE_AKEY_SIZE] = { 0 };
	char			 akey4[UPDATE_AKEY_SIZE] = { 0 };
	daos_recx_t		 recx;
	daos_epoch_range_t	 epr;
	daos_epoch_t		 epoch = 100;
	char			 buf_u[16];
	int			 rc;

	oid = dts_unit_oid_gen(0, 0);

	dts_key_gen(dkey, UPDATE_DKEY_SIZE, UPDATE_DKEY);
	dts_key_gen(akey, UPDATE_AKEY_SIZE, UPDATE_AKEY);
	dts_key_gen(akey2, UPDATE_AKEY_SIZE, UPDATE_AKEY);
	dts_key_gen(akey3, UPDATE_AKEY_SIZE, UPDATE_AKEY);
	dts_key_gen(akey4, UPDATE_AKEY_SIZE, UPDATE_AKEY);
	recx.rx_idx = 0;
	recx.rx_nr = 1;

	epr.epr_lo = 0;

	memset(buf_u, 'x', sizeof(buf_u));

	update_value(arg, oid, epoch++, 0, dkey, akey, DAOS_IOD_ARRAY,
		     sizeof(buf_u), &recx, buf_u);
	update_value(arg, oid, epoch++, 0, dkey, akey2, DAOS_IOD_SINGLE,
		     sizeof(buf_u), &recx, buf_u);
	arg->ta_flags |= TF_PUNCH;
	update_value(arg, oid, epoch++, 0, dkey, akey3, DAOS_IOD_ARRAY,
		     sizeof(buf_u), &recx, buf_u);
	update_value(arg, oid, epoch++, 0, dkey, akey4, DAOS_IOD_SINGLE,
		     sizeof(buf_u), &recx, buf_u);

	fetch_value(arg, oid, epoch++,
		    VOS_OF_COND_AKEY_FETCH, dkey, akey,
		    DAOS_IOD_ARRAY, sizeof(buf_u), &recx, buf_u);
	fetch_value(arg, oid, epoch++,
		    VOS_OF_COND_AKEY_FETCH, dkey, akey2,
		    DAOS_IOD_SINGLE, sizeof(buf_u), &recx, buf_u);
	memset(buf_u, 0, sizeof(buf_u));
	fetch_value(arg, oid, epoch++,
		    VOS_OF_COND_AKEY_FETCH, dkey, akey3,
		    DAOS_IOD_ARRAY, sizeof(buf_u), &recx, buf_u);
	fetch_value(arg, oid, epoch++,
		    VOS_OF_COND_AKEY_FETCH, dkey, akey4,
		    DAOS_IOD_SINGLE, sizeof(buf_u), &recx, buf_u);

	update_value(arg, oid, epoch++, 0, dkey, akey, DAOS_IOD_ARRAY,
		     sizeof(buf_u), &recx, buf_u);
	update_value(arg, oid, epoch++, 0, dkey, akey2, DAOS_IOD_SINGLE,
		     sizeof(buf_u), &recx, buf_u);

	epr.epr_hi = epoch++;

	rc = vos_aggregate(arg->ctx.tc_co_hdl, &epr, NULL, NULL, 0);
	assert_rc_equal(rc, 0);

	fetch_value(arg, oid, epoch++,
		    VOS_OF_COND_AKEY_FETCH, dkey, akey,
		    DAOS_IOD_ARRAY, sizeof(buf_u), &recx, buf_u);
	fetch_value(arg, oid, epoch++,
		    VOS_OF_COND_AKEY_FETCH, dkey, akey2,
		    DAOS_IOD_SINGLE, sizeof(buf_u), &recx, buf_u);
	fetch_value(arg, oid, epoch++,
		    VOS_OF_COND_AKEY_FETCH, dkey, akey3,
		    DAOS_IOD_ARRAY, sizeof(buf_u), &recx, buf_u);
	fetch_value(arg, oid, epoch++,
		    VOS_OF_COND_AKEY_FETCH, dkey, akey4,
		    DAOS_IOD_SINGLE, sizeof(buf_u), &recx, buf_u);

	arg->ta_flags &= TF_PUNCH;

	memset(buf_u, 'x', sizeof(buf_u));

	/* Also check conditional updates still work */
	update_value(arg, oid, epoch++,
		     VOS_OF_COND_DKEY_UPDATE, dkey,
		     akey, DAOS_IOD_ARRAY, sizeof(buf_u), &recx, buf_u);
	update_value(arg, oid, epoch++,
		     VOS_OF_COND_AKEY_UPDATE, dkey,
		     akey2, DAOS_IOD_SINGLE, sizeof(buf_u), &recx, buf_u);
	update_value(arg, oid, epoch++,
		     VOS_OF_COND_AKEY_UPDATE, dkey,
		     akey3, DAOS_IOD_ARRAY, sizeof(buf_u), &recx, buf_u);
	update_value(arg, oid, epoch++,
		     VOS_OF_COND_DKEY_UPDATE, dkey,
		     akey4, DAOS_IOD_SINGLE, sizeof(buf_u), &recx, buf_u);
	cleanup();
}

static void
aggregate_23(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[8];
	daos_epoch_t		 punch_epoch[3];
	int			 iod_size = 1024, punch_nr = 3, end_idx;

	end_idx = (VOS_MW_FLUSH_THRESH + iod_size - 1) / iod_size;
	assert_true(end_idx > 5);

	/* record in first window */
	recx_arr[0].rx_idx = 0;
	recx_arr[0].rx_nr = 1;
	/* delete record spans window, fully covered in first window */
	recx_arr[1].rx_idx = end_idx - 3;
	recx_arr[1].rx_nr = 5;
	/* record spans window, fully covered in first window */
	recx_arr[2].rx_idx = end_idx - 4;
	recx_arr[2].rx_nr = 6;
	/* delete record to fill up first window */
	recx_arr[3].rx_idx = 1;
	recx_arr[3].rx_nr = end_idx + 1;
	/* delete record spans window, partial covered in first window */
	recx_arr[4].rx_idx = end_idx - 5;
	recx_arr[4].rx_nr = 10;
	/* delete spans window, partial covered in first window */
	recx_arr[5].rx_idx = end_idx - 4;
	recx_arr[5].rx_nr = 10;
	/* delete in first window */
	recx_arr[6].rx_idx = end_idx - 3;
	recx_arr[6].rx_nr = 1;
	/* delete in the next window */
	recx_arr[7].rx_idx = end_idx + 3;
	recx_arr[7].rx_nr = 1;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = iod_size;
	ds.td_recx_nr = 8;
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 4;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 8;
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = 9;
	ds.td_discard = false;
	ds.td_delete = true;

	punch_epoch[0] = 3;
	punch_epoch[1] = 4;
	punch_epoch[2] = 6;

	VERBOSE_MSG("Aggregate deleted records spanning window end.\n");
	aggregate_basic(arg, &ds, punch_nr, punch_epoch);
	cleanup();
}

static void
aggregate_24(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[4];
	daos_epoch_t		 punch_epochs[] = {2, 3, 4};
	int			 iod_size = 1024, end_idx;

	end_idx = (VOS_MW_FLUSH_THRESH + iod_size - 1) / iod_size;
	assert_true(end_idx > 5);

	/* Insert a record */
	recx_arr[0].rx_idx = end_idx - 5;
	recx_arr[0].rx_nr = 25;
	recx_arr[1].rx_idx = end_idx - 2;
	recx_arr[1].rx_nr = 4;
	recx_arr[2].rx_idx = end_idx + 4;
	recx_arr[2].rx_nr = 4;
	recx_arr[3].rx_idx = end_idx + 2;
	recx_arr[3].rx_nr = 2;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = iod_size;
	ds.td_recx_nr = ARRAY_SIZE(recx_arr);
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 2;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 4;
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = 5;
	ds.td_discard = false;
	ds.td_delete = true;

	VERBOSE_MSG("Aggregate extents not fully covered by delete record\n");
	aggregate_basic(arg, &ds, ARRAY_SIZE(punch_epochs), &punch_epochs[0]);
	cleanup();
}

static void
aggregate_25(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[6];
	daos_epoch_t		 punch_epochs[] = {2, 5};
	int			 iod_size = 1024, end_idx;

	end_idx = (VOS_MW_FLUSH_THRESH + iod_size - 1) / iod_size;
	assert_true(end_idx > 5);

	/* Insert a record */
	recx_arr[0].rx_idx = end_idx - 5;
	recx_arr[0].rx_nr = end_idx + 5;
	recx_arr[1].rx_idx = end_idx - 2;
	recx_arr[1].rx_nr = end_idx + 2;
	recx_arr[2].rx_idx = end_idx - 2;
	recx_arr[2].rx_nr = 4;
	recx_arr[3].rx_idx = (end_idx * 5) + end_idx - 5;
	recx_arr[3].rx_nr = end_idx + 5;
	recx_arr[4].rx_idx = (end_idx * 5) + end_idx - 2;
	recx_arr[4].rx_nr = end_idx + 2;
	recx_arr[5].rx_idx = (end_idx * 5) + end_idx - 2;
	recx_arr[5].rx_nr = 4;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = iod_size;
	ds.td_recx_nr = ARRAY_SIZE(recx_arr);
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 2;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = ARRAY_SIZE(recx_arr);
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = ARRAY_SIZE(recx_arr) + 1;
	ds.td_discard = false;
	ds.td_delete = true;

	VERBOSE_MSG("Aggregate delete of end of merge window\n")
	aggregate_basic(arg, &ds, ARRAY_SIZE(punch_epochs), &punch_epochs[0]);
	cleanup();
}

static void
aggregate_26(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[6];
	daos_epoch_t		 punch_epochs[] = {2, 3, 4, 5};
	int			 iod_size = 1024, end_idx;

	end_idx = (VOS_MW_FLUSH_THRESH + iod_size - 1) / iod_size;
	assert_true(end_idx > 5);

	/* Insert a record */
	recx_arr[0].rx_idx = 0;
	recx_arr[0].rx_nr = end_idx * 4;
	recx_arr[1].rx_idx = 0;
	recx_arr[1].rx_nr = end_idx;
	recx_arr[2].rx_idx = end_idx;
	recx_arr[2].rx_nr = end_idx;
	recx_arr[3].rx_idx = end_idx * 2;
	recx_arr[3].rx_nr = end_idx;
	recx_arr[4].rx_idx = end_idx * 3;
	recx_arr[4].rx_nr = end_idx;
	recx_arr[5].rx_idx = end_idx * 12;
	recx_arr[5].rx_nr = end_idx;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = iod_size;
	ds.td_recx_nr = ARRAY_SIZE(recx_arr);
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 1;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = ARRAY_SIZE(recx_arr);
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = ARRAY_SIZE(recx_arr) + 1;
	ds.td_discard = false;
	ds.td_delete = true;

	VERBOSE_MSG("Consecutive removed extents\n");
	aggregate_basic(arg, &ds, ARRAY_SIZE(punch_epochs), &punch_epochs[0]);
	cleanup();
}

static void
aggregate_27(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[5];
	daos_epoch_t		 punch_epochs[] = {2, 3, 4, 5};
	int			 iod_size = 1024, end_idx;

	end_idx = (VOS_MW_FLUSH_THRESH + iod_size - 1) / iod_size;
	assert_true(end_idx > 5);

	/* Insert a record */
	recx_arr[0].rx_idx = 0;
	recx_arr[0].rx_nr = end_idx * 4;
	recx_arr[1].rx_idx = 0;
	recx_arr[1].rx_nr = end_idx;
	recx_arr[2].rx_idx = end_idx;
	recx_arr[2].rx_nr = end_idx;
	recx_arr[3].rx_idx = end_idx * 2;
	recx_arr[3].rx_nr = end_idx;
	recx_arr[4].rx_idx = end_idx * 3;
	recx_arr[4].rx_nr = end_idx;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = iod_size;
	ds.td_recx_nr = ARRAY_SIZE(recx_arr);
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 0;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = ARRAY_SIZE(recx_arr);
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = ARRAY_SIZE(recx_arr) + 1;
	ds.td_discard = false;
	ds.td_delete = true;

	VERBOSE_MSG("Consecutive removed extents, no logical extents\n");
	aggregate_basic(arg, &ds, ARRAY_SIZE(punch_epochs), &punch_epochs[0]);
	cleanup();
}

static void
aggregate_28(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[6];
	daos_epoch_t		 punch_epochs[] = {3, 4, 5, 6};
	int			 iod_size = 1024, end_idx;

	end_idx = (VOS_MW_FLUSH_THRESH + iod_size - 1) / iod_size;
	assert_true(end_idx > 5);

	/* Insert a record */
	recx_arr[0].rx_idx = 0;
	recx_arr[0].rx_nr = end_idx;
	recx_arr[1].rx_idx = end_idx;
	recx_arr[1].rx_nr = end_idx * 4;
	recx_arr[2].rx_idx = end_idx;
	recx_arr[2].rx_nr = end_idx;
	recx_arr[3].rx_idx = end_idx * 2;
	recx_arr[3].rx_nr = end_idx;
	recx_arr[4].rx_idx = end_idx * 3;
	recx_arr[4].rx_nr = end_idx;
	recx_arr[5].rx_idx = end_idx * 4;
	recx_arr[5].rx_nr = end_idx;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = iod_size;
	ds.td_recx_nr = ARRAY_SIZE(recx_arr);
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 1;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = ARRAY_SIZE(recx_arr);
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = ARRAY_SIZE(recx_arr) + 1;
	ds.td_discard = false;
	ds.td_delete = true;

	VERBOSE_MSG("Logical extent followed by consecutive removed extents\n");
	aggregate_basic(arg, &ds, ARRAY_SIZE(punch_epochs), &punch_epochs[0]);
	cleanup();
}

static void
aggregate_29(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[6];
	daos_epoch_t		 punch_epochs[] = {3, 4, 5, 6};
	int			 iod_size = 1024, end_idx;

	end_idx = (VOS_MW_FLUSH_THRESH + iod_size - 1) / iod_size;
	assert_true(end_idx > 5);

	/* Insert a record */
	recx_arr[0].rx_idx = 0;
	recx_arr[0].rx_nr = 1;
	recx_arr[1].rx_idx = end_idx;
	recx_arr[1].rx_nr = end_idx * 4;
	recx_arr[2].rx_idx = end_idx;
	recx_arr[2].rx_nr = end_idx;
	recx_arr[3].rx_idx = end_idx * 2;
	recx_arr[3].rx_nr = end_idx;
	recx_arr[4].rx_idx = end_idx * 3;
	recx_arr[4].rx_nr = end_idx;
	recx_arr[5].rx_idx = end_idx * 4;
	recx_arr[5].rx_nr = end_idx;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = iod_size;
	ds.td_recx_nr = ARRAY_SIZE(recx_arr);
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 1;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = ARRAY_SIZE(recx_arr);
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = ARRAY_SIZE(recx_arr) + 1;
	ds.td_discard = false;
	ds.td_delete = true;

	VERBOSE_MSG("Logical extent followed by disjoint removed extents\n");
	aggregate_basic(arg, &ds, ARRAY_SIZE(punch_epochs), &punch_epochs[0]);
	cleanup();
}

#define MAX_OPS 100
#define MAX_REMOVE 50
static void
removal_stress_case(struct io_test_args *arg, int recx_nr, daos_recx_t *recx_arr, int remove_nr,
		    daos_epoch_t *remove_epochs, daos_epoch_t *remove_bounds, bool add_parity)
{
	struct agg_tst_dataset	 ds = { 0 };
	int			 iod_size = 1024, end_idx;
	int			 i;
	int			 remove = 0;

	D_ASSERT(recx_nr > remove_nr && (recx_nr % remove_nr) == 0);
	D_ASSERT(recx_nr <= MAX_OPS);
	D_ASSERT(remove_nr <= MAX_REMOVE);
	end_idx = (VOS_MW_FLUSH_THRESH + iod_size - 1) / iod_size;
	assert_true(end_idx > 5);

	for (i = 0; i < recx_nr; i++) {
		recx_arr[i].rx_idx = MAX(0, (i % 7) * end_idx - i);
		if (add_parity && (i & 1) == 0)
			recx_arr[i].rx_idx |= PARITY_BIT;
		recx_arr[i].rx_nr = end_idx + end_idx * (i % 2);
		if ((i + 1) % (recx_nr / remove_nr) == 0) {
			remove_epochs[remove] = i + 1;
			remove_bounds[remove] = MAX(0, (i + 1) - 23);
			VERBOSE_MSG("delete "DF_U64" records at "DF_X64"@"DF_X64"-"DF_X64"\n",
				    recx_arr[i].rx_nr, recx_arr[i].rx_idx, remove_bounds[remove],
			       remove_epochs[remove]);
			remove++;
		} else {
			VERBOSE_MSG("update "DF_U64" records at "DF_X64"@%x\n", recx_arr[i].rx_nr,
				    recx_arr[i].rx_idx, i + 1);
		}
		fflush(stdout);
	}

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = iod_size;
	ds.td_recx_nr = recx_nr;
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = -1;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = recx_nr;
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = recx_nr + 1;
	ds.td_discard = false;
	ds.td_delete = true;

	VERBOSE_MSG("Stress test recx_nr=%d, remove_nr=%d\n", recx_nr, remove_nr);
	aggregate_basic_lb(arg, &ds, remove_nr, &remove_epochs[0], &remove_bounds[0], 0);
}

static void
aggregate_30(void **state)
{
	struct io_test_args	*arg = *state;
	daos_recx_t		*recx_arr;
	daos_epoch_t		*remove_epochs;
	daos_epoch_t		*remove_bounds;

	D_ALLOC_ARRAY(recx_arr, MAX_OPS);
	assert_non_null(recx_arr);
	D_ALLOC_ARRAY(remove_epochs, MAX_REMOVE);
	assert_non_null(remove_epochs);
	D_ALLOC_ARRAY(remove_bounds, MAX_REMOVE);
	assert_non_null(remove_bounds);

	removal_stress_case(arg, 10, recx_arr, 2, remove_epochs, remove_bounds, false);
	removal_stress_case(arg, 14, recx_arr, 7, remove_epochs, remove_bounds, false);
	removal_stress_case(arg, 20, recx_arr, 4, remove_epochs, remove_bounds, true);
	if (!DAOS_ON_VALGRIND) {
		removal_stress_case(arg, 24, recx_arr, 6, remove_epochs, remove_bounds, false);
		removal_stress_case(arg, 30, recx_arr, 3, remove_epochs, remove_bounds, false);
		removal_stress_case(arg, 40, recx_arr, 4, remove_epochs, remove_bounds, false);
		removal_stress_case(arg, 50, recx_arr, 5, remove_epochs, remove_bounds, false);
		removal_stress_case(arg, 60, recx_arr, 15, remove_epochs, remove_bounds, true);
		removal_stress_case(arg, 75, recx_arr, 25, remove_epochs, remove_bounds, false);
		removal_stress_case(arg, 80, recx_arr, 8, remove_epochs, remove_bounds, false);
		removal_stress_case(arg, 100, recx_arr, 10, remove_epochs, remove_bounds, true);
	}

	D_FREE(recx_arr);
	D_FREE(remove_epochs);
	D_FREE(remove_bounds);

	cleanup();
}

static void
aggregate_31(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[4];
	daos_epoch_t		 punch_epochs[] = {4};
	int			 iod_size = 1024, end_idx;

	end_idx = (VOS_MW_FLUSH_THRESH + iod_size - 1) / iod_size;
	assert_true(end_idx > 5);

	/* Insert a record */
	recx_arr[0].rx_idx = 0;
	recx_arr[0].rx_nr = end_idx * 2;
	recx_arr[1].rx_idx = 0;
	recx_arr[1].rx_nr = end_idx;
	recx_arr[2].rx_idx = end_idx;
	recx_arr[2].rx_nr = end_idx;
	recx_arr[3].rx_idx = 0;
	recx_arr[3].rx_nr = end_idx * 2;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = iod_size;
	ds.td_recx_nr = ARRAY_SIZE(recx_arr);
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 0;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = ARRAY_SIZE(recx_arr);
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = ARRAY_SIZE(recx_arr) + 1;
	ds.td_discard = false;
	ds.td_delete = true;

	aggregate_basic(arg, &ds, ARRAY_SIZE(punch_epochs), &punch_epochs[0]);
	cleanup();
}

static void
aggregate_32(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[11];
	daos_epoch_t		 punch_epochs[] = {3, 4, 6, 7, 9, 10, 11};
	int			 iod_size = 1024, end_idx;

	end_idx = (VOS_MW_FLUSH_THRESH + iod_size - 1) / iod_size;
	assert_true(end_idx > 5);

	/* Insert a record */
	recx_arr[0].rx_idx = 0;
	recx_arr[0].rx_nr = end_idx * 2;
	recx_arr[1].rx_idx = 0;
	recx_arr[1].rx_nr = end_idx + 2;
	recx_arr[2].rx_idx = end_idx;
	recx_arr[2].rx_nr = end_idx;
	recx_arr[3].rx_idx = 0;
	recx_arr[3].rx_nr = end_idx + 2;
	recx_arr[4].rx_idx = 0;
	recx_arr[4].rx_nr = end_idx * 2;
	recx_arr[5].rx_idx = 2;
	recx_arr[5].rx_nr = end_idx;
	recx_arr[6].rx_idx = 0;
	recx_arr[6].rx_nr = end_idx * 2;
	recx_arr[7].rx_idx = 0;
	recx_arr[7].rx_nr = end_idx * 2;
	recx_arr[8].rx_idx = 2;
	recx_arr[8].rx_nr = end_idx;
	recx_arr[9].rx_idx = 0;
	recx_arr[9].rx_nr = 3;
	recx_arr[10].rx_idx = end_idx - 1;
	recx_arr[10].rx_nr = end_idx + 1;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = iod_size;
	ds.td_recx_nr = ARRAY_SIZE(recx_arr);
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 0;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = ARRAY_SIZE(recx_arr);
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = ARRAY_SIZE(recx_arr) + 1;
	ds.td_discard = false;
	ds.td_delete = true;

	aggregate_basic(arg, &ds, ARRAY_SIZE(punch_epochs), &punch_epochs[0]);
	cleanup();
}

static void
aggregate_33(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[10];
	daos_epoch_t		 punch_epochs[] = {2, 4, 6, 8, 10, 12};
	int			 iod_size = 1024, end_idx;

	end_idx = (VOS_MW_FLUSH_THRESH + iod_size - 1) / iod_size;
	assert_true(end_idx > 5);

	/* Insert a record */
	recx_arr[0].rx_idx = 0;
	recx_arr[0].rx_nr = 2;
	recx_arr[1].rx_idx = 0;
	recx_arr[1].rx_nr = 2;
	recx_arr[2].rx_idx = 2;
	recx_arr[2].rx_nr = 2;
	recx_arr[3].rx_idx = 1;
	recx_arr[3].rx_nr = 2;
	recx_arr[4].rx_idx = 4;
	recx_arr[4].rx_nr = 2;
	recx_arr[5].rx_idx = 4;
	recx_arr[5].rx_nr = 2;
	recx_arr[6].rx_idx = 6;
	recx_arr[6].rx_nr = 2;
	recx_arr[7].rx_idx = 6;
	recx_arr[7].rx_nr = 2;
	recx_arr[8].rx_idx = 12;
	recx_arr[8].rx_nr = 20;
	recx_arr[9].rx_idx = 12;
	recx_arr[9].rx_nr = 2;

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = iod_size;
	ds.td_recx_nr = ARRAY_SIZE(recx_arr);
	ds.td_recx = &recx_arr[0];
	ds.td_expected_recs = 2;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = ARRAY_SIZE(recx_arr);
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = ARRAY_SIZE(recx_arr) + 1;
	ds.td_discard = false;
	ds.td_delete = true;

	aggregate_basic(arg, &ds, ARRAY_SIZE(punch_epochs), &punch_epochs[0]);
	cleanup();
}

/*
 * Aggregate on single akey-EV, with or w/o 'force_merge' flag
 */
static void
aggregate_34(void **state)
{
	struct io_test_args	*arg = *state;
	vos_pool_info_t		 pool_info;
	struct vos_pool_space	*vps = &pool_info.pif_space;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		*recx_arr;
	int			 rc, i, rec_cnt = vos_agg_nvme_thresh - 1;
	unsigned int		 agg_flags = VOS_AGG_FL_FORCE_SCAN;

	rc = vos_pool_query(arg->ctx.tc_po_hdl, &pool_info);
	assert_rc_equal(rc, 0);

	/* NVMe isn't enabled */
	if (NVME_TOTAL(vps) == 0) {
		print_message("NVMe isn't enabled, skip test\n");
		skip();
	}

	D_ASSERT(rec_cnt > 0);
	D_ALLOC_ARRAY(recx_arr, rec_cnt);
	D_ASSERT(recx_arr != NULL);

	for (i = 0; i < rec_cnt; i++) {
		recx_arr[i].rx_idx = i * VOS_BLK_SZ;
		recx_arr[i].rx_nr = VOS_BLK_SZ;
	}

	ds.td_type = DAOS_IOD_ARRAY;
	ds.td_iod_size = 1;
	ds.td_recx_nr = rec_cnt;
	ds.td_recx = recx_arr;
	ds.td_expected_recs = rec_cnt;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = rec_cnt;
	ds.td_agg_epr.epr_lo = 0;
	ds.td_agg_epr.epr_hi = rec_cnt + 1;
	ds.td_discard = false;

	VERBOSE_MSG("Aggregate NVMe records w/o 'force_merge' flag\n");
	aggregate_basic_lb(arg, &ds, 0, NULL, NULL, agg_flags);

	VERBOSE_MSG("Aggregate NVMe records with 'force_merge' flag\n");
	ds.td_expected_recs = 1;
	agg_flags |= VOS_AGG_FL_FORCE_MERGE;
	aggregate_basic_lb(arg, &ds, 0, NULL, NULL, agg_flags);

	D_FREE(recx_arr);
	cleanup();
}

#define INIT_FEATS 0x8000000000073f43ULL
D_CASSERT((INIT_FEATS & VOS_AGG_TIME_MASK) == 0);

static void
aggregate_35(void **state)
{
	uint64_t	feats = INIT_FEATS;
	daos_epoch_t	epoch;
	bool		result;

	result = vos_feats_agg_time_get(feats, &epoch);
	assert_false(result);

	feats |= VOS_TF_AGG_OPT;
	result = vos_feats_agg_time_get(feats, &epoch);
	assert_true(result);
	assert_int_equal(epoch, 0);

	vos_feats_agg_time_update(252, &feats);
	result = vos_feats_agg_time_get(feats, &epoch);
	assert_true(result);
	assert_int_equal(epoch, 252);
	assert_int_equal(feats & INIT_FEATS, INIT_FEATS);

	/** Set lower time, same answer */
	vos_feats_agg_time_update(242, &feats);
	result = vos_feats_agg_time_get(feats, &epoch);
	assert_true(result);
	assert_int_equal(epoch, 252);
	assert_int_equal(feats & INIT_FEATS, INIT_FEATS);

	/** Set higher time, new answer */
	vos_feats_agg_time_update(342, &feats);
	result = vos_feats_agg_time_get(feats, &epoch);
	assert_true(result);
	assert_int_equal(epoch, 342);
	assert_int_equal(feats & INIT_FEATS, INIT_FEATS);

	/** If upper 24-bits is set, we assume HLC */
	vos_feats_agg_time_update(0x463abcdef00ULL, &feats);
	result = vos_feats_agg_time_get(feats, &epoch);
	assert_true(result);
	/** When we set the timestamp, we round up */
	assert_int_equal(epoch, 0x463ac000000ULL);
	assert_int_equal(feats & INIT_FEATS, INIT_FEATS);

	/** Try setting a lower time, should get same result */
	vos_feats_agg_time_update(0x463a00def00ULL, &feats);
	result = vos_feats_agg_time_get(feats, &epoch);
	assert_true(result);
	assert_int_equal(epoch, 0x463ac000000ULL);
	assert_int_equal(feats & INIT_FEATS, INIT_FEATS);

	/** Try setting a higher time, should get updated */
	vos_feats_agg_time_update(0x463adcdef00ULL, &feats);
	result = vos_feats_agg_time_get(feats, &epoch);
	assert_true(result);
	assert_int_equal(epoch, 0x463ae000000ULL);
	assert_int_equal(feats & INIT_FEATS, INIT_FEATS);

	/** Test maximum epoch (not HLC) */
	feats = INIT_FEATS;
	epoch = (1ULL << VOS_AGG_NR_BITS) - 1;
	vos_feats_agg_time_update(epoch, &feats);
	result = vos_feats_agg_time_get(feats, &epoch);
	assert_true(result);
	assert_int_equal(epoch, (1ULL << VOS_AGG_NR_BITS) - 1);
	assert_false(feats & VOS_TF_AGG_HLC);
	assert_int_equal(feats & INIT_FEATS, INIT_FEATS);

	/** Test minimum HLC */
	epoch = 1ULL << VOS_AGG_NR_BITS;
	vos_feats_agg_time_update(epoch, &feats);
	result = vos_feats_agg_time_get(feats, &epoch);
	assert_true(result);
	/** Already rounded to nearest 1/4 ms */
	assert_int_equal(epoch, 1ULL << VOS_AGG_NR_BITS);
	assert_true(feats & VOS_TF_AGG_HLC);
	assert_int_equal(feats & INIT_FEATS, INIT_FEATS);

	/** Test rounding */
	epoch = (1ULL << VOS_AGG_NR_BITS) + 1;
	vos_feats_agg_time_update(epoch, &feats);
	result = vos_feats_agg_time_get(feats, &epoch);
	assert_true(result);
	assert_int_equal(epoch, (1ULL << VOS_AGG_NR_BITS) + (1ULL << VOS_AGG_NR_HLC_BITS));
	assert_true(feats & VOS_TF_AGG_HLC);
	assert_int_equal(feats & INIT_FEATS, INIT_FEATS);
}

static int
agg_tst_teardown(void **state)
{
	daos_fail_loc_set(0);
	test_args_reset((struct io_test_args *) *state, VPOOL_SIZE);
	return 0;
}

static const struct CMUnitTest discard_tests[] = {
	{ "VOS451: Discard SV with specified epoch",
	  discard_1, NULL, agg_tst_teardown },
	{ "VOS452: Discard SV with confined epr",
	  discard_2, NULL, agg_tst_teardown },
	{ "VOS453: Discard SV with epr [0, DAOS_EPOCH_MAX]",
	  discard_3, NULL, agg_tst_teardown },
	{ "VOS454: Discard SV with punch records",
	  discard_4, NULL, agg_tst_teardown },
	{ "VOS455: Discard SV with random punch, random yield",
	  discard_5, NULL, agg_tst_teardown },
	{ "VOS456: Discard SV, multiple objects, keys",
	  discard_6, NULL, agg_tst_teardown },
	{ "VOS457: Discard EV with specified epoch",
	  discard_7, NULL, agg_tst_teardown },
	{ "VOS458: Discard EV with confined epr",
	  discard_8, NULL, agg_tst_teardown },
	{ "VOS459: Discard EV with epr [0, DAOS_EPOCH_MAX]",
	  discard_9, NULL, agg_tst_teardown },
	{ "VOS460: Discard EV with punch records",
	  discard_10, NULL, agg_tst_teardown },
	{ "VOS461: Discard EV with random punch, random yield",
	  discard_11, NULL, agg_tst_teardown },
	{ "VOS462: Discard EV, multiple objects, keys",
	  discard_12, NULL, agg_tst_teardown },
	{ "VOS463: Discard won't run into infinite loop",
	  discard_13, NULL, agg_tst_teardown },
	{ "VOS464: Discard object/key punches sv",
	  discard_14, NULL, agg_tst_teardown },
	{ "VOS465: Discard object/key punches array",
	  discard_15, NULL, agg_tst_teardown },
	{ "VOS466: Object specific discard (empty obj)",
	  discard_16, NULL, agg_tst_teardown },
	{ "VOS467: Object specific discard (non-empty obj)",
	  discard_17, NULL, agg_tst_teardown },
};

static const struct CMUnitTest aggregate_tests[] = {
	{ "VOS401: Aggregate SV with confined epr",
	  aggregate_1, NULL, agg_tst_teardown },
	{ "VOS402: Aggregate SV with punch records",
	  aggregate_2, NULL, agg_tst_teardown },
	{ "VOS403: Aggregate SV with random punch, random yield",
	  aggregate_3, NULL, agg_tst_teardown },
	{ "VOS404: Aggregate SV, multiple objects, keys",
	  aggregate_4, NULL, agg_tst_teardown },
	{ "VOS405: Aggregate EV, single record",
	  aggregate_5, NULL, agg_tst_teardown },
	{ "VOS406: Aggregate EV, disjoint records",
	  aggregate_6, NULL, agg_tst_teardown },
	{ "VOS407: Aggregate EV, adjacent records",
	  aggregate_7, NULL, agg_tst_teardown },
	{ "VOS408: Aggregate EV, overlapped records",
	  aggregate_8, NULL, agg_tst_teardown },
	{ "VOS409: Aggregate EV, fully covered records",
	  aggregate_9, NULL, agg_tst_teardown },
	{ "VOS410: Aggregate EV, records spanning window end",
	  aggregate_10, NULL, agg_tst_teardown },
	{ "VOS411: Aggregate EV with random punch, random yield",
	  aggregate_11, NULL, agg_tst_teardown },
	{ "VOS412: Aggregate EV with random punch, small flush threshold",
	  aggregate_12, NULL, agg_tst_teardown },
	{ "VOS413: Aggregate EV, multiple objects, keys",
	  aggregate_13, NULL, agg_tst_teardown },
	{ "VOS414: Update and Aggregate EV repeatedly",
	  aggregate_14, NULL, agg_tst_teardown },
	{ "VOS415: Aggregate many object/key punches sv",
	  aggregate_15, NULL, agg_tst_teardown },
	{ "VOS416: Aggregate many object/key punches array",
	  aggregate_16, NULL, agg_tst_teardown },
	{ "VOS417: Aggregate EV, disjoint records, csum",
	  aggregate_17, NULL, agg_tst_teardown },
	{ "VOS418: Aggregate EV, fully covered records, csum",
	  aggregate_18, NULL, agg_tst_teardown },
	{ "VOS419: Aggregate EV, records spanning window end, csum",
	  aggregate_19, NULL, agg_tst_teardown },
	{ "VOS420: Aggregate EV with random punch, random yield, csum",
	  aggregate_20, NULL, agg_tst_teardown },
	{ "VOS421: Aggregate EV with random punch, small flush threshold, csum",
	  aggregate_21, NULL, agg_tst_teardown },
	{ "VOS422: Conditional fetch before and after aggregation is same",
	  aggregate_22, NULL, agg_tst_teardown },
	{ "VOS423: Aggregate deleted records spanning window end",
	  aggregate_23, NULL, agg_tst_teardown },
	{ "VOS424: Aggregate extents not fully covered by delete record",
	  aggregate_24, NULL, agg_tst_teardown },
	{ "VOS425: Aggregate delete of end of merge window",
	  aggregate_25, NULL, agg_tst_teardown },
	{ "VOS426: Consecutive removed extents",
	  aggregate_26, NULL, agg_tst_teardown },
	{ "VOS427: Consecutive removed extents, no logical extents",
	  aggregate_27, NULL, agg_tst_teardown },
	{ "VOS428: Logical extent followed by consecutive removed extents",
	  aggregate_28, NULL, agg_tst_teardown },
	{ "VOS429: Logical extent followed by disjoint removed extents",
	  aggregate_29, NULL, agg_tst_teardown },
	{ "VOS430: Removal stress test",
	  aggregate_30, NULL, agg_tst_teardown },
	{ "VOS431: Removal spans windows, flush with no physical records",
	  aggregate_31, NULL, agg_tst_teardown },
	{ "VOS432: Overlapping removals",
	  aggregate_32, NULL, agg_tst_teardown },
	{ "VOS433: Many small removals",
	  aggregate_33, NULL, agg_tst_teardown },
	{ "VOS434: Selectively merging NVMe records",
	  aggregate_34, NULL, agg_tst_teardown },
	{ "VOS435: Test aggregation timestamp functions",
	  aggregate_35, NULL, NULL },
};

int
run_discard_tests(const char *cfg)
{
	char	test_name[DTS_CFG_MAX];

	dts_create_config(test_name, "Discard Tests %s", cfg);
	return cmocka_run_group_tests_name(test_name, discard_tests,
					   setup_io, teardown_io);
}

int
run_aggregate_tests(bool slow, const char *cfg)
{
	char	test_name[DTS_CFG_MAX];

	dts_create_config(test_name, "Aggregate Tests (%s) %s", cfg, slow ? "slow" : "fast");

	slow_test = slow;
	return cmocka_run_group_tests_name(test_name,
					   aggregate_tests, setup_io,
					   teardown_io);
}
