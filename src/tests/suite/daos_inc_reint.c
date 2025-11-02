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
ir_cont_create(test_arg_t *arg, struct test_cont *cont)
{
	struct test_pool *pool = &arg->pool;
	int               rc;

	print_message("IR: creating container ...\n");

	rc = daos_cont_create(pool->poh, &cont->uuid, NULL, NULL);
	assert_rc_equal(rc, 0);

	uuid_unparse_upper(cont->uuid, cont->label);

	rc = daos_cont_open(pool->poh, cont->label, DAOS_COO_RW, &cont->coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	print_message("IR: created container " DF_UUID "\n", DP_UUID(cont->uuid));
}

static void
ir_cont_destroy(test_arg_t *arg, struct test_cont *cont)
{
	int rc;

	print_message("IR: destroying container " DF_UUID "\n", DP_UUID(cont->uuid));

	if (daos_handle_is_valid(cont->coh)) {
		rc = daos_cont_close(cont->coh, NULL);
		assert_rc_equal(rc, 0);
		cont->coh = DAOS_HDL_INVAL;
	}

	rc = daos_cont_destroy(arg->pool.poh, cont->label, 0, NULL);
	assert_rc_equal(rc, 0);

	print_message("IR: destroyed container " DF_UUID "\n", DP_UUID(cont->uuid));
}

static void
ir_rank_exclude(test_arg_t *arg, d_rank_t rank)
{
	/* Sleep for a while for cont IV before excluding the rank. */
	sleep(5);

	print_message("IR: excluding rank %d\n", rank);

	rebuild_single_pool_rank(arg, rank, true);

	print_message("IR: excluded rank %d\n", rank);
}

static int
ir_rank_restart(test_arg_t *arg, d_rank_t rank, uint64_t fail_loc)
{
	int rc;
	int i;

	/* Sleep for a while for cont IV before restarting the rank. */
	sleep(5);

	print_message("IR: restarting rank %d\n", rank);

	rc = dmg_system_start_rank(arg->dmg_config, rank);
	if (rc != 0) {
		print_message("IR: fail to restart rank %d: " DF_RC "\n", rank, DP_RC(rc));
		return rc;
	}

	sleep(10);

	if (fail_loc != 0) {
		for (i = 0; i < 10; i++) {
			rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, fail_loc, 0,
						   NULL);
			if (rc == 0 || rc != -DER_TIMEDOUT)
				break;

			sleep(2);
		}
	}

	print_message("IR: restarted rank %d\n", rank);

	return rc;
}

static int
ir_rank_reint(test_arg_t *arg, d_rank_t rank, bool restart)
{
	int rc;

	if (restart) {
		rc = ir_rank_restart(arg, rank, 0);
		if (rc != 0)
			return rc;
	}

	print_message("IR: reintegrating rank %d\n", rank);

	rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group, rank, -1);
	if (rc != 0)
		print_message("IR: fail to reintegrate rank %d: " DF_RC "\n", rank, DP_RC(rc));
	else
		print_message("IR: reintegrated rank %d\n", rank);

	return rc;
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
		ir_cont_create(arg, &conts[i]);

	ir_rank_exclude(arg, 1);

	for (i = 5; i < 7; i++)
		ir_cont_create(arg, &conts[i]);

	for (i = 1; i < 5; i += 2)
		ir_cont_destroy(arg, &conts[i]);

	/*
	 * Now, the left containers are: 0, 2, 4, 5, 6. When we incrementally reintegrate former
	 * excluded rank, it is expected to create cont_5 & cont_6, then destroy cont_1 & cont_3.
	 */
	print_message("Incrementally reintegrate rank 1 for pool " DF_UUID "\n",
		      DP_UUID(pool->pool_uuid));

	rc = ir_rank_reint(arg, 1, true);
	assert_rc_equal(rc, 0);

	test_verify_cont(arg, pool, conts, 7);
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
		ir_cont_create(arg, &conts[i]);

	ir_rank_exclude(arg, 1);

	for (i = 0; i < 5; i++)
		ir_cont_destroy(arg, &conts[i]);

	print_message("Incrementally reintegrate rank 1 for pool " DF_UUID "\n",
		      DP_UUID(pool->pool_uuid));

	rc = ir_rank_reint(arg, 1, true);
	assert_rc_equal(rc, 0);

	test_verify_cont(arg, pool, conts, 5);
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

	print_message("INC_REINT3: recovery for the pool with huge amount of containers\n");

	D_ALLOC_ARRAY(conts, 256);
	assert_non_null(conts);

	ir_cont_create(arg, &conts[0]);

	ir_rank_exclude(arg, 1);

	for (i = 1; i < 256; i++)
		ir_cont_create(arg, &conts[i]);

	print_message("Incrementally reintegrate rank 1 for pool " DF_UUID "\n",
		      DP_UUID(pool->pool_uuid));

	rc = ir_rank_reint(arg, 1, true);
	assert_rc_equal(rc, 0);

	test_verify_cont(arg, pool, conts, 256);
	D_FREE(conts);
}

static void
ir_race(test_arg_t *arg, bool create)
{
	struct test_pool *pool     = &arg->pool;
	struct test_cont  conts[4] = {0};
	pid_t             pid;
	int               rc;
	int               i;

	for (i = 0; i < 3; i++)
		ir_cont_create(arg, &conts[i]);

	ir_rank_exclude(arg, 1);

	rc = ir_rank_restart(arg, 1, DAOS_POOL_REINT_SLOW | DAOS_FAIL_ALWAYS);
	assert_rc_equal(rc, 0);

	pid = fork();
	assert_true(pid >= 0);

	if (pid == 0) {
		/* Wait for ir_rank_reint can be run firstly on parent. */
		sleep(3);

		if (create)
			ir_cont_create(arg, &conts[3]);
		else
			ir_cont_destroy(arg, &conts[1]);

		/* Do NOT exit immediately, otherwise, the pipeline for parent may be broken. */
		sleep(15);
		exit(0);
	} else {
		/* Incremental reintegration will internally repeat to handle the race. */
		rc = ir_rank_reint(arg, 1, false);
		assert_rc_equal(rc, 0);
		waitpid(pid, &rc, 0);
	}

	rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	assert_rc_equal(rc, 0);

	test_verify_cont(arg, pool, conts, 4);
}

static void
inc_reint4(void **state)
{
	test_arg_t *arg = *state;

	FAULT_INJECTION_REQUIRED();

	print_message("INC_REINT4: race between container recovery and container create\n");

	ir_race(arg, true);
}

static void
inc_reint5(void **state)
{
	test_arg_t *arg = *state;

	FAULT_INJECTION_REQUIRED();

	print_message("INC_REINT5: race between container recovery and container destroy\n");

	ir_race(arg, false);
}

static int
ir_sub_setup(void **state)
{
	test_arg_t *arg;
	int         rc;

	save_group_state(state);

	rc = test_setup(state, SETUP_POOL_CONNECT, false, REBUILD_SMALL_POOL_SIZE, 0, NULL);
	if (rc != 0)
		return rc;

	arg = *state;
	rc  = daos_pool_set_prop(arg->pool.pool_uuid, "reintegration", "incremental");

	print_message("SETUP incremental reintegration: " DF_RC "\n", DP_RC(rc));

	return rc;
}

static int
ir_sub_teardown(void **state)
{
	int rc;

	rc = test_teardown(state);
	restore_group_state(state);

	return rc;
}

/* clang-format off */
static const struct CMUnitTest ir_tests[] = {
	{"INC_REINT1: verify container recovery for incremental reintegration",
	 inc_reint1, ir_sub_setup, ir_sub_teardown},
	{"INC_REINT2: container recovery for empty pool",
	 inc_reint2, ir_sub_setup, ir_sub_teardown},
	{"INC_REINT3: recovery for the pool with huge amount of containers",
	 inc_reint3, ir_sub_setup, ir_sub_teardown},
	{"INC_REINT4: race between container recovery and container create",
	 inc_reint4, ir_sub_setup, ir_sub_teardown},
	{"INC_REINT5: race between container recovery and container destroy",
	 inc_reint5, ir_sub_setup, ir_sub_teardown},
};
/* clang-format on */

static int
ir_setup(void **state)
{
	return test_setup(state, SETUP_EQ, false, REBUILD_SMALL_POOL_SIZE, 0, NULL);
}

int
run_daos_inc_reint_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	if (rank == 0) {
		if (sub_tests_size == 0)
			rc = cmocka_run_group_tests_name("DAOS_Inc_Reint", ir_tests, ir_setup,
							 test_teardown);
		else
			rc = run_daos_sub_tests("DAOS_Inc_Reint", ir_tests, ARRAY_SIZE(ir_tests),
						sub_tests, sub_tests_size, ir_setup, test_teardown);
	}

	par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);

	return rc;
}
