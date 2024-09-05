/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * XXX
 */
#define D_LOGFAC DD_FAC(tests)

#include <stddef.h>
#include <stdbool.h>
#include <uuid/uuid.h>
#include <daos_types.h>
#include <daos/object.h>

#include "vts_io.h"
#include "dts_utils.h"

/* Please see dtx_is_valid_handle() for details. */
#define MINIMAL_VALID_HLC 1
#define MINIMAL_VALID_DTX_ID {.dti_uuid = {0}, .dti_hlc = MINIMAL_VALID_HLC }

/* Please see dtx_epoch_chosen() for details. */
#define MINIMAL_CHOSEN_DTX_EPOCH { 1 }

#define DTX_NUM     128 /* arbitrarily chosen */

/*
 * Each next DTX has one more record starting from one record for the first DTX.
 * So overall number of requiered SVT records is an arithmetic series:
 * S = (a1 + an) * n / 2
 */
#define SVT_REC_NUM ((1 + DTX_NUM) * DTX_NUM / 2)

static void
svt_records_alloc(struct umem_instance *umm, umem_off_t svt_records[])
{
	umem_off_t rec;
	struct vos_irec_df *svt;
	int rc;

	rc = umem_tx_begin(umm, NULL);
	assert_rc_equal(rc, 0);
	for (int i = 0; i < SVT_REC_NUM; ++i) {
		rec            = umem_zalloc(umm, sizeof(struct vos_irec_df));
		svt = umem_off2ptr(umm, umem_off2offset(rec));
		svt->ir_dtx = DTX_LID_RESERVED + 1; /* arbitrarily picked */
		svt_records[i] = rec;
	}
	rc = umem_tx_end(umm, DER_SUCCESS);
	assert_rc_equal(rc, 0);
}

static void
xxx(void **state)
{
	struct io_test_args   *arg = *state;
	struct dtx_id	 dti = MINIMAL_VALID_DTX_ID;
	struct dtx_epoch epoch = MINIMAL_CHOSEN_DTX_EPOCH;
	uint16_t sub_modification_cnt = 1;
	uint32_t pm_ver = 0;
	daos_unit_oid_t *leader_oid = &arg->oid;
	struct dtx_id *dti_cos = NULL;
	int dti_cos_cnt = 0;
	uint32_t flags = 0;
	struct dtx_memberships *mbs = NULL;
	struct dtx_handle *dth = NULL;
	struct ds_cont_child *cont = NULL;
	struct vos_container *vc = NULL;
	struct umem_instance *umm = NULL;
	uint32_t tx_id;
	umem_off_t svt_records[SVT_REC_NUM] = {UMOFF_NULL};
	struct vos_irec_df *svt;
	int rc;

	/* prepare SVT records */
	vc = vos_hdl2cont(arg->ctx.tc_co_hdl);
	umm = vos_pool2umm(vc->vc_pool);
	svt_records_alloc(umm, svt_records);

	for (int i = 0, r = 0; i < DTX_NUM; ++i, ++dti.dti_hlc, ++epoch.oe_value) {
		/* begin a DTX transaction */
		rc = dtx_begin(arg->ctx.tc_co_hdl, &dti, &epoch,
			sub_modification_cnt, pm_ver, leader_oid, dti_cos,
			dti_cos_cnt, flags, mbs, &dth);
		assert_rc_equal(rc, 0);
		/* begin the associated VOS transaction */
		rc = vos_tx_begin(dth, umm, false);
		assert_rc_equal(rc, 0);
		/* begin the first sub-modification */
		rc = dtx_sub_init(dth, leader_oid, 0);
		assert_rc_equal(rc, 0);
		/* register a few records */
		for (int j = 0; j <= i; ++j, ++r) {
			rc = vos_dtx_register_record(umm, svt_records[r], DTX_RT_SVT, &tx_id);
			assert_rc_equal(rc, 0);
		}
		/* end both VOS and DTX transactions */
		rc = vos_tx_end(vc, dth, NULL, NULL, false, NULL, DER_SUCCESS);
		assert_rc_equal(rc, 0);
		rc = dtx_end(dth, cont, DER_SUCCESS);
		assert_rc_equal(rc, 0);
	}

	/* commit every other DTX transaction */
	struct dtx_id dtis[DTX_NUM / 2] = {0};
	for (int i = 0; i < DTX_NUM / 2; ++i) {
		dtis[i].dti_hlc = MINIMAL_VALID_HLC + i * 2;
	}
	/* this API call returns the number of committed transactions */
	rc = vos_dtx_commit(arg->ctx.tc_co_hdl, dtis, DTX_NUM / 2, NULL);
	assert_rc_equal(rc, DTX_NUM / 2);

	/* abort every other DTX transaction */
	for (int i = 0; i < DTX_NUM / 2; ++i) {
		dti.dti_hlc = MINIMAL_VALID_HLC + i * 2 + 1;
		rc = vos_dtx_abort(arg->ctx.tc_co_hdl, &dti, DAOS_EPOCH_MAX);
		assert_rc_equal(rc, 0);
	}

	/* assert respective records were either committed or aborted */
	for (int i = 0, r = 0; i < DTX_NUM; ++i) {
		for (int j = 0; j <= i; ++j, ++r) {
			svt = umem_off2ptr(umm, umem_off2offset(svt_records[r]));
			if (i % 2 == 0) {
				assert_rc_equal(svt->ir_dtx, DTX_LID_COMMITTED);
			} else {
				assert_rc_equal(svt->ir_dtx, DTX_LID_ABORTED);
			}
		}
	}
}

static const struct CMUnitTest xxx_tests_all[] = {
    {"DTX400: xxx", xxx, setup_local_args, teardown_local_args},
};

int
run_xxx_tests(const char *cfg)
{
	const char *test_name = "DTX xxx";

	dts_global_init();

	return cmocka_run_group_tests_name(test_name, xxx_tests_all, setup_io, teardown_io);
}
