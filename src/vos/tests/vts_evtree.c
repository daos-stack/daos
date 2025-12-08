/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of vos/tests/
 *
 * vos/tests/vts_evtree.c
 */
#define D_LOGFAC DD_FAC(tests)

#include <daos_srv/evtree.h>

#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "evt_priv.h"

/* values picked arbitrarily where invalid means not as expected by the caller */
#define DTX_LID_VALID   ((uint32_t)123)
#define DTX_LID_INVALID ((uint32_t)DTX_LID_VALID + 1)

static const struct evt_desc invalid_magic = {.dc_magic = (EVT_DESC_MAGIC + 1)};

static const struct evt_desc invalid_dtx_lid = {.dc_magic = EVT_DESC_MAGIC,
						.dc_dtx   = DTX_LID_INVALID};

static const struct evt_desc valid = {.dc_magic = EVT_DESC_MAGIC, .dc_dtx = DTX_LID_VALID};

static void
evt_desc_is_valid_test(void **state)
{
	assert_false(evt_desc_is_valid(NULL, DTX_LID_VALID));
	assert_false(evt_desc_is_valid(&invalid_magic, DTX_LID_VALID));
	assert_false(evt_desc_is_valid(&invalid_dtx_lid, DTX_LID_VALID));
	assert_true(evt_desc_is_valid(&valid, DTX_LID_VALID));
}

static const struct CMUnitTest evtree_tests_all[] = {
    {"VOS1000: evt_desc_is_valid", evt_desc_is_valid_test, NULL, NULL},
};

int
run_evtree_tests(const char *cfg)
{
	char *test_name = "evtree";
	return cmocka_run_group_tests_name(test_name, evtree_tests_all, NULL, NULL);
}
