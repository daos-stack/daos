/**
 * (C) Copyright 2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of vos/tests/
 *
 * vos/tests/vts_aggregate.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include "vts_io.h"
#include <vos_internal.h>	/* for VOS_BLK_SZ */

#define VERBOSE_MSG(...)			\
{						\
	if (false)				\
		print_message(__VA_ARGS__);	\
}

static void
update_value(struct io_test_args *arg, daos_unit_oid_t oid, daos_epoch_t epoch,
	     char *dkey, char *akey, daos_iod_type_t type, daos_size_t iod_size,
	     daos_recx_t *recx, char *buf)
{
	daos_iod_t	iod = { 0 };
	daos_sg_list_t	sgl = { 0 };
	daos_key_t	dkey_iov, akey_iov;
	daos_size_t	buf_len;
	int		rc;

	assert_true(dkey != NULL && akey != NULL);
	assert_true(strlen(dkey) && strlen(akey));
	assert_true(!(arg->ta_flags & TF_ZERO_COPY));

	arg->oid = oid;
	daos_iov_set(&dkey_iov, dkey, strlen(dkey));
	daos_iov_set(&akey_iov, akey, strlen(akey));

	rc = daos_sgl_init(&sgl, 1);
	assert_int_equal(rc, 0);

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
	iod.iod_recxs = recx;

	if (arg->ta_flags & TF_PUNCH) {
		memset(buf, 0, buf_len);
		iod.iod_size = 0;
	} else {
		dts_buf_render(buf, buf_len);
		if (rand() % 2 == 0)
			arg->ta_flags |= TF_ZERO_COPY;
	}

	rc = io_test_obj_update(arg, epoch, &dkey_iov, &iod, &sgl, true);
	assert_int_equal(rc, 0);

	daos_sgl_fini(&sgl, false);
	arg->ta_flags &= ~TF_ZERO_COPY;
}

static void
fetch_value(struct io_test_args *arg, daos_unit_oid_t oid, daos_epoch_t epoch,
	    char *dkey, char *akey, daos_iod_type_t type, daos_size_t iod_size,
	    daos_recx_t *recx, char *buf)
{
	daos_iod_t	iod = { 0 };
	daos_sg_list_t	sgl = { 0 };
	daos_key_t	dkey_iov, akey_iov;
	daos_size_t	buf_len;
	int		rc;

	assert_true(dkey != NULL && akey != NULL);
	assert_true(strlen(dkey) && strlen(akey));
	assert_true(!(arg->ta_flags & TF_ZERO_COPY));

	arg->oid = oid;
	daos_iov_set(&dkey_iov, dkey, strlen(dkey));
	daos_iov_set(&akey_iov, akey, strlen(akey));

	rc = daos_sgl_init(&sgl, 1);
	assert_int_equal(rc, 0);

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

	rc = io_test_obj_fetch(arg, epoch, &dkey_iov, &iod, &sgl, true);
	assert_int_equal(rc, 0);
	assert_true(iod.iod_size == 0 || iod.iod_size == buf_len);

	daos_sgl_fini(&sgl, false);
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
	daos_iov_set(&dkey_iov, dkey, strlen(dkey));
	daos_iov_set(&akey_iov, akey, strlen(akey));

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

	iter_type = (type == DAOS_IOD_SINGLE) ?
		VOS_ITER_SINGLE : VOS_ITER_RECX;

	rc = vos_iterate(&iter_param, iter_type, false, &anchors,
			 counting_cb, &nr);
	assert_int_equal(rc, 0);

	return nr;
}

static int
lookup_object(struct io_test_args *arg, daos_unit_oid_t oid)
{
	struct vos_obj_df	*obj_df = NULL;
	int			 rc;

	rc = vos_oi_find(vos_hdl2cont(arg->ctx.tc_co_hdl), oid, 1,
			 DAOS_INTENT_DEFAULT, &obj_df);
	return rc;
}

struct agg_tst_dataset {
	daos_iod_type_t			 td_type;
	daos_epoch_range_t		 td_upd_epr;
	daos_epoch_range_t		 td_agg_epr;
	daos_recx_t			*td_recx;
	unsigned int			 td_recx_nr;
	daos_size_t			 td_iod_size;
	char				*td_expected_view;
	int				 td_expected_recs;
	bool				 td_discard;
	bool				 td_gen_recx;
};

static daos_size_t
get_view_len(struct agg_tst_dataset *ds, daos_recx_t *recx)
{
	daos_size_t	view_len;

	if (ds->td_type == DAOS_IOD_SINGLE) {
		view_len = ds->td_iod_size;
	} else {
		assert_true(ds->td_recx_nr > 0);
		recx->rx_idx = ds->td_recx[0].rx_idx;
		recx->rx_nr = ds->td_recx[ds->td_recx_nr - 1].rx_idx +
			      ds->td_recx[ds->td_recx_nr - 1].rx_nr;
		assert_true(recx->rx_nr > recx->rx_idx);
		recx->rx_nr -= recx->rx_idx;

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

	VERBOSE_MSG("Generate logcial view\n");

	epr_u = &ds->td_upd_epr;
	epr_a = &ds->td_agg_epr;
	view_len = get_view_len(ds, &recx);

	/* Setup expected logical view from aggregate/discard epr_hi */
	D_ALLOC(ds->td_expected_view, view_len);
	assert_non_null(ds->td_expected_view);

	/* All updates below discard epr will be discarded */
	if (ds->td_discard && epr_u->epr_lo >= epr_a->epr_lo) {
		memset(ds->td_expected_view, 0, view_len);
		return;
	}

	view_epoch = ds->td_discard ? (epr_a->epr_lo - 1) : epr_a->epr_hi;

	fetch_value(arg, oid, view_epoch, dkey, akey, ds->td_type,
		    ds->td_iod_size, &recx, ds->td_expected_view);
}

static void
verify_view(struct io_test_args *arg, daos_unit_oid_t oid, char *dkey,
	    char *akey, struct agg_tst_dataset *ds)
{
	daos_epoch_range_t	*epr_a;
	char			*buf_f;
	daos_size_t		 view_len;
	daos_recx_t		 recx;
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

	fetch_value(arg, oid, epr_a->epr_hi, dkey, akey, ds->td_type,
		    ds->td_iod_size, &recx, buf_f);

	assert_memory_equal(buf_f, ds->td_expected_view, view_len);

	D_FREE(buf_f);
	D_FREE(ds->td_expected_view);
	ds->td_expected_view = NULL;
}

static void
generate_recx(daos_recx_t *recx_tot, daos_size_t iod_size, daos_recx_t *recx)
{
	assert_true(false); /* TODO */
}

static void
aggregate_basic(struct io_test_args *arg, struct agg_tst_dataset *ds,
		int punch_nr, daos_epoch_t punch_epoch[])
{
	daos_unit_oid_t		 oid;
	char			 dkey[UPDATE_DKEY_SIZE] = { 0 };
	char			 akey[UPDATE_AKEY_SIZE] = { 0 };
	daos_epoch_range_t	*epr_u, *epr_a;
	daos_epoch_t		 epoch;
	char			*buf_u;
	daos_recx_t		 recx = { 0 }, *recx_p;
	daos_size_t		 view_len;
	int			 punch_idx = 0, recx_idx = 0, rc;

	oid = dts_unit_oid_gen(0, 0, 0);
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
		if (punch_nr > 0 && punch_epoch[punch_idx] == epoch) {
			arg->ta_flags |= TF_PUNCH;
			assert_true(punch_idx < punch_nr);
			punch_idx++;
		} else if (punch_nr < 0 && (rand() % 2) == 0) {
			arg->ta_flags |= TF_PUNCH;
		}

		if (ds->td_type == DAOS_IOD_SINGLE) {
			recx_p = NULL;
		} else if (ds->td_gen_recx) {
			assert_true(ds->td_recx_nr == 1);
			recx_p = &recx;
			generate_recx(&ds->td_recx[0], ds->td_iod_size, recx_p);
		} else {
			assert_true(recx_idx < ds->td_recx_nr);
			recx_p = &ds->td_recx[recx_idx];
			recx_idx++;
		}

		update_value(arg, oid, epoch, dkey, akey, ds->td_type,
			     ds->td_iod_size, recx_p, buf_u);
		arg->ta_flags &= ~TF_PUNCH;
	}
	D_FREE(buf_u);

	generate_view(arg, oid, dkey, akey, ds);

	VERBOSE_MSG("%s epr ["DF_U64", "DF_U64"]\n", ds->td_discard ?
		    "Discard" : "Aggregate", epr_a->epr_lo, epr_a->epr_hi);

	if (ds->td_discard)
		rc = vos_discard(arg->ctx.tc_co_hdl, epr_a);
	else
		rc = vos_aggregate(arg->ctx.tc_co_hdl, epr_a);
	assert_int_equal(rc, 0);

	verify_view(arg, oid, dkey, akey, ds);
}

static inline int
get_ds_index(int oid_idx, int dkey_idx, int akey_idx, int nr)
{
	return oid_idx * nr * nr + dkey_idx * nr + akey_idx;
}

static void
multi_view(struct io_test_args *arg, daos_unit_oid_t oids[],
	   char dkeys[][UPDATE_DKEY_SIZE], char akeys[][UPDATE_DKEY_SIZE],
	   int nr, struct agg_tst_dataset *ds_arr, bool verify)
{
	daos_unit_oid_t		 oid;
	char			*dkey, *akey;
	struct agg_tst_dataset	*ds;
	int			 oid_idx, dkey_idx, akey_idx, ds_idx;

	for (oid_idx = 0; oid_idx < nr; oid_idx++) {
		oid = oids[oid_idx];

		for (dkey_idx = 0; dkey_idx < nr; dkey_idx++) {
			dkey = dkeys[dkey_idx];

			for (akey_idx = 0; akey_idx < nr; akey_idx++) {
				akey = akeys[akey_idx];
				ds_idx = get_ds_index(oid_idx, dkey_idx,
						      akey_idx, nr);
				ds = &ds_arr[ds_idx];

				if (verify)
					verify_view(arg, oid, dkey, akey, ds);
				else
					generate_view(arg, oid, dkey, akey, ds);
			}
		}
	}

}

#define AT_OBJ_KEY_NR	3
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
		oids[i] = dts_unit_oid_gen(0, 0, 0);
		dts_key_gen(dkeys[i], UPDATE_DKEY_SIZE, UPDATE_DKEY);
		dts_key_gen(akeys[i], UPDATE_AKEY_SIZE, UPDATE_AKEY);
	}

	ds_nr = AT_OBJ_KEY_NR * AT_OBJ_KEY_NR * AT_OBJ_KEY_NR;
	D_ALLOC_ARRAY(ds_arr, ds_nr);
	assert_non_null(ds_arr);

	for (i = 0; i < ds_nr; i++) {
		ds = &ds_arr[i];
		*ds = *ds_sample;
		memset(&ds->td_upd_epr, 0, sizeof(*epr_u));
	}

	view_len = get_view_len(ds_sample, &recx);
	D_ALLOC(buf_u, view_len);
	assert_non_null(buf_u);

	VERBOSE_MSG("Generate random updates over multiple objs/keys.\n");
	for (epoch = epr_u->epr_lo; epoch < epr_u->epr_hi; epoch++) {
		oid_idx = rand() % AT_OBJ_KEY_NR;
		dkey_idx = rand() % AT_OBJ_KEY_NR;
		akey_idx = rand() % AT_OBJ_KEY_NR;
		ds_idx = get_ds_index(oid_idx, dkey_idx, akey_idx,
				      AT_OBJ_KEY_NR);

		oid = oids[oid_idx];
		dkey = dkeys[dkey_idx];
		akey = akeys[akey_idx];
		ds = &ds_arr[ds_idx];

		if (rand() % 2)
			arg->ta_flags |= TF_PUNCH;

		if (ds->td_type == DAOS_IOD_SINGLE) {
			recx_p = NULL;
		} else {
			assert_true(ds->td_recx_nr == 1);
			recx_p = &recx;
			generate_recx(&ds->td_recx[0], ds->td_iod_size, recx_p);
		}
		/*
		 * Amend update epr, it'll be used to setup correct expected
		 * physical record nr and expected view.
		 */
		if (ds->td_upd_epr.epr_lo == 0)
			ds->td_upd_epr.epr_lo = epoch;
		ds->td_upd_epr.epr_hi = epoch;

		update_value(arg, oid, epoch, dkey, akey, ds->td_type,
			     ds->td_iod_size, recx_p, buf_u);
		arg->ta_flags &= ~TF_PUNCH;

	}
	D_FREE(buf_u);

	/*
	 * Amend expected physical records nr when update epr has no
	 * itersection with aggregate/discard epr.
	 */
	for (i = 0; i < ds_nr; i++) {
		ds = &ds_arr[i];

		if (ds->td_expected_recs <= 0)
			continue;

		if (ds->td_upd_epr.epr_hi < epr_a->epr_lo ||
		    ds->td_upd_epr.epr_lo > epr_a->epr_hi)
			ds->td_expected_recs = 0;
	}

	multi_view(arg, oids, dkeys, akeys, AT_OBJ_KEY_NR, ds_arr, false);

	VERBOSE_MSG("%s multiple objs/keys\n", ds_sample->td_discard ?
		    "Discard" : "Aggregate");

	if (ds_sample->td_discard)
		rc = vos_discard(arg->ctx.tc_co_hdl, epr_a);
	else
		rc = vos_aggregate(arg->ctx.tc_co_hdl, epr_a);
	assert_int_equal(rc, 0);

	multi_view(arg, oids, dkeys, akeys, AT_OBJ_KEY_NR, ds_arr, true);
	D_FREE(ds_arr);
}

#define AT_SV_IOD_SIZE_SMALL	32			/* SCM record */
#define AT_SV_IOD_SIZE_LARGE	(VOS_BLK_SZ + 500)	/* NVMe record */
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
	assert_int_equal(rc, -DER_NONEXIST);
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
	daos_fail_loc_set(0);
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
	ds.td_iod_size = 1024;
	ds.td_recx_nr = 0;
	ds.td_expected_recs = 0;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 1000;
	ds.td_agg_epr.epr_lo = 850;
	ds.td_agg_epr.epr_hi = DAOS_EPOCH_MAX;
	ds.td_discard = true;

	aggregate_multi(arg, &ds);
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
	daos_fail_loc_set(0);
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
	ds.td_iod_size = 1024;
	ds.td_recx_nr = 0;
	ds.td_expected_recs = 1;
	ds.td_upd_epr.epr_lo = 1;
	ds.td_upd_epr.epr_hi = 1000;
	ds.td_agg_epr.epr_lo = 850;
	ds.td_agg_epr.epr_hi = 999;
	ds.td_discard = false;

	aggregate_multi(arg, &ds);
}

static int
agg_tst_teardown(void **state)
{
	test_args_reset((struct io_test_args *) *state, VPOOL_SIZE);
	return 0;
}

static const struct CMUnitTest discard_tests[] = {
	{ "VOS301: Discard SV with specified epoch",
	  discard_1, NULL, agg_tst_teardown },
	{ "VOS302: Discard SV with confined epr",
	  discard_2, NULL, agg_tst_teardown },
	{ "VOS303: Discard SV with epr [0, DAOS_EPOCH_MAX]",
	  discard_3, NULL, agg_tst_teardown },
	{ "VOS304: Discard SV with punch records",
	  discard_4, NULL, agg_tst_teardown },
	{ "VOS305: Discard SV with random punch, random yield",
	  discard_5, NULL, agg_tst_teardown },
	{ "VOS306: Discard SV, multiple objects, keys",
	  discard_6, NULL, agg_tst_teardown },
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
};

int
run_discard_tests(void)
{
	return cmocka_run_group_tests_name("VOS Discard Test", discard_tests,
					   setup_io, teardown_io);
}

int
run_aggregate_tests(void)
{
	return cmocka_run_group_tests_name("VOS Aggregate Test",
					   aggregate_tests, setup_io,
					   teardown_io);
}
