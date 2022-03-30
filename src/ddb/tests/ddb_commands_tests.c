/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <gurt/debug.h>
#include <daos/tests_lib.h>
#include <ddb_common.h>
#include <ddb_cmd_options.h>
#include <daos_srv/vos.h>
#include "ddb_cmocka.h"
#include "ddb_test_driver.h"

/*
 * Test that the command line arguments execute the correct tool command with the correct
 * options/arguments for the command. Verification depends on the ability to set fake command
 * functions in a command function table that the program uses.
 */

static uint32_t fake_print_called;
static char fake_print_buffer[1024];
int fake_print(const char *fmt, ...)
{
	va_list args;

	fake_print_called++;
	va_start(args, fmt);
	vsnprintf(fake_print_buffer, ARRAY_SIZE(fake_print_buffer), fmt, args);
	va_end(args);
	if (g_verbose)
		printf("%s", fake_print_buffer);

	return 0;
}

/*
 * -----------------------------------------------
 * Test Functions
 * -----------------------------------------------
 */
static void
test_quit(void **state)
{
	struct ddb_ctx		 ctx = {0};

	/* Quit is really simple and should just indicate to the program context that it's
	 * time to quit
	 */
	assert_success(ddb_run_quit(&ctx));
	assert_true(ctx.dc_should_quit);
}

static void
test_ls(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	struct ddb_ctx		 ctx = {0};
	struct ls_options	 opt = {.recursive = false, .path = ""};
	int			 items_in_tree;

	ctx.dc_poh = tctx->dvt_poh;
	ctx.dc_io_ft.ddb_print_message = fake_print;
	ctx.dc_io_ft.ddb_print_error = fake_print;
	assert_success(ddb_run_ls(&ctx, &opt));

	/* At least each container should be printed */
	assert_true(ARRAY_SIZE(g_uuids) <= fake_print_called);

	/* With recursive set, every item in the tree should be printed */
	opt.recursive = true;
	items_in_tree = ARRAY_SIZE(g_uuids) * ARRAY_SIZE(g_oids) *
			ARRAY_SIZE(g_dkeys) * ARRAY_SIZE(g_akeys);
	fake_print_called = 0;
	assert_success(ddb_run_ls(&ctx, &opt));
	assert_true(items_in_tree <= fake_print_called);

	/* pick a specific oid - each dkey should be printed */
	opt.path = "[0]/[0]";
	opt.recursive = false;
	assert_success(ddb_run_ls(&ctx, &opt));
	assert_true(ARRAY_SIZE(g_dkeys) <= fake_print_called);
}

/*
 * --------------------------------------------------------------
 * End test functions
 * --------------------------------------------------------------
 */

static int
dcv_suit_setup(void **state)
{
	struct dt_vos_pool_ctx *tctx;

	assert_success(ddb_test_setup_vos(state));

	/* test setup creates the pool, but doesn't open it ... leave it open for these tests */
	tctx = *state;
	assert_success(vos_pool_open(tctx->dvt_pmem_file, tctx->dvt_pool_uuid, 0, &tctx->dvt_poh));

	return 0;
}

static int
dcv_suit_teardown(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;

	dvt_delete_all_containers(tctx->dvt_poh);
	assert_success(vos_pool_close(tctx->dvt_poh));
	ddb_teardown_vos(state);

	return 0;
}

#define TEST(dsc, test) { dsc, test, NULL, NULL }
static const struct CMUnitTest tests[] = {
	TEST("01: quit", test_quit),
	TEST("01: ls", test_ls),
};

int
dvc_tests_run()
{
	return cmocka_run_group_tests_name("DDB commands tests", tests,
					   dcv_suit_setup, dcv_suit_teardown);
}
