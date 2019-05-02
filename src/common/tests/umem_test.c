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
#define D_LOGFAC	DD_FAC(tests)

#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <uuid/uuid.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include <daos/mem.h>
#include <daos/tests_lib.h>
#include "utest_common.h"

#define POOL_SIZE ((1024 * 1024  * 1024ULL))

struct test_arg {
	struct utest_context	*ta_utx;
	uint64_t		*ta_root;
	char			*ta_pool_name;
};

int
teardown_vmem(void **state)
{
	struct test_arg *arg = *state;
	int		 rc;

	if (arg == NULL) {
		print_message("state not set, likely due to group-setup"
			      " issue\n");
		return 0;
	}

	rc = utest_utx_destroy(arg->ta_utx);

	return rc;
}
int
setup_vmem(void **state)
{
	struct test_arg		*arg = *state;
	int			 rc = 0;

	rc = utest_vmem_create(sizeof(*arg->ta_root), &arg->ta_utx);
	if (rc != 0) {
		perror("Could not create vmem context");
		rc = 1;
		goto failed;
	}

	arg->ta_root = utest_utx2root(arg->ta_utx);

	return 0;
failed:
	return rc;
}


int
teardown_pmem(void **state)
{
	struct test_arg *arg = *state;
	int		 rc;

	if (arg == NULL) {
		print_message("state not set, likely due to group-setup"
			      " issue\n");
		return 0;
	}

	rc = utest_utx_destroy(arg->ta_utx);

	D_FREE(arg->ta_pool_name);

	return rc;
}
int
setup_pmem(void **state)
{
	struct test_arg		*arg = *state;
	static int		 tnum;
	int			 rc = 0;

	D_ASPRINTF(arg->ta_pool_name, "/mnt/daos/umem-test-%d", tnum++);
	if (arg->ta_pool_name == NULL) {
		print_message("Failed to allocate test struct\n");
		return 1;
	}

	rc = utest_pmem_create(arg->ta_pool_name, POOL_SIZE,
			       sizeof(*arg->ta_root), &arg->ta_utx);
	if (rc != 0) {
		perror("Could not create pmem context");
		rc = 1;
		goto failed;
	}

	arg->ta_root = utest_utx2root(arg->ta_utx);

	return 0;
failed:
	D_FREE(arg->ta_pool_name);
	return rc;
}

static int
global_setup(void **state)
{
	struct test_arg	*arg;

	D_ALLOC_PTR(arg);
	if (arg == NULL) {
		print_message("Failed to allocate test struct\n");
		return 1;
	}

	*state = arg;

	return 0;
}

static int
global_teardown(void **state)
{
	struct test_arg	*arg = *state;

	D_FREE(arg);

	return 0;
}

static void
test_invalid_flags(void **state)
{
	struct test_arg		*arg = *state;
	umem_off_t		 umoff = UMOFF_NULL;
	int			 i;

	assert_true(UMOFF_IS_NULL(umoff));

	assert_int_equal(umem_off_get_invalid_flags(umoff), 0);

	for (i = 0; i < UMOFF_MAX_FLAG; i++) {
		umem_off_set_invalid(&umoff, i);
		assert_int_equal(umem_off_get_invalid_flags(umoff), i);
	}

	umoff = UMOFF_NULL;
	assert_int_equal(umem_off_get_invalid_flags(umoff), 0);

	assert_int_equal(utest_alloc_off(arg->ta_utx, &umoff, 4, NULL, NULL),
			 0);
	assert_int_equal(umem_off_get_invalid_flags(umoff), 0);

	assert_int_equal(utest_free_off(arg->ta_utx, umoff), 0);
}

static void
test_alloc(void **state)
{
	struct test_arg		*arg = *state;
	struct umem_instance	*umm = utest_utx2umm(arg->ta_utx);
	int			*value1;
	int			*value2;
	umem_off_t		 umoff = 0;
	umem_id_t		 mmid;
	int			 rc;

	rc = utest_tx_begin(arg->ta_utx);
	if (rc != 0)
		goto done;

	umoff = umem_zalloc_off(umm, 4);
	if (UMOFF_IS_NULL(umoff)) {
		print_message("umoff unexpectedly NULL\n");
		rc = 1;
		goto end;
	}

	mmid = umem_off2id(umm, umoff);
	if (UMMID_IS_NULL(mmid)) {
		print_message("mmid unexpectedly NULL\n");
		rc = 1;
		goto end;
	}

	value1 = umem_off2ptr(umm, umoff);
	value2 = umem_id2ptr(umm, mmid);
	if (value1 != value2) {
		print_message("different values returned for umoff and mmid\n");
		rc = 1;
		goto end;
	}

	if (*value1 != 0) {
		print_message("Bad value for allocated umoff\n");
		rc = 1;
		goto end;
	}

	rc = umem_free(umm, mmid);
end:
	rc = utest_tx_end(arg->ta_utx, rc);
done:
	assert_int_equal(rc, 0);
}

int
main(int argc, char **argv)
{
	static const struct CMUnitTest umem_tests[] = {
		{ "UMEM001: Test invalid flags pmem", test_invalid_flags,
			setup_pmem, teardown_pmem},
		{ "UMEM002: Test invalid flags vmem", test_invalid_flags,
			setup_vmem, teardown_vmem},
		{ "UMEM003: Test alloc pmem", test_alloc,
			setup_pmem, teardown_pmem},
		{ "UMEM004: Test alloc vmem", test_alloc,
			setup_vmem, teardown_vmem},
		{ NULL, NULL, NULL, NULL }
	};

	return cmocka_run_group_tests_name("umem tests", umem_tests,
					   global_setup, global_teardown);
}
