/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of vos/tests/
 *
 * vos/tests/vts_tree.c
 */
#define D_LOGFAC DD_FAC(tests)

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "vos_internal.h"

/* values picked arbitrarily where invalid means not as expected by the caller */
#define DTX_LID_VALID   ((uint32_t)123)
#define DTX_LID_INVALID ((uint32_t)DTX_LID_VALID + 1)

static const struct vos_irec_df invalid_dtx_lid = {.ir_dtx = DTX_LID_INVALID};

static const struct vos_irec_df valid = {.ir_dtx = DTX_LID_VALID};

static void
vos_irec_is_valid_test(void **state)
{
	assert_false(vos_irec_is_valid(NULL, DTX_LID_VALID));
	assert_false(vos_irec_is_valid(&invalid_dtx_lid, DTX_LID_VALID));
	assert_true(vos_irec_is_valid(&valid, DTX_LID_VALID));
}

static const struct CMUnitTest tree_tests_all[] = {
    {"VOS1100: vos_irec_is_valid", vos_irec_is_valid_test, NULL, NULL},
};

int
run_tree_tests(const char *cfg)
{
	char *test_name = "tree";
	return cmocka_run_group_tests_name(test_name, tree_tests_all, NULL, NULL);
}
