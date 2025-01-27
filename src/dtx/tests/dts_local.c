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

/** Setup and teardown functions */

static const char *pre_test_data = "Aloha";

/**
 * Fill up the container with some data.
 */
static int
setup_warm(void **state)
{
	setup_local_args(state);

	struct io_test_args   *arg = *state;
	daos_handle_t          coh = arg->ctx.tc_co_hdl;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	DTS_PRINT("Warmup:");

	dts_update(coh, la, DKEY_ID2, pre_test_data, NULL);

	DTS_FETCH_EXISTING(coh, la, DKEY_ID2, pre_test_data);
	DTS_FETCH_NON_EXISTING(coh, la, DKEY_ID3);

	return 0;
}

static int
teardown_warm(void **state)
{
	struct io_test_args   *arg = *state;
	daos_handle_t          coh = arg->ctx.tc_co_hdl;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	DTS_PRINT("Cleanup:");

	DTS_FETCH_EXISTING(coh, la, DKEY_ID2, pre_test_data);

	teardown_local_args(state);

	return 0;
}

/** Tests */

#define ABORT_ROUND  0
#define COMMIT_ROUND 1
#define MAX_ROUND    2

static void
ut_local_transaction(void **state)
{
	struct io_test_args   *arg = *state;
	daos_handle_t          coh = arg->ctx.tc_co_hdl;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	struct dtx_handle     *dth       = NULL;
	const char            *test_data = "Hello";

	dts_print_start_message();

	for (int i = 0; i < MAX_ROUND; i++) {
		dth = dts_local_begin(arg->ctx.tc_po_hdl, DTX_SUB_MOD_MAX);

		dts_update(coh, la, DKEY_ID0, test_data, dth);
		dts_update(coh, la, DKEY_ID1, test_data, dth);
		++la->epoch;
		dts_punch_dkey(coh, la, DKEY_ID1, dth);

		if (i == ABORT_ROUND) {
			/** On abort, both values are expected to be non-existing. */
			dts_local_abort(dth);
			DTS_FETCH_NON_EXISTING(coh, la, DKEY_ID0);
			DTS_FETCH_NON_EXISTING(coh, la, DKEY_ID1);
		} else { /** COMMIT_ROUND */
			/** On commit, only the non-punched value is expected to exists. */
			dts_local_commit(dth);
			DTS_FETCH_EXISTING(coh, la, DKEY_ID0, test_data);
			DTS_FETCH_NON_EXISTING(coh, la, DKEY_ID1);
		}

		++la->epoch;
	}
}

#define OIDS_NUM 100

static void
ut_big_local_transaction(void **state)
{
	struct io_test_args   *arg = *state;
	daos_handle_t          coh = arg->ctx.tc_co_hdl;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	int                    rc = 0;
	char                   buf[BUF_SIZE];
	struct dtx_handle     *dth       = NULL;
	const char            *test_data = "Hello";

	daos_key_t            *dkey = &la->dkey[0];

	/** prepare OIDs */
	daos_unit_oid_t        oids[OIDS_NUM];
	for (int i = 0; i < OIDS_NUM; ++i) {
		oids[i] = gen_oid(arg->otype);
	}

	dts_print_start_message();

	for (int r = 0; r < MAX_ROUND; r++) {
		dth = dts_local_begin(arg->ctx.tc_po_hdl, DTX_SUB_MOD_MAX);

		DTS_PRINT("- insert at all %d OIDs", OIDS_NUM);
		la->iod.iod_size = strlen(test_data);
		d_iov_set(&la->sgl.sg_iovs[0], (void *)test_data, la->iod.iod_size);
		for (int i = 0; i < OIDS_NUM; ++i) {
			rc = vos_obj_update_ex(coh, oids[i], la->epoch, 0, 0, dkey, 1, &la->iod,
					       NULL, &la->sgl, dth);
			assert_rc_equal(rc, 0);
		}

		if (r == ABORT_ROUND) {
			dts_local_abort(dth);
			DTS_PRINT("- " FETCH_NON_EXISTING_STR);
		} else { /** COMMIT_ROUND */
			dts_local_commit(dth);
			DTS_PRINT("- " FETCH_EXISTING_STR);
		}

		for (int i = 0; i < OIDS_NUM; ++i) {
			d_iov_set(&la->fetch_sgl.sg_iovs[0], (void *)buf, BUF_SIZE);
			dts_fetch(coh, oids[i], la->epoch, dkey, &la->iod, &la->fetch_sgl);

			if (r == ABORT_ROUND) {
				dts_validate(&la->iod, &la->fetch_sgl, NULL, NON_EXISTING);
			} else { /** COMMIT_ROUND */
				dts_validate(&la->iod, &la->fetch_sgl, test_data, EXISTING);
			}
		}
	}
}

#undef OIDS_NUM

static void
ut_too_many_submodifications(void **state)
{
	struct io_test_args   *arg = *state;
	daos_handle_t          coh = arg->ctx.tc_co_hdl;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	int                    rc        = 0;
	struct dtx_handle     *dth       = NULL;
	const char            *test_data = "Hello";
	daos_key_t            *dkey1     = &la->dkey[DKEY_ID1];

	dts_print_start_message();

	dth = dts_local_begin(arg->ctx.tc_po_hdl, 0 /** sub_modification_cnt */);

	/** there is always a single inline slot available */
	dts_update(coh, la, DKEY_ID0, test_data, dth);

	/** there should not be available slot to record another operation */
	DTS_PRINT(UPDATE_FORMAT, DKEY_ID1, la->epoch, -DER_NOMEM);
	rc = vos_obj_update_ex(coh, la->oid, la->epoch, 0, 0, dkey1, 1, &la->iod, NULL, &la->sgl,
			       dth);
	assert_rc_equal(rc, -DER_NOMEM);

	/** When an operation in a transaction fail the whole transaction has to be aborted. */
	dts_local_abort(dth);

	DTS_FETCH_NON_EXISTING(coh, la, DKEY_ID0);
	DTS_FETCH_NON_EXISTING(coh, la, DKEY_ID1);
}

static void
ut_overlapping(void **state)
{
	struct io_test_args   *arg = *state;
	daos_handle_t          coh = arg->ctx.tc_co_hdl;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	struct dtx_handle     *dth         = NULL;
	const char            *test_data_1 = "Hello";
	const char            *test_data_2 = "Bye";

	dts_print_start_message();

	/** Overlapping operations affecting the existence of an entity are allowed as long as they
	 * take place in the same transactions.
	 * - overlapping operations - targeting the same major epoch and the same entity
	 * - entity - fully described by OID + DKEY + AKEY path
	 *
	 * The overwrites tested here are possible by leveraging the minor epoch comparison. Each
	 * next operation in the local transaction belongs to next minor epoch. So an operation that
	 * happens later in the transaction can overwrite what has happened earlier in the
	 * transaction.
	 *
	 * Note: Punch operations targeting just an OID or DKEY/AKEY affect all entities that have
	 * them specified on their OID + DKEY + AKEY path.
	 *
	 * Note: Updating already existing value or punching already punched value does not affect
	 * the entity's existence.
	 * */
	dth = dts_local_begin(arg->ctx.tc_po_hdl, DTX_SUB_MOD_MAX);

	dts_update(coh, la, DKEY_ID0, test_data_1, dth);
	dts_update(coh, la, DKEY_ID0, test_data_2, dth);

	dts_update(coh, la, DKEY_ID1, test_data_1, dth);
	dts_punch_dkey(coh, la, DKEY_ID1, dth);

	dts_punch_dkey(coh, la, DKEY_ID2, dth);
	dts_update(coh, la, DKEY_ID2, test_data_1, dth);

	dts_punch_dkey(coh, la, DKEY_ID3, dth);
	dts_punch_dkey(coh, la, DKEY_ID3, dth);

	dts_local_commit(dth);

	DTS_FETCH_EXISTING(coh, la, DKEY_ID0, test_data_2);
	DTS_FETCH_NON_EXISTING(coh, la, DKEY_ID1);
	DTS_FETCH_EXISTING(coh, la, DKEY_ID2, test_data_1);
	DTS_FETCH_NON_EXISTING(coh, la, DKEY_ID3);
}

#define WARM_UT(NO, NAME, FUNC)                                                                    \
	{                                                                                          \
		"DTX" #NO ": " NAME, FUNC, setup_warm, teardown_warm                               \
	}

static const struct CMUnitTest local_tests_all[] = {
    BASIC_UT(100, "Simple local transaction", ut_local_transaction),
    WARM_UT(101, "Simple local transaction with pre-existing data", ut_local_transaction),
    WARM_UT(102, "Big local transaction", ut_big_local_transaction),
    BASIC_UT(103, "Too many submodifications", ut_too_many_submodifications),
    BASIC_UT(104, "Overlapping updates", ut_overlapping),
};

int
run_local_tests(const char *cfg)
{
	const char *test_name = "Local transaction";

	dts_global_init();

	return cmocka_run_group_tests_name(test_name, local_tests_all, setup_io, teardown_io);
}
