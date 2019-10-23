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
 * vos/tests/vts_pm.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include "vts_io.h"
#include "vts_array.h"

#define MAX_ELEM 200
struct pm_info {
	daos_unit_oid_t	pi_oid;
	daos_handle_t	pi_aoh;
	daos_epoch_t	pi_epoch;
	int32_t		pi_fetch_buf[MAX_ELEM];
	int32_t		pi_update_buf[MAX_ELEM];
	int32_t		pi_fill_buf[MAX_ELEM];
};


static int
pm_setup(void **state)
{
	struct io_test_args	*arg = *state;
	struct pm_info		*info;
	int			 rc;

	arg->custom = NULL;

	D_ALLOC_PTR(info);
	if (info == NULL)
		return -DER_NOMEM;

	info->pi_epoch = 1;

	rc = vts_array_alloc(arg->ctx.tc_co_hdl, info->pi_epoch, &info->pi_oid);
	if (rc != 0)
		goto fail;

	rc = vts_array_open(arg->ctx.tc_co_hdl, info->pi_oid, &info->pi_aoh);
	if (rc == 0) {
		arg->custom = info;
		return 0;
	}

	vts_array_free(arg->ctx.tc_co_hdl, info->pi_oid);
fail:
	D_FREE(info);
	return rc;
}

static int
pm_teardown(void **state)
{
	struct io_test_args	*arg = *state;
	struct pm_info		*info = arg->custom;

	vts_array_close(info->pi_aoh);
	vts_array_free(arg->ctx.tc_co_hdl, info->pi_oid);

	return 0;
}

static void
array_set_get_size(void **state)
{
	struct io_test_args	*arg = *state;
	struct pm_info		*info = arg->custom;
	size_t			 size;
	int			 rc;

	rc = vts_array_set_size(info->pi_aoh, 2, MAX_ELEM);
	assert_int_equal(rc, 0);

	rc = vts_array_get_size(info->pi_aoh, 3, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, MAX_ELEM);

	rc = vts_array_set_size(info->pi_aoh, 4, 5);
	assert_int_equal(rc, 0);

	rc = vts_array_get_size(info->pi_aoh, 5, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, 5);

	rc = vts_array_reset(info->pi_aoh, 6, 7);
	assert_int_equal(rc, 0);

	rc = vts_array_get_size(info->pi_aoh, 8, &size);
	assert_int_equal(rc, 0);
	assert_int_equal(size, 0);
}

static void
array_read_write_punch(void **state)
{
	struct io_test_args	*arg = *state;
	struct pm_info		*info = arg->custom;
	daos_epoch_t		 epoch = 2;
	daos_size_t		 size;
	int			 rc;
	int			 i;

	for (i = 0; i < MAX_ELEM; i++)
		info->pi_update_buf[i] = i;
	memset(info->pi_fill_buf, 0xa, sizeof(info->pi_fill_buf));

	for (i = 0; i < 5; i++) {
		rc = vts_array_write(info->pi_aoh, epoch++, 0, MAX_ELEM,
				     info->pi_update_buf);
		assert_int_equal(rc, 0);

		rc = vts_array_get_size(info->pi_aoh, epoch++, &size);
		assert_int_equal(rc, 0);
		assert_int_equal(size, MAX_ELEM);

		memset(info->pi_fetch_buf, 0xa, sizeof(info->pi_fetch_buf));
		rc = vts_array_read(info->pi_aoh, epoch++, 0, MAX_ELEM,
				    info->pi_fetch_buf);
		assert_int_equal(rc, 0);
		assert_memory_equal(info->pi_update_buf, info->pi_fetch_buf,
				    sizeof(info->pi_fetch_buf));

		rc = vts_array_set_size(info->pi_aoh, epoch++, 5);
		assert_int_equal(rc, 0);

		memset(info->pi_fetch_buf, 0xa, sizeof(info->pi_fetch_buf));
		rc = vts_array_read(info->pi_aoh, epoch++, 0, MAX_ELEM,
				    info->pi_fetch_buf);
		assert_int_equal(rc, 0);
		assert_memory_equal(info->pi_update_buf, info->pi_fetch_buf, 5);
		assert_memory_equal(info->pi_fetch_buf + 5, info->pi_fill_buf,
				    MAX_ELEM - 5);

		rc = vts_array_get_size(info->pi_aoh, epoch++, &size);
		assert_int_equal(rc, 0);
		assert_int_equal(size, 5);

		rc = vts_array_reset(info->pi_aoh, epoch, epoch+1);
		assert_int_equal(rc, 0);
		epoch += 2;

		rc = vts_array_get_size(info->pi_aoh, epoch++, &size);
		assert_int_equal(rc, 0);
		assert_int_equal(size, 0);
	}
}

static const struct CMUnitTest punch_model_tests[] = {
	{ "VOS800: VOS punch model array set/get size",
	  array_set_get_size, pm_setup, pm_teardown },
	{ "VOS801: VOS punch model array read/write/punch",
	  array_read_write_punch, pm_setup, pm_teardown },
};

int
run_pm_tests(void)
{
	return cmocka_run_group_tests_name("VOS Punch Model tests",
					   punch_model_tests, setup_io,
					   teardown_io);
}
