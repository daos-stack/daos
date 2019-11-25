/**
 * (C) Copyright 2016-2018 Intel Corporation.
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

/** create/destroy pool on all tgts */
static void
pool_create_all(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	char		 uuid_str[64];
	daos_event_t	 ev;
	daos_event_t	*evp;
	int		 rc;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}


	/** create container */
	print_message("creating pool %ssynchronously ... ",
		      arg->async ? "a" : "");
	rc = daos_pool_create(0700 /* mode */, 0 /* uid */, 0 /* gid */,
			      arg->group, NULL /* tgts */, "pmem" /* dev */,
			      0 /* minimal size */, 0 /* nvme size */,
			      NULL /* properties */, &arg->pool.svc /* svc */,
			      uuid, arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		/** wait for container creation */
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(ev.ev_error, 0);
	}

	uuid_unparse_lower(uuid, uuid_str);
	print_message("success uuid = %s\n", uuid_str);

	/** destroy container */
	print_message("destroying pool %ssynchronously ... ",
		      arg->async ? "a" : "");
	rc = daos_pool_destroy(uuid, arg->group, 1, arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		/** for container destroy */
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(ev.ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}
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
		/* Set some properties in the in/out tpools[i] struct */
		lparg->tpools[i].poh = DAOS_HDL_INVAL;
		lparg->tpools[i].svc.rl_ranks = lparg->tpools[i].ranks;
		lparg->tpools[i].svc.rl_nr = svc_nreplicas;
		lparg->tpools[i].pool_size = 1 << 30;	/* 1GB SCM */

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
	int			 rc;

	if (lparg == NULL)
		return 0;

	for (i = 0; i < lparg->nsyspools; i++) {
		if (!uuid_is_null(lparg->tpools[i].pool_uuid)) {
			if (arg->myrank == 0)
				rc = pool_destroy_safe(arg, &lparg->tpools[i]);
			if (arg->multi_rank)
				MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
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
		ranks_match = d_rank_list_identical(&lparg->tpools[i].svc,
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
 * rc_ret:	return code from daos_mgmt_list_pools()
 * npools_in:	npools input argument to daos_mgmt_list_pools()
 * npools_out:	npools output argument value after daos_mgmt_list_pools()
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
	/* Walk through pools[] items daos_mgmt_list_pools() was told about */
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
	rc = daos_mgmt_list_pools(arg->group, &npools, NULL /* pools */,
			NULL /* ev */);
	assert_int_equal(rc, 0);
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
	rc = daos_mgmt_list_pools(arg->group, &npools, pools, NULL /* ev */);
	assert_int_equal(rc, 0);
	verify_pool_info(state, rc, npools_alloc, pools, npools);
	clean_pool_info(npools_alloc, pools);
	print_message("success t%d: pools[] over-sized\n", tnum++);

	/***** Test: provide npools=0, non-NULL pools  ****/
	npools = 0;
	rc = daos_mgmt_list_pools(arg->group, &npools, pools, NULL /* ev */);
	assert_int_equal(rc, 0);
	assert_int_equal(npools, lparg->nsyspools);
	print_message("success t%d: npools=0, non-NULL pools[] rc=%d\n",
		      tnum++, rc);

	/* Teardown for above 2 tests */
	D_FREE(pools);	/* clean_pool_info() freed mgpi_svc */
	pools = NULL;

	/***** Test: invalid npools=NULL *****/
	rc = daos_mgmt_list_pools(arg->group, NULL /* npools */,
				  NULL /* pools */, NULL /* ev */);
	assert_int_equal(rc, -DER_INVAL);
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
		rc = daos_mgmt_list_pools(arg->group, &npools, pools,
					  NULL /* ev */);
		assert_int_equal(rc, 0);
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
		rc = daos_mgmt_list_pools(arg->group, &npools, pools,
					  NULL /* ev */);
		assert_int_equal(rc, -DER_TRUNC);
		verify_pool_info(state, rc, npools_alloc, pools, npools);
		print_message("success t%d: pools[] under-sized\n", tnum++);

		/* Teardown */
		D_FREE(pools);	/* clean_pool_info() freed mgpi_svc */
		pools = NULL;
	} /* if (lpargs->nsyspools > 0) */

	print_message("success\n");
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
};

static int
setup(void **state)
{
	return test_setup(state, SETUP_EQ, false, DEFAULT_POOL_SIZE, NULL);
}

int
run_daos_mgmt_test(int rank, int size)
{
	int	rc;

	if (rank == 0)
		rc = cmocka_run_group_tests_name("Management tests", tests,
						 setup, test_teardown);

	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	return rc;
}
