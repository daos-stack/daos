/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

#include <daos/common.h>
#include <daos/placement.h>
#include <daos.h>
#include <daos/object.h>
#include "place_obj_common.h"
/* Gain some internal knowledge of pool server */
#include "../../pool/rpc.h"
#include "../../pool/srv_pool_map.h"

static void
base_pda_test(void **state)
{
	struct pool_map		*po_map;
	struct pl_map		*pl_map;

	/* --------------------------------------------------------- */
	print_message("\nWith 2 domains, 2 nodes each, 2 targets each = 8 targets\n");
	gen_maps(1, 2, 2, 2, &po_map, &pl_map);
	/* even though it's 8 total, still need a domain for each replica */
	assert_invalid_param(pl_map, OC_RP_4G2, 0);

	free_pool_and_placement_map(po_map, pl_map);

	/* --------------------------------------------------------- */
	print_message("\nWith 4 PDs, 4 domains each PD, 2 nodes each domain, "
		      "8 targets each node = 256 targets\n");
	gen_maps(4, 4, 2, 8, &po_map, &pl_map);
	print_message("place OC_RP_4G2 pda 3\n");
	assert_placement_success_print(pl_map, OC_RP_4G2, 3);
	print_message("place OC_EC_4P2G2 pda 1\n");
	assert_placement_success_print(pl_map, OC_EC_4P2G2, 1);
	print_message("place OC_EC8P2G2 pda 4\n");
	assert_placement_success_print(pl_map, OC_EC_8P2G2, 4);
	print_message("place OC_EC16P2G8 pda 3, need 18 domains will fail\n");
	assert_invalid_param(pl_map, OC_EC_16P2G8, 3);

	free_pool_and_placement_map(po_map, pl_map);

	/* The End */
}

/*
 * ------------------------------------------------
 * End Test Cases
 * ------------------------------------------------
 */

static int
placement_test_setup(void **state)
{
	assert_success(obj_class_init());

	return pl_init();
}

static int
placement_test_teardown(void **state)
{
	pl_fini();
	obj_class_fini();

	return 0;
}

#define T(dsc, test) { "PLACEMENT "STR(__COUNTER__)" ("#test"): " dsc, test, \
			  placement_test_setup, placement_test_teardown }

static const struct CMUnitTest pda_tests[] = {
	/* Standard configurations */
	T("Base PDA test", base_pda_test),
};

int
pda_tests_run(bool verbose)
{
	int rc = 0;

	g_verbose = verbose;

	rc += cmocka_run_group_tests_name("Jump Map Placement PDA Tests", pda_tests,
					  NULL, NULL);

	return rc;
}
