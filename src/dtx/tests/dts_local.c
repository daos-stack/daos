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

static struct dts_local_args local_args;

#define BUF_SIZE 32

static char invalid_data[BUF_SIZE];

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

static void
fetch(daos_handle_t chl, daos_unit_oid_t oid, daos_epoch_t epoch, daos_key_t *dkey, daos_iod_t *iod,
      d_sg_list_t *sgl)
{
	strncpy(sgl->sg_iovs[0].iov_buf, invalid_data, BUF_SIZE);
	iod->iod_size = UINT64_MAX;
	int rc        = vos_obj_fetch(chl, oid, epoch, 0, dkey, 1, iod, sgl);
	assert_rc_equal(rc, 0);
}

/**
 * Validate the fetch results.
 *
 * \param[in] exp_buf	The expected contents of the buffer after fetch
 * \param[in] existing	The fetched value was expected to exist (true) or not exist (false)
 */
static void
validate_fetch(daos_iod_t *iod, d_sg_list_t *sgl, const char *exp_buf, bool existing)
{
	int         fetched_size = existing ? (int)strlen(exp_buf) : 0;
	const char *iov_buf      = (char *)sgl->sg_iovs[0].iov_buf;

	assert_int_equal(iod->iod_size, fetched_size);
	assert_int_equal(sgl->sg_iovs[0].iov_len, fetched_size);
	assert_memory_equal(iov_buf, exp_buf, strlen(exp_buf));

	print_message("buf = %.*s\n", (int)strlen(exp_buf), iov_buf);
}

/**
 * Fill up the container with some data.
 */
static int
warmup(void **state)
{
	setup_local_args(state);

	struct io_test_args   *arg = *state;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	int                    rc = 0;
	char                   buf[BUF_SIZE];
	const char            *pre_test_data = "Aloha";

	daos_key_t            *used_dkey   = &la->dkey[2];
	daos_key_t            *unused_dkey = &la->dkey[3];

	print_message("Warmup:\n");

	print_message("- store initial value\n");
	la->iod.iod_size = strlen(pre_test_data);
	d_iov_set(&la->sgl.sg_iovs[0], (void *)pre_test_data, la->iod.iod_size);
	rc = vos_obj_update(arg->ctx.tc_co_hdl, la->oid, la->epoch++, 0, 0, used_dkey, 1, &la->iod,
			    NULL, &la->sgl);
	assert_rc_equal(rc, 0);

	d_iov_set(&la->fetch_sgl.sg_iovs[0], (void *)buf, BUF_SIZE);

	print_message("- fetch the inserted value\n");
	fetch(arg->ctx.tc_co_hdl, la->oid, la->epoch, used_dkey, &la->iod, &la->fetch_sgl);
	validate_fetch(&la->iod, &la->fetch_sgl, pre_test_data, true);

	print_message("- fetch a non-existing value\n");
	fetch(arg->ctx.tc_co_hdl, la->oid, la->epoch, unused_dkey, &la->iod, &la->fetch_sgl);
	validate_fetch(&la->iod, &la->fetch_sgl, invalid_data, false);

	return 0;
}

static int
cleanup(void **state)
{
	struct io_test_args   *arg = *state;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	char                   buf[BUF_SIZE];
	/** as inserted in the warmup function */
	const char            *pre_test_data = "Aloha";

	daos_key_t            *used_dkey = &la->dkey[2];

	print_message("Cleanup:\n");

	d_iov_set(&la->fetch_sgl.sg_iovs[0], (void *)buf, BUF_SIZE);

	print_message("- fetch the initial value\n");
	fetch(arg->ctx.tc_co_hdl, la->oid, la->epoch, used_dkey, &la->iod, &la->fetch_sgl);
	validate_fetch(&la->iod, &la->fetch_sgl, pre_test_data, true);

	teardown_local_args(state);

	return 0;
}

#define ABORT_ROUND  0
#define COMMIT_ROUND 1

static void
local_transaction(void **state)
{
	struct io_test_args   *arg = *state;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	int                    rc = 0;
	int                    passed_rc;
	char                   buf[BUF_SIZE];
	struct dtx_handle     *dth       = NULL;
	const char            *test_data = "Hello";

	daos_key_t            *insert_dkey           = &la->dkey[0];
	daos_key_t            *insert_and_punch_dkey = &la->dkey[1];

	print_message("Test:\n");

	for (int i = 0; i < 2; i++) {
		print_message("- begin local transaction\n");
		rc = dtx_begin(arg->ctx.tc_po_hdl, NULL, NULL, 256, 0, NULL, NULL, 0, DTX_LOCAL,
			       NULL, &dth);
		assert_rc_equal(rc, 0);
		assert_non_null(dth);

		la->iod.iod_size = strlen(test_data);
		d_iov_set(&la->sgl.sg_iovs[0], (void *)test_data, la->iod.iod_size);

		print_message("- insert at DKEY[0]\n");
		rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, la->oid, la->epoch++, 0, 0, insert_dkey,
				       1, &la->iod, NULL, &la->sgl, dth);
		assert_rc_equal(rc, 0);

		print_message("- insert and punch at DKEY[1]\n");
		rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, la->oid, la->epoch++, 0, 0,
				       insert_and_punch_dkey, 1, &la->iod, NULL, &la->sgl, dth);
		assert_rc_equal(rc, 0);
		rc = vos_obj_punch(arg->ctx.tc_co_hdl, la->oid, la->epoch++, 0, 0,
				   insert_and_punch_dkey, 0, NULL, dth);
		assert_rc_equal(rc, 0);

		if (i == ABORT_ROUND) {
			/** abort the first time */
			print_message("- abort the transaction\n");
			passed_rc = -DER_EXIST;
		} else { /** COMMIT_ROUND */
			/** commit the second time */
			print_message("- commit the transaction\n");
			passed_rc = 0;
		}
		rc = dtx_end(dth, NULL, passed_rc);
		assert_rc_equal(rc, passed_rc);

		d_iov_set(&la->fetch_sgl.sg_iovs[0], (void *)buf, BUF_SIZE);

		print_message("- try fetching the inserted value\n");
		fetch(arg->ctx.tc_co_hdl, la->oid, la->epoch, insert_dkey, &la->iod,
		      &la->fetch_sgl);

		if (i == ABORT_ROUND) {
			validate_fetch(&la->iod, &la->fetch_sgl, invalid_data, false);
		} else { /** COMMIT_ROUND */
			validate_fetch(&la->iod, &la->fetch_sgl, test_data, true);
		}

		print_message("- try fetching the inserted and punched value\n");
		fetch(arg->ctx.tc_co_hdl, la->oid, la->epoch, insert_and_punch_dkey, &la->iod,
		      &la->fetch_sgl);
		validate_fetch(&la->iod, &la->fetch_sgl, invalid_data, false);

		la->epoch++;
	}
}

#define OIDS_NUM 100

static void
big_local_transaction(void **state)
{
	struct io_test_args   *arg = *state;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	int                    rc = 0;
	int                    passed_rc;
	char                   buf[BUF_SIZE];
	struct dtx_handle     *dth       = NULL;
	const char            *test_data = "Hello";

	daos_key_t            *dkey = &la->dkey[0];

	/** prepare OIDs */
	daos_unit_oid_t        oids[OIDS_NUM];
	for (int i = 0; i < OIDS_NUM; ++i) {
		oids[i] = gen_oid(arg->otype);
	}

	print_message("Test:\n");

	print_message("- begin local transaction\n");
	rc =
	    dtx_begin(arg->ctx.tc_po_hdl, NULL, NULL, 256, 0, NULL, NULL, 0, DTX_LOCAL, NULL, &dth);
	assert_rc_equal(rc, 0);
	assert_non_null(dth);

	la->iod.iod_size = strlen(test_data);
	d_iov_set(&la->sgl.sg_iovs[0], (void *)test_data, la->iod.iod_size);

	print_message("- insert at all OIDs\n");
	for (int i = 0; i < OIDS_NUM; ++i) {
		rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, oids[i], la->epoch++, 0, 0, dkey, 1,
				       &la->iod, NULL, &la->sgl, dth);
		assert_rc_equal(rc, 0);
	}

	print_message("- abort the transaction\n");
	passed_rc = -DER_EXIST;
	rc        = dtx_end(dth, NULL, passed_rc);
	assert_rc_equal(rc, passed_rc);

	print_message("- try fetching the inserted values\n");
	for (int i = 0; i < OIDS_NUM; ++i) {
		d_iov_set(&la->fetch_sgl.sg_iovs[0], (void *)buf, BUF_SIZE);
		fetch(arg->ctx.tc_co_hdl, oids[i], la->epoch, dkey, &la->iod, &la->fetch_sgl);

		validate_fetch(&la->iod, &la->fetch_sgl, invalid_data, false);
	}

	la->epoch++;
}

#undef OIDS_NUM

static void
too_complex_transaction(void **state)
{
	struct io_test_args   *arg = *state;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	int                    rc = 0;
	char                   buf[BUF_SIZE];
	struct dtx_handle     *dth       = NULL;
	const char            *test_data = "Hello";
	daos_key_t            *dkey0     = &la->dkey[0];
	daos_key_t            *dkey1     = &la->dkey[1];

	print_message("Test:\n");

	print_message("- begin local transaction\n");
	uint16_t sub_modification_cnt = 0;
	rc = dtx_begin(arg->ctx.tc_po_hdl, NULL, NULL, sub_modification_cnt, 0, NULL, NULL, 0,
		       DTX_LOCAL, NULL, &dth);
	assert_rc_equal(rc, 0);
	assert_non_null(dth);

	la->iod.iod_size = strlen(test_data);
	d_iov_set(&la->sgl.sg_iovs[0], (void *)test_data, la->iod.iod_size);

	/* there is always a single inline slot available */
	print_message("- insert at DKEY[0] (rc=0)\n");
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, la->oid, la->epoch++, 0, 0, dkey0, 1, &la->iod,
			       NULL, &la->sgl, dth);
	assert_rc_equal(rc, 0);

	/* there should not be slot available to record another operation */
	print_message("- insert at DKEY[1] (rc=-DER_NOMEM)\n");
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, la->oid, la->epoch++, 0, 0, dkey1, 1, &la->iod,
			       NULL, &la->sgl, dth);
	assert_rc_equal(rc, -DER_NOMEM);

	print_message("- abort the transaction (rc=-DER_NOMEM)\n");
	rc = dtx_end(dth, NULL, rc);
	assert_rc_equal(rc, -DER_NOMEM);

	d_iov_set(&la->fetch_sgl.sg_iovs[0], (void *)buf, BUF_SIZE);

	print_message("- try fetching the value inserted at DKEY[0]\n");
	fetch(arg->ctx.tc_co_hdl, la->oid, la->epoch, dkey0, &la->iod, &la->fetch_sgl);
	validate_fetch(&la->iod, &la->fetch_sgl, invalid_data, false);

	print_message("- try fetching the value that failed to be inserted at DKEY[1]\n");
	fetch(arg->ctx.tc_co_hdl, la->oid, la->epoch, dkey1, &la->iod, &la->fetch_sgl);
	validate_fetch(&la->iod, &la->fetch_sgl, invalid_data, false);
}

static void
overlapping_updates(void **state)
{
	struct io_test_args   *arg = *state;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	int                    rc = 0;
	char                   buf[BUF_SIZE];

	struct dtx_handle     *dth         = NULL;
	const char            *test_data_1 = "Hello";
	const char            *test_data_2 = "Bye";
	daos_key_t            *dkey        = &la->dkey[0];

	print_message("Test:\n");

	print_message("- begin local transaction\n");
	rc =
	    dtx_begin(arg->ctx.tc_po_hdl, NULL, NULL, 256, 0, NULL, NULL, 0, DTX_LOCAL, NULL, &dth);
	assert_rc_equal(rc, 0);
	assert_non_null(dth);

	la->iod.iod_size = strlen(test_data_1);
	d_iov_set(&la->sgl.sg_iovs[0], (void *)test_data_1, la->iod.iod_size);

	print_message("- insert at DKEY[0] (rc=0)\n");
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, la->oid, la->epoch, 0, 0, dkey, 1, &la->iod,
			       NULL, &la->sgl, dth);
	assert_rc_equal(rc, 0);

	la->iod.iod_size = strlen(test_data_2);
	d_iov_set(&la->sgl.sg_iovs[0], (void *)test_data_2, la->iod.iod_size);

	print_message("- insert at DKEY[0] on the same epoch (rc=0)\n");
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, la->oid, la->epoch++, 0, 0, dkey, 1, &la->iod,
			       NULL, &la->sgl, dth);
	assert_rc_equal(rc, 0);

	print_message("- commit the transaction (rc=0)\n");
	rc = dtx_end(dth, NULL, rc);
	assert_rc_equal(rc, 0);

	d_iov_set(&la->fetch_sgl.sg_iovs[0], (void *)buf, BUF_SIZE);

	print_message("- try fetching the value inserted at DKEY[0]\n");
	fetch(arg->ctx.tc_co_hdl, la->oid, la->epoch, dkey, &la->iod, &la->fetch_sgl);
	validate_fetch(&la->iod, &la->fetch_sgl, test_data_2, true);
}

static void
overlapping_update_and_punch(void **state)
{
	struct io_test_args   *arg = *state;
	struct dts_local_args *la  = (struct dts_local_args *)arg->custom;

	int                    rc = 0;
	char                   buf[BUF_SIZE];

	struct dtx_handle     *dth       = NULL;
	const char            *test_data = "Hello";
	daos_key_t            *dkey      = &la->dkey[0];

	print_message("Test:\n");

	print_message("- begin local transaction\n");
	rc =
	    dtx_begin(arg->ctx.tc_po_hdl, NULL, NULL, 256, 0, NULL, NULL, 0, DTX_LOCAL, NULL, &dth);
	assert_rc_equal(rc, 0);
	assert_non_null(dth);

	la->iod.iod_size = strlen(test_data);
	d_iov_set(&la->sgl.sg_iovs[0], (void *)test_data, la->iod.iod_size);

	print_message("- insert at DKEY[0] (rc=0)\n");
	rc = vos_obj_update_ex(arg->ctx.tc_co_hdl, la->oid, la->epoch, 0, 0, dkey, 1, &la->iod,
			       NULL, &la->sgl, dth);
	assert_rc_equal(rc, 0);

	print_message("- punch DKEY[0] on the same epoch (rc=0)\n");
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, la->oid, la->epoch++, 0, 0, dkey, 1, &la->akey, dth);
	assert_rc_equal(rc, 0);

	print_message("- commit the transaction (rc=0)\n");
	rc = dtx_end(dth, NULL, rc);
	assert_rc_equal(rc, 0);

	d_iov_set(&la->fetch_sgl.sg_iovs[0], (void *)buf, BUF_SIZE);

	print_message("- exptect no value at DKEY[0]\n");
	fetch(arg->ctx.tc_co_hdl, la->oid, la->epoch, dkey, &la->iod, &la->fetch_sgl);
	validate_fetch(&la->iod, &la->fetch_sgl, invalid_data, false);
}

static const struct CMUnitTest local_tests_all[] = {
    {"DTX100: Simple local transaction", local_transaction, setup_local_args, teardown_local_args},
    {"DTX101: Simple local transaction with pre-existing data", local_transaction, warmup, cleanup},
    {"DTX102: Big local transaction", big_local_transaction, warmup, cleanup},
    {"DTX103: Too complex transaction", too_complex_transaction, setup_local_args,
     teardown_local_args},
    {"DTX104: Transaction with an overlapping updates", overlapping_updates, setup_local_args,
     teardown_local_args},
    {"DTX105: Transaction with an overlapping update and punch", overlapping_update_and_punch,
     setup_local_args, teardown_local_args},
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
