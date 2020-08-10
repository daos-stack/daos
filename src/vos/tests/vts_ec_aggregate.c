#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <daos_types.h>
#include "vts_io.h"
#include "vos_internal.h"

int ds_obj_ec_aggregate(daos_handle_t ch, daos_epoch_range_t *epr);

static void
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
	iod.iod_recxs = (type == DAOS_IOD_SINGLE) ? NULL : recx;

	if (arg->ta_flags & TF_PUNCH) {
		memset(buf, 0, buf_len);
		iod.iod_size = 0;
	} else if ((arg->ta_flags & TF_USE_VAL) == 0) {
		dts_buf_render(buf, buf_len);
		if (rand() % 2 == 0)
			arg->ta_flags |= TF_ZERO_COPY;
	}

	rc = io_test_obj_update(arg, epoch, flags, &dkey_iov, &iod, &sgl, NULL,
				true);
	assert_int_equal(rc, 0);

	daos_sgl_fini(&sgl, false);
	arg->ta_flags &= ~TF_ZERO_COPY;
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
};

static daos_size_t
ec_get_view_len(struct agg_tst_dataset *ds, daos_recx_t *recx)
{
	daos_size_t	view_len;

	if (ds->td_type == DAOS_IOD_SINGLE) {
		view_len = ds->td_iod_size;
	} else {
		uint64_t	start = UINT64_MAX, end = 0, tmp;
		int		i;

		assert_true(ds->td_recx_nr > 0);
		for (i = 0; i < ds->td_recx_nr; i++) {
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
ec_aggregate_basic(struct io_test_args *arg, struct agg_tst_dataset *ds)
{
	daos_unit_oid_t		 oid;
	char			 dkey[UPDATE_DKEY_SIZE] = { 0 };
	char			 akey[UPDATE_AKEY_SIZE] = { 0 };
	daos_epoch_range_t	*epr_u, *epr_a;
	daos_epoch_t		 epoch;
	char			*buf_u;
	daos_size_t		 view_len;
	daos_recx_t		 recx = { 0 }, *recx_p;
	int			 recx_idx = 0, rc;

	if (daos_unit_oid_is_null(ds->td_oid))
		oid = dts_unit_oid_gen(OC_EC_2P2G1, 0, 0);
	else
		oid = ds->td_oid;
	dts_key_gen(dkey, UPDATE_DKEY_SIZE, UPDATE_DKEY);
	dts_key_gen(akey, UPDATE_AKEY_SIZE, UPDATE_AKEY);

	epr_u = &ds->td_upd_epr;
	epr_a = &ds->td_agg_epr;
	//VERBOSE_MSG("Update epr ["DF_U64", "DF_U64"]\n",
	//	    epr_u->epr_lo, epr_u->epr_hi);

	view_len = ec_get_view_len(ds, &recx);
	D_ALLOC(buf_u, view_len);

	assert_non_null(buf_u);

	for (epoch = epr_u->epr_lo; epoch <= epr_u->epr_hi; epoch++) {
		assert_true(recx_idx < ds->td_recx_nr);
		recx_p = &ds->td_recx[recx_idx];
		recx_idx++;

		update_value(arg, oid, epoch, 0, dkey, akey, ds->td_type,
			     ds->td_iod_size, recx_p, buf_u);
	}
	D_FREE(buf_u);

	rc = ds_obj_ec_aggregate(arg->ctx.tc_co_hdl, epr_a);
	D_PRINT("rc: %d\n", rc);
}

static void
ec_generate_recx(daos_recx_t *recx_tot, daos_recx_t *recx)
{
	uint64_t	max_nr;

	recx->rx_idx = recx_tot->rx_idx + rand() % recx_tot->rx_nr;
	max_nr = recx_tot->rx_idx + recx_tot->rx_nr - recx->rx_idx;
	recx->rx_nr = rand() % max_nr + 1;
}
/*
 * Aggregate on single akey->EV.
 */
static void
ec_aggregate_1(void **state)
{
	struct io_test_args	*arg = *state;
	struct agg_tst_dataset	 ds = { 0 };
	daos_recx_t		 recx_arr[500];
	daos_recx_t		 recx_tot;
	int			 i;

	recx_tot.rx_idx = 0;
	recx_tot.rx_nr = 1000;
	for (i = 0; i < 500; i++)
		ec_generate_recx(&recx_tot, &recx_arr[i]);

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

	//VERBOSE_MSG("Aggregate.\n");

	ec_aggregate_basic(arg, &ds);
}

static const struct CMUnitTest ec_aggregate_tests[] = {
	{ "VOS901: Aggregate full stripe", ec_aggregate_1, NULL, NULL },
};

int
main(int argc, char **argv)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("EC local aggregation",
					 ec_aggregate_tests, setup_io,
					 teardown_io);
	return rc;
}
