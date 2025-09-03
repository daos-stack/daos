/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is for incremental reintegration test.
 *
 * tests/suite/daos_inc_reint.c
 *
 *
 */
#define D_LOGFAC DD_FAC(tests)

#include <unistd.h>
#include <sys/wait.h>

#include "daos_test.h"
#include <daos_prop.h>
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

static void
inc_reint_cont_create(test_arg_t *arg, struct test_cont *cont)
{
	struct test_pool *pool = &arg->pool;
	daos_prop_t      *prop;
	daos_handle_t     coh;
	int               rc;

	print_message("IR: creating container ...\n");

	prop = daos_prop_alloc(1);
	assert_non_null(prop);

	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[0].dpe_val  = DAOS_PROP_CO_REDUN_RF1;

	rc = daos_cont_create(pool->poh, &cont->uuid, prop, NULL);
	assert_rc_equal(rc, 0);

	daos_prop_free(prop);
	uuid_unparse_upper(cont->uuid, cont->label);

	rc = daos_cont_open(pool->poh, NULL, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	/* Sleep for a while for cont IV. */
	sleep(5);

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	print_message("IR: created container " DF_UUID "\n", DP_UUID(cont->uuid));
}

static void
inc_reint_cont_destroy(test_arg_t *arg, struct test_cont *cont)
{
	int rc;

	print_message("IR: destroying container " DF_UUID "\n", DP_UUID(cont->uuid));

	rc = daos_cont_destroy(arg->pool.poh, cont->label, 0, NULL);
	assert_rc_equal(rc, 0);

	print_message("IR: destroyed container " DF_UUID "\n", DP_UUID(cont->uuid));
}

static void
inc_reint1(void **state)
{
	test_arg_t       *arg      = *state;
	struct test_pool *pool     = &arg->pool;
	struct test_cont  conts[7] = {0};
	int               rc;
	int               i;

	FAULT_INJECTION_REQUIRED();

	print_message("INC_REINT1: verify container recovery for incremental reintegration\n");

	for (i = 0; i < 5; i++)
		inc_reint_cont_create(arg, &conts[i]);

	rebuild_single_pool_rank(arg, 1, false);

	for (i = 5; i < 7; i++)
		inc_reint_cont_create(arg, &conts[i]);

	for (i = 1; i < 5; i += 2)
		inc_reint_cont_destroy(arg, &conts[i]);

	/*
	 * Now, the left containers are: 0, 2, 4, 5, 6. When we incrementally reintegrate former
	 * excluded rank, it is expected to create cont_5 & cont_6, then destroy cont_1 & cont_3.
	 */
	rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group, 1, -1);
	assert_rc_equal(rc, 0);

	test_verify_cont(arg, pool);
}

static void
inc_reint2(void **state)
{
	test_arg_t       *arg      = *state;
	struct test_pool *pool     = &arg->pool;
	struct test_cont  conts[5] = {0};
	int               rc;
	int               i;

	FAULT_INJECTION_REQUIRED();

	print_message("INC_REINT2: container recovery for empty pool\n");

	for (i = 0; i < 5; i++)
		inc_reint_cont_create(arg, &conts[i]);

	rebuild_single_pool_rank(arg, 1, false);

	for (i = 0; i < 5; i++)
		inc_reint_cont_destroy(arg, &conts[i]);

	rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group, 1, -1);
	assert_rc_equal(rc, 0);

	test_verify_cont(arg, pool);
}

static void
inc_reint3(void **state)
{
	test_arg_t       *arg   = *state;
	struct test_pool *pool  = &arg->pool;
	struct test_cont *conts = NULL;
	int               rc;
	int               i;

	FAULT_INJECTION_REQUIRED();

	print_message("INC_REINT3: container recovery for the pool with 2000+ containers\n");

	D_ALLOC_ARRAY(conts, 2048);
	assert_non_null(conts);

	inc_reint_cont_create(arg, &conts[0]);

	rebuild_single_pool_rank(arg, 1, false);

	for (i = 1; i < 2048; i++)
		inc_reint_cont_create(arg, &conts[i]);

	rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group, 1, -1);
	assert_rc_equal(rc, 0);

	test_verify_cont(arg, pool);
	D_FREE(conts);
}

static void
inc_reint4(void **state)
{
	test_arg_t       *arg      = *state;
	struct test_pool *pool     = &arg->pool;
	struct test_cont  conts[3] = {0};
	pid_t             pid;
	int               rc;
	int               i;

	FAULT_INJECTION_REQUIRED();

	print_message("INC_REINT4: race between container recovery and container create\n");

	for (i = 0; i < 2; i++)
		inc_reint_cont_create(arg, &conts[i]);

	rebuild_single_pool_rank(arg, 1, false);

	rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				   DAOS_REBUILD_REINT_SLOW | DAOS_FAIL_ALWAYS, 0, NULL);
	assert_rc_equal(rc, 0);

	pid = fork();
	assert_true(pid >= 0);

	if (pid == 0) {
		sleep(2);
		inc_reint_cont_create(arg, &conts[2]);
		exit(0);
	} else {
		rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group, 1, -1);
		assert_rc_equal(rc, -DER_AGAIN);
		waitpid(pid, &rc, 0);
	}

	rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	assert_rc_equal(rc, 0);

	rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group, 1, -1);
	assert_rc_equal(rc, 0);

	test_verify_cont(arg, pool);
}

static void
inc_reint5(void **state)
{
	test_arg_t       *arg      = *state;
	struct test_pool *pool     = &arg->pool;
	struct test_cont  conts[3] = {0};
	pid_t             pid;
	int               rc;
	int               i;

	FAULT_INJECTION_REQUIRED();

	print_message("INC_REINT5: race between container recovery and container destroy\n");

	for (i = 0; i < 3; i++)
		inc_reint_cont_create(arg, &conts[i]);

	rebuild_single_pool_rank(arg, 1, false);

	rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				   DAOS_REBUILD_REINT_SLOW | DAOS_FAIL_ALWAYS, 0, NULL);
	assert_rc_equal(rc, 0);

	pid = fork();
	assert_true(pid >= 0);

	if (pid == 0) {
		sleep(2);
		inc_reint_cont_destroy(arg, &conts[1]);
		exit(0);
	} else {
		rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group, 1, -1);
		assert_rc_equal(rc, -DER_AGAIN);
		waitpid(pid, &rc, 0);
	}

	rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	assert_rc_equal(rc, 0);

	rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group, 1, -1);
	assert_rc_equal(rc, 0);

	test_verify_cont(arg, pool);
}

static int
inc_reint_sub_setup(void **state)
{
	daos_prop_t *prop;
	int          rc;

	save_group_state(state);

	rc = test_setup(state, SETUP_EQ, false, REBUILD_SMALL_POOL_SIZE, 0, NULL);
	if (rc != 0)
		return rc;

	prop = daos_prop_alloc(1);
	if (prop == NULL)
		return -DER_NOMEM;

	prop->dpp_entries[0].dpe_type = DAOS_PROP_PO_REINT_MODE;
	prop->dpp_entries[0].dpe_val  = DAOS_REINT_MODE_INCREMENTAL;

	rc = test_setup_pool_create(state, NULL, NULL, prop);
	daos_prop_free(prop);

	if (rc == 0)
		rc = test_setup_pool_connect(state, NULL);

	return rc;
}

static int
inc_reint_sub_teardown(void **state)
{
	int rc;

	rc = test_teardown(state);
	restore_group_state(state);

	return rc;
}

/* clang-format off */
static const struct CMUnitTest inc_reint_tests[] = {
	{"INC_REINT1: verify container recovery for incremental reintegration",
	 inc_reint1, inc_reint_sub_setup, inc_reint_sub_teardown},
	{"INC_REINT2: container recovery for empty pool",
	 inc_reint2, inc_reint_sub_setup, inc_reint_sub_teardown},
	{"INC_REINT3: container recovery for the pool with 2000+ containers",
	 inc_reint3, inc_reint_sub_setup, inc_reint_sub_teardown},
	{"INC_REINT4: race between container recovery and container create",
	 inc_reint4, inc_reint_sub_setup, inc_reint_sub_teardown},
	{"INC_REINT5: race between container recovery and container destroy",
	 inc_reint5, inc_reint_sub_setup, inc_reint_sub_teardown},
};
/* clang-format on */

static int
inc_reint_setup(void **state)
{
	return test_setup(state, SETUP_EQ, false, REBUILD_SMALL_POOL_SIZE, 0, NULL);
}

int
run_daos_inc_reint_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	if (rank == 0) {
		if (sub_tests_size == 0)
			rc = cmocka_run_group_tests_name("DAOS_Inc_Reint", inc_reint_tests,
							 inc_reint_setup, test_teardown);
		else
			rc = run_daos_sub_tests("DAOS_Inc_Reint", inc_reint_tests,
						ARRAY_SIZE(inc_reint_tests), sub_tests,
						sub_tests_size, inc_reint_setup, test_teardown);
	}

	par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);

	return rc;
}
