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

#define START_EPOCH 5

static char invalid_data[BUF_SIZE];

void
dts_global_init()
{
	/** initialize the global invalid buffer */
	memset(invalid_data, 'x', BUF_SIZE - 1);
	invalid_data[BUF_SIZE - 1] = '\0';
}

void
dts_print_start_message()
{
	DTS_PRINT("Test:");
}

struct dtx_handle *
dts_local_begin(daos_handle_t poh, uint16_t sub_modification_cnt)
{
	struct dtx_handle *dth;

	DTS_PRINT("- begin local transaction");
	int rc = dtx_begin(poh, NULL, NULL, sub_modification_cnt, 0, NULL, NULL, 0, DTX_LOCAL, NULL,
			   &dth);
	assert_rc_equal(rc, 0);
	assert_non_null(dth);

	return dth;
}

void
dts_local_commit(struct dtx_handle *dth)
{
	DTS_PRINT("- commit the transaction");
	int rc = dtx_end(dth, NULL, 0);
	assert_rc_equal(rc, 0);
}

void
dts_local_abort(struct dtx_handle *dth)
{
	DTS_PRINT("- abort the transaction");

	int passed_rc = -DER_EXIST;
	int rc        = dtx_end(dth, NULL, passed_rc);
	assert_rc_equal(rc, passed_rc);
}

void
dts_update(daos_handle_t coh, struct dts_local_args *la, unsigned dkey_id, const char *value,
	   struct dtx_handle *dth)
{
	DTS_PRINT(UPDATE_FORMAT, dkey_id, la->epoch, 0);

	la->iod.iod_size = strlen(value);
	d_iov_set(&la->sgl.sg_iovs[0], (void *)value, la->iod.iod_size);

	int rc = vos_obj_update_ex(coh, la->oid, la->epoch, 0, 0, &la->dkey[dkey_id], 1, &la->iod,
				   NULL, &la->sgl, dth);
	assert_rc_equal(rc, 0);
}

void
dts_punch_dkey(daos_handle_t coh, struct dts_local_args *la, unsigned dkey_id,
	       struct dtx_handle *dth)
{
	DTS_PRINT("- punch at DKEY[%u] epoch=%" PRIu64, dkey_id, la->epoch);

	int rc = vos_obj_punch(coh, la->oid, la->epoch, 0, 0, &la->dkey[dkey_id], 0, NULL, dth);
	assert_rc_equal(rc, 0);
}

void
dts_fetch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch, daos_key_t *dkey,
	  daos_iod_t *iod, d_sg_list_t *sgl)
{
	strncpy(sgl->sg_iovs[0].iov_buf, invalid_data, BUF_SIZE);
	iod->iod_size = UINT64_MAX;
	int rc        = vos_obj_fetch(coh, oid, epoch, 0, dkey, 1, iod, sgl);
	assert_rc_equal(rc, 0);
}

void
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
}

void
_dts_fetch_and_validate(daos_handle_t coh, struct dts_local_args *la, unsigned dkey_id,
			const char *exp_buf, bool existing, const char *msg)
{
	char buf[BUF_SIZE];

	d_iov_set(&la->fetch_sgl.sg_iovs[0], (void *)buf, BUF_SIZE);

	DTS_PRINT("- %s at DKEY[%u] epoch=%" PRIu64, msg, dkey_id, la->epoch);
	dts_fetch(coh, la->oid, la->epoch, &la->dkey[dkey_id], &la->iod, &la->fetch_sgl);

	dts_validate(&la->iod, &la->fetch_sgl, exp_buf, existing);
}

/** Setup and teardown functions */

static struct dts_local_args local_args;

int
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

int
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
