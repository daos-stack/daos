/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos, basic testing for the management API
 */
#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_mgmt.h>
#include <daos_event.h>
#include <daos/agent.h>

/** create/destroy pool on all tgts */
static void
pool_create_all(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	char		 uuid_str[64];
	int		 rc;

	if (arg->async) {
		print_message("async not supported");
		return;
	}

	/** create container */
	print_message("creating pool synchronously ... ");
	rc = dmg_pool_create(dmg_config_file,
			     geteuid(), getegid(),
			     arg->group, NULL /* tgts */,
			     256 * 1024 * 1024 /* minimal size */,
			     0 /* nvme size */, NULL /* prop */,
			     arg->pool.svc /* svc */, uuid);
	assert_rc_equal(rc, 0);

	uuid_unparse_lower(uuid, uuid_str);
	print_message("success uuid = %s\n", uuid_str);

	/** destroy container */
	print_message("destroying pool synchronously ... ");
	rc = dmg_pool_destroy(dmg_config_file, uuid, arg->group, 1);
	assert_rc_equal(rc, 0);

	print_message("success\n");
}

/***** List Pools Testing ****/

/* Private definition for void * typed test_arg_t.mgmt_lp_args */
struct test_list_pools {
	daos_size_t		 nsyspools;
	struct test_pool	*tpools;
};

static int
setup_pools(void **state, daos_size_t npools)
{
	test_arg_t		*arg = *state;
	struct test_list_pools	*lparg = NULL;
	int			 i;
	int			 rc = 0;

	D_ALLOC_PTR(lparg);
	if (lparg == NULL)
		goto err;

	D_ALLOC_ARRAY(lparg->tpools, npools);
	if (lparg->tpools == NULL)
		goto err_free_lparg;

	for (i = 0; i < npools; i++) {
		d_rank_list_t	tmp_list;

		/* Set some properties in the in/out tpools[i] struct */
		lparg->tpools[i].poh = DAOS_HDL_INVAL;
		tmp_list.rl_nr = svc_nreplicas;
		tmp_list.rl_ranks = lparg->tpools[i].ranks;
		d_rank_list_dup(&lparg->tpools[i].svc, &tmp_list);
		lparg->tpools[i].pool_size = 1 << 28;	/* 256MB SCM */

		/* Create the pool */
		rc = test_setup_pool_create(state, NULL /* ipool */,
					    &lparg->tpools[i], NULL /* prop */);
		if (rc != 0)
			goto err_destroy_pools;
	}

	lparg->nsyspools = npools;
	arg->mgmt_lp_args = lparg;
	return 0;

err_destroy_pools:
	for (i = 0; i < npools; i++) {
		if (!uuid_is_null(lparg->tpools[i].pool_uuid) &&
		    (arg->myrank == 0))
			(void) pool_destroy_safe(arg, &lparg->tpools[i]);
		if (lparg->tpools[i].svc)
			d_rank_list_free(lparg->tpools[i].svc);
	}

	D_FREE(lparg->tpools);

err_free_lparg:
	D_FREE(lparg);

err:
	return 1;
}

static int
teardown_pools(void **state)
{
	test_arg_t		*arg = *state;
	struct test_list_pools	*lparg = arg->mgmt_lp_args;
	int			 i;
	int			 rc = 0;

	if (lparg == NULL)
		return 0;

	for (i = 0; i < lparg->nsyspools; i++) {
		if (!uuid_is_null(lparg->tpools[i].pool_uuid)) {
			if (arg->myrank == 0)
				rc = pool_destroy_safe(arg, &lparg->tpools[i]);
			if (arg->multi_rank)
				par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);
			if (rc != 0)
				return rc;
		}
	}
	lparg->nsyspools = 0;
	D_FREE(lparg->tpools);
	lparg->tpools = NULL;
	D_FREE(arg->mgmt_lp_args);
	arg->mgmt_lp_args = NULL;

	return test_case_teardown(state);
}

static int
setup_zeropools(void **state)
{
	return setup_pools(state, 0 /* npools */);
}

static int
setup_manypools(void **state)
{
	/* Keep this small - CI environment may only have ~6GB in config */
	const daos_size_t npools = 4;

	return setup_pools(state, npools);
}

/* zero out uuids, free svc rank lists in pool info returned by DAOS API */
static void
clean_pool_info(daos_size_t npools, daos_mgmt_pool_info_t *pools) {
	int	i;

	if (pools) {
		for (i = 0; i < npools; i++) {
			uuid_clear(pools[i].mgpi_uuid);
			if (pools[i].mgpi_svc) {
				d_rank_list_free(pools[i].mgpi_svc);
				pools[i].mgpi_svc = NULL;
			}
		}
	}
}

/* Search for pool information in pools created in setup (mgmt_lp_args)
 * Match pool UUID and service replica ranks.
 * Return matching index or -1 if no match.
 */
static int
find_pool(void **state, daos_mgmt_pool_info_t *pool)
{
	test_arg_t		*arg = *state;
	struct test_list_pools	*lparg = arg->mgmt_lp_args;
	int			 i;
	int			 found_idx = -1;

	for (i = 0; i < lparg->nsyspools; i++) {
		bool			 uuid_match;
		bool			 ranks_match;

		uuid_match = (uuid_compare(pool->mgpi_uuid,
					   lparg->tpools[i].pool_uuid) == 0);
		ranks_match = d_rank_list_identical(lparg->tpools[i].svc,
						    pool->mgpi_svc);

		if (uuid_match && ranks_match) {
			found_idx = i;
			break;
		}
	}

	print_message("pool "DF_UUIDF" %sfound in list result\n",
		      DP_UUID(pool->mgpi_uuid),
		      ((found_idx == -1) ? "NOT " : ""));
	return found_idx;
}

/* Verify pool info returned by DAOS API
 * rc_ret:	return code from dmg_list_pool()
 * npools_in:	npools input argument to dmg_list_pool()
 * npools_out:	npools output argument value after dmg_list_pool()
 */
static void
verify_pool_info(void **state, int rc_ret, daos_size_t npools_in,
		 daos_mgmt_pool_info_t *pools, daos_size_t npools_out)
{
	test_arg_t		*arg = *state;
	struct test_list_pools	*lparg = arg->mgmt_lp_args;
	daos_size_t		 nfilled;
	int			 i;
	int			 rc;

	assert_int_equal(npools_out, lparg->nsyspools);

	if (pools == NULL)
		return;

	/* How many entries of pools[] expected to be populated?
	 * In successful calls, npools_out.
	 */
	nfilled = (rc_ret == 0) ? npools_out : 0;

	print_message("verifying pools[0..%zu], nfilled=%zu\n", npools_in,
		      nfilled);
	/* Walk through pools[] items daos_json_list_pool() was told about */
	for (i = 0; i < npools_in; i++) {
		if (i < nfilled) {
			/* pool is found in the setup state */
			rc = find_pool(state, &pools[i]);
			assert_int_not_equal(rc, -1);
		} else {
			/* Expect no content in pools[>=nfilled] */
			rc = uuid_is_null(pools[i].mgpi_uuid);
			assert_int_not_equal(rc, 0);
			assert_ptr_equal(pools[i].mgpi_svc, NULL);
		}
	}
}

/* Common function for testing list pools feature.
 * Some tests can only be run when multiple pools have been created,
 * Other tests may run when there are zero or more pools in the system.
 */
static void
list_pools_test(void **state)
{
	test_arg_t		*arg = *state;
	struct test_list_pools	*lparg = arg->mgmt_lp_args;
	int			 rc;
	daos_size_t		 npools;
	daos_size_t		 npools_alloc;
	daos_size_t		 npools_orig;
	daos_mgmt_pool_info_t	*pools = NULL;
	int			 tnum = 0;

	/***** Test: retrieve number of pools in system *****/
	npools = npools_orig = 0xABC0; /* Junk value (e.g., uninitialized) */
	/* test only */
	rc = dmg_pool_list(dmg_config_file, arg->group, &npools, NULL);
	assert_rc_equal(rc, 0);
	verify_pool_info(state, rc, npools_orig, NULL /* pools */, npools);
	print_message("success t%d: output npools=%zu\n", tnum++,
		lparg->nsyspools);

	/* Setup for next 2 tests: alloc pools[] */
	npools_alloc = lparg->nsyspools + 10;
	D_ALLOC_ARRAY(pools, npools_alloc);
	assert_ptr_not_equal(pools, NULL);

	/***** Test: provide npools, pools. Expect npools=nsyspools
	 * and that many items in pools[] filled
	 *****/
	npools = npools_alloc;
	rc = dmg_pool_list(dmg_config_file, arg->group, &npools, pools);
	assert_rc_equal(rc, 0);
	verify_pool_info(state, rc, npools_alloc, pools, npools);
	clean_pool_info(npools_alloc, pools);
	print_message("success t%d: pools[] over-sized\n", tnum++);

	/***** Test: provide npools=0, non-NULL pools  ****/
	npools = 0;
	rc = dmg_pool_list(dmg_config_file, arg->group, &npools, pools);
	if (lparg->nsyspools > 0)
		assert_rc_equal(rc, -DER_TRUNC);
	else
		assert_rc_equal(rc, 0);
	assert_int_equal(npools, lparg->nsyspools);
	print_message("success t%d: npools=0, non-NULL pools[] rc=%d\n",
		      tnum++, rc);

	/* Teardown for above 2 tests */
	D_FREE(pools);	/* clean_pool_info() freed mgpi_svc */
	pools = NULL;

	/***** Test: invalid npools=NULL *****/
	rc = dmg_pool_list(dmg_config_file, arg->group, NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);
	print_message("success t%d: in &npools NULL, -DER_INVAL\n", tnum++);


	/*** Tests that can only run with multiple pools ***/
	if (lparg->nsyspools > 1) {
		/***** Test: Exact size buffer *****/
		/* Setup */
		npools_alloc = lparg->nsyspools;
		D_ALLOC_ARRAY(pools, npools_alloc);
		assert_ptr_not_equal(pools, NULL);

		/* Test: Exact size buffer */
		npools = npools_alloc;
		rc = dmg_pool_list(dmg_config_file, arg->group, &npools, pools);
		assert_rc_equal(rc, 0);
		verify_pool_info(state, rc, npools_alloc, pools, npools);

		/* Teardown */
		D_FREE(pools);	/* clean_pool_info() freed mgpi_svc */
		pools = NULL;
		print_message("success t%d: pools[] exact length\n", tnum++);

		/***** Test: Under-sized buffer (negative) -DER_TRUNC *****/
		/* Setup */
		npools_alloc = lparg->nsyspools - 1;
		D_ALLOC_ARRAY(pools, npools_alloc);
		assert_ptr_not_equal(pools, NULL);

		/* Test: Under-sized buffer */
		npools = npools_alloc;
		rc = dmg_pool_list(dmg_config_file, arg->group, &npools, pools);
		assert_rc_equal(rc, -DER_TRUNC);
		verify_pool_info(state, rc, npools_alloc, pools, npools);
		print_message("success t%d: pools[] under-sized\n", tnum++);

		/* Teardown */
		D_FREE(pools);	/* clean_pool_info() freed mgpi_svc */
		pools = NULL;
	} /* if (lpargs->nsyspools > 0) */

	print_message("success\n");
}

static void
pool_create_and_destroy_retry(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	int		 rc;

	FAULT_INJECTION_REQUIRED();

	if (arg->myrank != 0)
		return;

	print_message("setting DAOS_POOL_CREATE_FAIL_CORPC ... ");
	rc = daos_debug_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				  DAOS_POOL_CREATE_FAIL_CORPC | DAOS_FAIL_ONCE,
				  0, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("creating pool synchronously ... ");
	rc = dmg_pool_create(dmg_config_file,
			     geteuid(), getegid(),
			     arg->group, NULL /* tgts */,
			     256 * 1024 * 1024 /* minimal size */,
			     0 /* nvme size */, NULL /* prop */,
			     arg->pool.svc /* svc */, uuid);
	assert_rc_equal(rc, 0);
	print_message("success uuid = "DF_UUIDF"\n", DP_UUID(uuid));

	print_message("setting DAOS_POOL_DESTROY_FAIL_CORPC ... ");
	rc = daos_debug_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				  DAOS_POOL_DESTROY_FAIL_CORPC | DAOS_FAIL_ONCE,
				  0, NULL);
	assert_success(rc);
	print_message("success\n");

	print_message("destroying pool synchronously ... ");
	rc = dmg_pool_destroy(dmg_config_file, uuid, arg->group, 1);
	assert_rc_equal(rc, 0);

	print_message("success\n");
}

static void
get_sys_info_test(void **state)
{
	struct daos_sys_info	*info = NULL;
	char			*old_agent_path;
	uint32_t		i;
	int			rc;

	print_message("SUBTEST: alloc with NULL output\n");
	rc = daos_mgmt_get_sys_info("something", NULL);
	assert_rc_equal(rc, -DER_INVAL);

	print_message("SUBTEST: free with NULL input\n");
	daos_mgmt_put_sys_info(NULL); /* ensure it doesn't crash */

	print_message("SUBTEST: bad agent socket\n");
	old_agent_path = dc_agent_sockpath;
	dc_agent_sockpath = "/fake/path/not/real";
	rc = daos_mgmt_get_sys_info(NULL, &info);

	/* restore the global variable before checking rc */
	dc_agent_sockpath = old_agent_path;
	assert_rc_equal(rc, -DER_AGENT_COMM);
	assert_null(info);

	print_message("SUBTEST: success\n");
	rc = daos_mgmt_get_sys_info(NULL, &info);
	assert_rc_equal(rc, 0);
	assert_non_null(info);

	assert_int_not_equal(strnlen(info->dsi_system_name, DAOS_SYS_INFO_STRING_MAX), 0);
	print_message("system name: %s\n", info->dsi_system_name);
	assert_int_not_equal(strnlen(info->dsi_fabric_provider, DAOS_SYS_INFO_STRING_MAX), 0);
	print_message("provider: %s\n", info->dsi_fabric_provider);
	assert_non_null(info->dsi_ranks);
	print_message("number of ranks: %d\n", info->dsi_nr_ranks);
	for (i = 0; i < info->dsi_nr_ranks; i++)
		print_message("rank %u, uri: %s\n", info->dsi_ranks[i].dru_rank,
			      info->dsi_ranks[i].dru_uri);

	daos_mgmt_put_sys_info(info);
}

static const struct CMUnitTest tests[] = {
	{ "MGMT1: create/destroy pool on all tgts",
	  pool_create_all, async_disable, test_case_teardown},
	{ "MGMT2: create/destroy pool on all tgts (async)",
	  pool_create_all, async_enable, test_case_teardown},
	{ "MGMT3: list-pools with no pools in sys",
	  list_pools_test, setup_zeropools, teardown_pools},
	{ "MGMT4: list-pools with multiple pools in sys",
	  list_pools_test, setup_manypools, teardown_pools},
	{ "MGMT5: retry MGMT_POOL_{CREATE,DESETROY} upon errors",
	  pool_create_and_destroy_retry, async_disable, test_case_teardown},
	{ "MGMT6: daos_mgmt_get_sys_info",
	  get_sys_info_test, async_disable, test_case_teardown},
};

static int
setup(void **state)
{
	return test_setup(state, SETUP_EQ, false, DEFAULT_POOL_SIZE, 0, NULL);
}

int
run_daos_mgmt_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int	rc;

	if (rank == 0) {
		if (sub_tests_size == 0) {
			rc = cmocka_run_group_tests_name(
				"DAOS_Management", tests, setup,
				test_teardown);
		} else {
			rc = run_daos_sub_tests(
				"DAOS_Management", tests,
				ARRAY_SIZE(tests),
				sub_tests, sub_tests_size, setup,
				test_teardown);
		}
	}

	par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);
	return rc;
}
