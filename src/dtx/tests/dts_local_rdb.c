/**
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Local transactions tests
 */
#define D_LOGFAC DD_FAC(tests)

#include <stddef.h>
#include <stdbool.h>
#include <uuid/uuid.h>
#include <daos_types.h>
#include <daos/object.h>

#include "vts_io.h"
#include "dts_utils.h"
#include "dtx_internal.h"

#define SKIP_MINOR_EPOCHS_FORMAT "- skip %" PRIu16 " minor epochs"

static void
ut_rdb_mc(void **state)
{
	struct io_test_args   *arg = *state;
	daos_handle_t          coh = arg->ctx.tc_co_hdl;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	struct dtx_handle     *dth         = NULL;
	const char            *test_data_1 = "Hello";
	const char            *test_data_2 = "Bye";

	/** Assuming the same major epoch. Overwriting me2 > me1 is normal. Since the second
	 * transaction will use an unrelated minor epoch the tricky part is when me2 < me1. */
	const uint16_t         minor_e1 = 100;
	const uint16_t         minor_e2 = 50;

	dts_print_start_message();

	dth             = dts_local_begin(arg->ctx.tc_po_hdl, DTX_SUB_MOD_MAX);
	dth->dth_op_seq = minor_e1;
	DTS_PRINT(SKIP_MINOR_EPOCHS_FORMAT, dth->dth_op_seq);
	dts_update(coh, la, DKEY_ID0, test_data_1, dth);
	dts_local_commit(dth);
	DTS_FETCH_EXISTING(coh, la, DKEY_ID0, test_data_1);

	/** In general re-using the same epoch by consecutive local transactions is discouraged e.g.
	 * in this case, attempting to punch already existing value will result in undefined
	 * behavior. Updating already existing values at already used epoch may have also undefined
	 * consequences in regard to snapshotting and aggregation.
	 */

	dth             = dts_local_begin(arg->ctx.tc_po_hdl, DTX_SUB_MOD_MAX);
	dth->dth_op_seq = minor_e2;
	DTS_PRINT(SKIP_MINOR_EPOCHS_FORMAT, dth->dth_op_seq);
	dts_update(coh, la, DKEY_ID0, test_data_2, dth);
	dts_local_commit(dth);
	DTS_FETCH_EXISTING(coh, la, DKEY_ID0, test_data_2);
}

static void
ut_rdb_lc_discard(void **state)
{
	struct io_test_args   *arg = *state;
	daos_handle_t          coh = arg->ctx.tc_co_hdl;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	struct dtx_handle     *dth         = NULL;
	const char            *test_data_1 = "Hello";
	const char            *test_data_2 = "Bye";
	daos_epoch_t           start_epoch = la->epoch;
	daos_epoch_range_t     epr;
	int                    rc;

	dts_print_start_message();

	/* populate two epochs */
	dth = dts_local_begin(arg->ctx.tc_po_hdl, DTX_SUB_MOD_MAX);
	dts_update(coh, la, DKEY_ID0, test_data_1, dth);
	++la->epoch;
	dts_update(coh, la, DKEY_ID0, test_data_2, dth);
	dts_local_commit(dth);
	DTS_FETCH_EXISTING(coh, la, DKEY_ID0, test_data_2);

	/* discard these epochs */
	DTS_PRINT("- discard epoch(s) range <%" PRIu64 ", %" PRIu64 "> (rc=%d)", start_epoch,
		  la->epoch, 0);
	epr.epr_lo = start_epoch;
	epr.epr_hi = la->epoch;
	rc         = vos_discard(coh, NULL /* oidp */, &epr, NULL, NULL);
	assert_rc_equal(rc, 0);
	DTS_FETCH_NON_EXISTING(coh, la, DKEY_ID0);

	/* re-populate discarded epochs with different data */
	la->epoch = start_epoch;
	dth       = dts_local_begin(arg->ctx.tc_po_hdl, DTX_SUB_MOD_MAX);
	dts_update(coh, la, DKEY_ID0, test_data_2, dth);
	++la->epoch;
	dts_update(coh, la, DKEY_ID0, test_data_1, dth);
	dts_local_commit(dth);
	DTS_FETCH_EXISTING(coh, la, DKEY_ID0, test_data_1);
}

static const struct CMUnitTest tests_all[] = {
    BASIC_UT(200, "RDB's MC update scheme", ut_rdb_mc),
    BASIC_UT(201, "RDB's LC update/discard/update", ut_rdb_lc_discard),
};

int
run_local_rdb_tests(const char *cfg)
{
	const char *test_name = "Local transaction - RDB use cases";

	dts_global_init();

	return cmocka_run_group_tests_name(test_name, tests_all, setup_io, teardown_io);
}
