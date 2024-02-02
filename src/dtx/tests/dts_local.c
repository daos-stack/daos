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
#include "dtx_internal.h"

#define DKEY_ID0    0
#define DKEY_ID1    1
#define DKEY_ID2    2
#define DKEY_ID3    3

#define DKEY_NUM    4
#define START_EPOCH 5

struct dts_local_args {
	daos_unit_oid_t oid;

	char            dkey_buf[DKEY_NUM][UPDATE_DKEY_SIZE];
	daos_key_t      dkey[DKEY_NUM];

	char            akey_buf[UPDATE_AKEY_SIZE];
	daos_key_t      akey;

	daos_iod_t      iod;

	d_sg_list_t     sgl;
	d_sg_list_t     fetch_sgl;

	daos_epoch_t    epoch;
};

/** Utilities */

#define BUF_SIZE 32

static char invalid_data[BUF_SIZE];

static void
dts_print_start_message()
{
	print_message("Test:\n");
}

static struct dtx_handle *
dts_local_begin(daos_handle_t poh, uint16_t sub_modification_cnt)
{
	struct dtx_handle *dth;

	print_message("- begin local transaction\n");
	int rc = dtx_begin(poh, NULL, NULL, sub_modification_cnt, 0, NULL, NULL, 0, DTX_LOCAL, NULL,
			   &dth);
	assert_rc_equal(rc, 0);
	assert_non_null(dth);

	return dth;
}

static void
dts_local_commit(struct dtx_handle *dth)
{
	print_message("- commit the transaction\n");
	int rc = dtx_end(dth, NULL, 0);
	assert_rc_equal(rc, 0);
}

static void
dts_local_abort(struct dtx_handle *dth)
{
	print_message("- abort the transaction\n");

	int passed_rc = -DER_EXIST;
	int rc        = dtx_end(dth, NULL, passed_rc);
	assert_rc_equal(rc, passed_rc);
}

#define UPDATE_FORMAT "- update at DKEY[%u] epoch=%" PRIu64 " (rc=%d)\n"

static void
dts_update(daos_handle_t coh, struct dts_local_args *la, unsigned dkey_id, const char *value,
	   struct dtx_handle *dth)
{
	print_message(UPDATE_FORMAT, dkey_id, la->epoch, 0);

	la->iod.iod_size = strlen(value);
	d_iov_set(&la->sgl.sg_iovs[0], (void *)value, la->iod.iod_size);

	int rc = vos_obj_update_ex(coh, la->oid, la->epoch, 0, 0, &la->dkey[dkey_id], 1, &la->iod,
				   NULL, &la->sgl, dth);
	assert_rc_equal(rc, 0);
}

static void
dts_punch_dkey(daos_handle_t coh, struct dts_local_args *la, unsigned dkey_id,
	       struct dtx_handle *dth)
{
	print_message("- punch at DKEY[%u] epoch=%" PRIu64 "\n", dkey_id, la->epoch);

	int rc = vos_obj_punch(coh, la->oid, la->epoch, 0, 0, &la->dkey[dkey_id], 0, NULL, dth);
	assert_rc_equal(rc, 0);
}

static void
dts_fetch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch, daos_key_t *dkey,
	  daos_iod_t *iod, d_sg_list_t *sgl)
{
	strncpy(sgl->sg_iovs[0].iov_buf, invalid_data, BUF_SIZE);
	iod->iod_size = UINT64_MAX;
	int rc        = vos_obj_fetch(coh, oid, epoch, 0, dkey, 1, iod, sgl);
	assert_rc_equal(rc, 0);
}

#define EXISTING     true
#define NON_EXISTING false

/**
 * Validate the fetch results.
 *
 * \param[in] exp_buf	The expected contents of the buffer after fetch
 * \param[in] existing	The fetched value was expected to exist (true) or not exist (false)
 */
static void
dts_validate(daos_iod_t *iod, d_sg_list_t *sgl, const char *exp_buf, bool existing)
{
	const char *iov_buf = (char *)sgl->sg_iovs[0].iov_buf;
	int         fetched_size;

	if (existing) {
		fetched_size = (int)strlen(exp_buf);
	} else {
		exp_buf      = invalid_data;
		fetched_size = 0;
	}

	assert_int_equal(iod->iod_size, fetched_size);
	assert_int_equal(sgl->sg_iovs[0].iov_len, fetched_size);
	assert_memory_equal(iov_buf, exp_buf, strlen(exp_buf));

	print_message("buf = %.*s\n", (int)strlen(exp_buf), iov_buf);
}

/**
 * Fetch and validate the result.
 *
 * It is intended to be used via DTS_FETCH_*() macros.
 */
static void
_dts_fetch_and_validate(daos_handle_t coh, struct dts_local_args *la, unsigned dkey_id,
			const char *exp_buf, bool existing, const char *msg)
{
	char buf[BUF_SIZE];

	d_iov_set(&la->fetch_sgl.sg_iovs[0], (void *)buf, BUF_SIZE);

	print_message("- %s at DKEY[%u] epoch=%" PRIu64 "\n", msg, dkey_id, la->epoch);
	dts_fetch(coh, la->oid, la->epoch, &la->dkey[dkey_id], &la->iod, &la->fetch_sgl);

	dts_validate(&la->iod, &la->fetch_sgl, exp_buf, existing);
}

#define FETCH_EXISTING_STR     "fetch existing value(s)"
#define FETCH_NON_EXISTING_STR "fetch non-existing value(s)"

#define DTS_FETCH_EXISTING(coh, la, dkey_id, exp_buf)                                              \
	_dts_fetch_and_validate((coh), (la), (dkey_id), exp_buf, EXISTING, FETCH_EXISTING_STR)

#define DTS_FETCH_NON_EXISTING(coh, la, dkey_id)                                                   \
	_dts_fetch_and_validate((coh), (la), (dkey_id), NULL, NON_EXISTING, FETCH_NON_EXISTING_STR)

/** Setup and teardown functions */

static struct dts_local_args local_args;

static int
setup_local_args(void **state)
{
	struct io_test_args   *arg      = *state;
	struct dts_local_args *la       = &local_args;
	int                    int_flag = is_daos_obj_type_set(arg->otype, DAOS_OT_DKEY_UINT64);
	int                    rc;

	memset(&local_args, 0, sizeof(local_args));

	/** i.a. recreates the container */
	test_args_reset(arg, VPOOL_SIZE);

	/** prepare OID */
	la->oid = gen_oid(arg->otype);

	/** prepare DKEYs */
	for (int i = 0; i < DKEY_NUM; i++) {
		vts_key_gen(&la->dkey_buf[i][0], arg->dkey_size, true, arg);
		set_iov(&la->dkey[i], &la->dkey_buf[i][0], int_flag);
	}

	/** prepare AKEYs */
	vts_key_gen(&la->akey_buf[0], arg->akey_size, true, arg);
	set_iov(&la->akey, &la->akey_buf[0], int_flag);

	/** prepare IO descriptor */
	la->iod.iod_type  = DAOS_IOD_SINGLE;
	la->iod.iod_name  = la->akey;
	la->iod.iod_recxs = NULL;
	la->iod.iod_nr    = 1;

	/** initialize scatter-gather lists */
	rc = d_sgl_init(&la->sgl, 1);
	assert_rc_equal(rc, 0);
	rc = d_sgl_init(&la->fetch_sgl, 1);
	assert_rc_equal(rc, 0);

	la->epoch = START_EPOCH;

	/** attach local arguments */
	arg->custom = la;

	return 0;
}

static int
teardown_local_args(void **state)
{
	struct io_test_args   *arg = *state;
	struct dts_local_args *la  = arg->custom;

	/** finalize scatter-gather lists */
	d_sgl_fini(&la->sgl, false);
	d_sgl_fini(&la->fetch_sgl, false);

	/** detach local arguments */
	arg->custom = NULL;

	return 0;
}

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

	print_message("Warmup:\n");

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

	print_message("Cleanup:\n");

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

		print_message("- insert at all %d OIDs\n", OIDS_NUM);
		la->iod.iod_size = strlen(test_data);
		d_iov_set(&la->sgl.sg_iovs[0], (void *)test_data, la->iod.iod_size);
		for (int i = 0; i < OIDS_NUM; ++i) {
			rc = vos_obj_update_ex(coh, oids[i], la->epoch, 0, 0, dkey, 1, &la->iod,
					       NULL, &la->sgl, dth);
			assert_rc_equal(rc, 0);
		}

		if (r == ABORT_ROUND) {
			dts_local_abort(dth);
			print_message("- " FETCH_NON_EXISTING_STR "\n");
		} else { /** COMMIT_ROUND */
			dts_local_commit(dth);
			print_message("- " FETCH_EXISTING_STR "\n");
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
	print_message(UPDATE_FORMAT, DKEY_ID1, la->epoch, -DER_NOMEM);
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

static void
ut_rdb_mc(void **state)
{
	struct io_test_args   *arg = *state;
	daos_handle_t          coh = arg->ctx.tc_co_hdl;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	struct dtx_handle     *dth         = NULL;
	const char            *test_data_1 = "Hello";
	const char            *test_data_2 = "Bye";

	dts_print_start_message();

	dth = dts_local_begin(arg->ctx.tc_po_hdl, DTX_SUB_MOD_MAX);
	dts_update(coh, la, DKEY_ID0, test_data_1, dth);
	dts_local_commit(dth);
	DTS_FETCH_EXISTING(coh, la, DKEY_ID0, test_data_1);

	/** In general re-using the same epoch by consecutive local transactions is discouraged e.g.
	 * in this case, attempting to punch already existing value will result in undefined
	 * behaviour. Updating already existing values at already used epoch may have also undefined
	 * consequences in regard to snapshotting and aggregation.
	 */

	dth = dts_local_begin(arg->ctx.tc_po_hdl, DTX_SUB_MOD_MAX);
	dts_update(coh, la, DKEY_ID0, test_data_2, dth);
	dts_local_commit(dth);
	DTS_FETCH_EXISTING(coh, la, DKEY_ID0, test_data_2);
}

#define BASIC_UT(NO, NAME, FUNC)                                                                   \
	{                                                                                          \
		"DTX" #NO ": " NAME, FUNC, setup_local_args, teardown_local_args                   \
	}

#define WARM_UT(NO, NAME, FUNC)                                                                    \
	{                                                                                          \
		"DTX" #NO ": " NAME, FUNC, setup_warm, teardown_warm                               \
	}

static const struct CMUnitTest local_tests_all[] = {
    BASIC_UT(100, "Simple local transaction", ut_local_transaction),
    WARM_UT(101, "Simple local transaction with pre-existing data", ut_local_transaction),
    WARM_UT(102, "Big local transaction", ut_big_local_transaction),
    BASIC_UT(103, "Too many  submodifications", ut_too_many_submodifications),
    BASIC_UT(104, "Overlapping updates", ut_overlapping),
    BASIC_UT(105, "RDB's MC update scheme", ut_rdb_mc),
};

int
run_local_tests(const char *cfg)
{
	const char *test_name = "Local transaction";

	/** initialize the global invalid buffer */
	memset(invalid_data, 'x', BUF_SIZE - 1);
	invalid_data[BUF_SIZE - 1] = '\0';

	return cmocka_run_group_tests_name(test_name, local_tests_all, setup_io, teardown_io);
}
