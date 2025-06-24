/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>
#include "ddb_vos.h"

#define COH_COOKIE    0x1515
#define DTX_ID_PTR    ((struct dtx_id *)0x6367)
#define DISCARDED_PTR ((int *)0x9303)

int
__wrap_vos_dtx_discard_invalid(daos_handle_t coh, struct dtx_id *dti, int *discarded)
{
	assert_int_equal(coh.cookie, COH_COOKIE);
	assert_ptr_equal(dti, DTX_ID_PTR);
	assert_ptr_equal(discarded, DISCARDED_PTR);

	return mock();
}

#define SOME_ERROR (-DER_BAD_CERT)

static void
dtx_act_discard_invalid_test(void **state)
{
	daos_handle_t coh = {.cookie = COH_COOKIE};
	int           rc;

	will_return(__wrap_vos_dtx_discard_invalid, SOME_ERROR);
	rc = dv_dtx_active_entry_discard_invalid(coh, DTX_ID_PTR, DISCARDED_PTR);
	assert_int_equal(rc, SOME_ERROR);

	will_return(__wrap_vos_dtx_discard_invalid, 0);
	rc = dv_dtx_active_entry_discard_invalid(coh, DTX_ID_PTR, DISCARDED_PTR);
	assert_int_equal(rc, 0);
}

#define TEST(x)                                                                                    \
	{                                                                                          \
		#x, x##_test, NULL, NULL                                                           \
	}

const struct CMUnitTest dv_test_cases[] = {
    TEST(dtx_act_discard_invalid),
};

int
ddb_vos_tests_run()
{
	return cmocka_run_group_tests_name("DDB VOS Interface Unit Tests", dv_test_cases, NULL,
					   NULL);
}
