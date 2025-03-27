/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2020 Hewlett Packard Enterprise Development LP
 */

/* Notes:
 *
 * This test is perfunctory at present. A fuller set of tests is available:
 *
 * virtualize.sh fabtests/unit/fi_eq_test
 *
 * TODO: current implementation does not support wait states.
 */

#include <stdio.h>
#include <stdlib.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>

#include <ofi.h>

#include "cxip.h"
#include "cxip_test_common.h"

TestSuite(eq, .init = cxit_setup_eq, .fini = cxit_teardown_eq,
	  .timeout = CXIT_DEFAULT_TIMEOUT);

/* Test basic CQ creation */
Test(eq, simple)
{
	cxit_create_eq();
	cr_assert(cxit_eq != NULL);
	cxit_destroy_eq();
}

