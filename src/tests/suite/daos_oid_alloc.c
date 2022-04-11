/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos_m
 *
 * src/tests/suite/
 */

#include "daos_test.h"

static void
reconnect(test_arg_t *arg) {
	int rc;
	unsigned int flags;

	par_barrier(PAR_COMM_WORLD);
	rc = daos_cont_close(arg->coh, NULL);
	assert_rc_equal(rc, 0);
	par_barrier(PAR_COMM_WORLD);
	rc = daos_pool_disconnect(arg->pool.poh, NULL /* ev */);
	arg->pool.poh = DAOS_HDL_INVAL;
	assert_rc_equal(rc, 0);
	par_barrier(PAR_COMM_WORLD);

	flags = (DAOS_COO_RW | DAOS_COO_FORCE);
	if (arg->myrank == 0) {
		rc = daos_pool_connect(arg->pool.pool_str, arg->group,
				       DAOS_PC_RW,
				       &arg->pool.poh, &arg->pool.pool_info,
				       NULL /* ev */);
		if (rc)
			goto bcast;
		rc = daos_cont_open(arg->pool.poh, arg->co_str, flags,
				    &arg->coh, &arg->co_info, NULL);
	}
bcast:
	/** broadcast container open result */
	if (arg->rank_size > 1)
		par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);
	assert_rc_equal(rc, 0);
	/** l2g and g2l the container handle */
	if (arg->rank_size > 1) {
		handle_share(&arg->pool.poh, HANDLE_POOL, arg->myrank,
			     arg->pool.poh, 0);
		handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->pool.poh,
			     0);
	}
}

static void
simple_oid_allocator(void **state)
{
	test_arg_t	*arg = *state;
	uint64_t	oid;
	int		num_oids = 29;
	int		i;
	int		rc;

	for (i = 0; i < 10; i++) {
		par_barrier(PAR_COMM_WORLD);
		if (arg->myrank == 0)
			fprintf(stderr, "%d ---------------------\n", i);
		par_barrier(PAR_COMM_WORLD);

		rc = daos_cont_alloc_oids(arg->coh, num_oids, &oid, NULL);
		if (rc)
			print_message("OID alloc failed (%d)\n", rc);
		assert_rc_equal(rc, 0);

		print_message("%d: OID range %" PRId64 " - %" PRId64 "\n",
			      arg->myrank, oid, oid+num_oids);

		reconnect(arg);
	}
}

static void
multi_cont_oid_allocator(void **state)
{
	test_arg_t	*arg = *state;
	uint64_t	oid, prev_oid = 0;
	int		num_oids = 50;
	int		i;
	int		rc = 0, rc_reduce;

	par_barrier(PAR_COMM_WORLD);
	if (arg->myrank != 0)
		goto verify_rc;

	for (i = 0; i < 10; i++) {
		uuid_t		co_uuid;
		char		str[37];
		daos_handle_t	coh;
		daos_cont_info_t co_info;

		print_message("Cont %d ---------------------\n", i);

		rc = daos_cont_create(arg->pool.poh, &co_uuid, NULL, NULL);
		if (rc) {
			print_message("Cont create failed\n");
			goto verify_rc;
		}

		uuid_unparse(co_uuid, str);
		rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW,
				    &coh, &co_info, NULL);
		if (rc) {
			print_message("Cont Open failed\n");
			goto verify_rc;
		}

		rc = daos_cont_alloc_oids(coh, num_oids, &oid, NULL);
		if (rc) {
			print_message("OID alloc failed (%d)\n", rc);
			daos_cont_close(coh, NULL);
			goto verify_rc;
		}

		print_message("%d: OID range %" PRId64 " - %" PRId64 "\n",
			      arg->myrank, oid, oid+num_oids);

		if (i != 0 && oid != prev_oid) {
			print_message("Cont %d: ID verification failed\n", i);
			rc = -1;
			goto verify_rc;
		}
		prev_oid = oid;

		rc = daos_cont_close(coh, NULL);
		if (rc)
			goto verify_rc;

		rc = daos_cont_destroy(arg->pool.poh, str, 1, NULL);
		if (rc)
			goto verify_rc;
	}

verify_rc:
	par_allreduce(PAR_COMM_WORLD, &rc, &rc_reduce, 1, PAR_INT, PAR_MIN);
	assert_int_equal(rc_reduce, 0);
}

static int
check_ranges(int *num_oids, uint64_t *oids, int num_rgs, test_arg_t *arg)
{
	int *g_num_oids;
	uint64_t *g_oids;
	int i, j;
	int rc;

	D_ALLOC_ARRAY(g_num_oids, num_rgs * arg->rank_size);
	D_ALLOC_ARRAY(g_oids, num_rgs * arg->rank_size);

	rc = par_gather(PAR_COMM_WORLD, num_oids, g_num_oids, num_rgs, PAR_INT, 0);
	assert_int_equal(rc, 0);

	rc = par_gather(PAR_COMM_WORLD, oids, g_oids, num_rgs, PAR_UINT64, 0);
	assert_int_equal(rc, 0);

	if (arg->myrank != 0) {
		rc = 0;
		goto out;
	}

	for (i = 0; i < num_rgs * arg->rank_size; i++) {
		int num = g_num_oids[i];
		uint64_t oid = g_oids[i];

		for (j = 0; j < num_rgs * arg->rank_size; j++) {
			int numx = g_num_oids[j];
			uint64_t oidx = g_oids[j];

			if (j == i)
				continue;

			if ((oidx <= oid && oid < oidx+numx) ||
			    (oidx <= oid+num-1 && oid+num-1 < oidx+numx)) {
				fprintf(stderr,
					"RG OVERLAP: (%"PRId64" - %"PRId64")"
					"(%"PRId64" - %"PRId64")\n",
					oid, oid+num, oidx, oidx+numx);
				return -1;
			}
		}

		if (i % num_rgs == 0)
			print_message("Verified %d ranges...\n", i);
	}
	print_message("Verified %d ranges...\n", i);

out:
	par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);
	return rc;
}

#define NUM_OIDS 20

static void
oid_allocator_mult_hdls(void **state)
{
	test_arg_t	*arg = *state;
	char		*label = "oid_test_mult_hdls";
	uint64_t	oids[NUM_OIDS];
	int		num_oids[NUM_OIDS];
	daos_handle_t	coh1, coh2;
	daos_handle_t	poh1, poh2;
	int		i = 0;
	int		rc = 0;

	srand(time(NULL));
	if (arg->myrank == 0) {
		rc = daos_cont_create_with_label(arg->pool.poh, label, NULL, NULL, NULL);
		assert_rc_equal(rc, 0);
	}

	par_barrier(PAR_COMM_WORLD);
	while (i < NUM_OIDS) {
		rc = daos_pool_connect(arg->pool.pool_str, arg->group, DAOS_PC_RW,
				       &poh1, NULL, NULL);
		assert_rc_equal(rc, 0);

		rc = daos_pool_connect(arg->pool.pool_str, arg->group, DAOS_PC_RW,
				       &poh2, NULL, NULL);
		assert_rc_equal(rc, 0);

		rc = daos_cont_open(poh1, label, DAOS_COO_RW, &coh1, NULL, NULL);
		assert_rc_equal(rc, 0);

		rc = daos_cont_open(poh2, label, DAOS_COO_RW, &coh2, NULL, NULL);
		assert_rc_equal(rc, 0);

		num_oids[i] = rand() % 256 + 1;
		rc = daos_cont_alloc_oids(coh1, num_oids[i], &oids[i], NULL);
		assert_rc_equal(rc, 0);
		i++;

		num_oids[i] = rand() % 256 + 1;
		rc = daos_cont_alloc_oids(coh2, num_oids[i], &oids[i], NULL);
		assert_rc_equal(rc, 0);
		i++;

		rc = daos_cont_close(coh1, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_cont_close(coh2, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_pool_disconnect(poh1, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_pool_disconnect(poh2, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = check_ranges(num_oids, oids, NUM_OIDS, arg);
	assert_int_equal(rc, 0);

	par_barrier(PAR_COMM_WORLD);
	if (arg->myrank == 0) {
		rc = daos_cont_destroy(arg->pool.poh, label, 0, NULL);
		assert_rc_equal(rc, 0);
	}
	par_barrier(PAR_COMM_WORLD);
}

#define NUM_RGS 1000

static void
oid_allocator_checker(void **state)
{
	test_arg_t	*arg = *state;
	uint64_t	oids[NUM_RGS];
	int		num_oids[NUM_RGS];
	int		i = 0;
	int		rc, rc_reduce;

	srand(time(NULL));
	reconnect(arg);

	if (arg->myrank == 0)
		print_message("Allocating %d OID ranges per rank\n", NUM_RGS);

	while (i < NUM_RGS) {
		num_oids[i] = rand() % 256 + 1;
		rc = daos_cont_alloc_oids(arg->coh, num_oids[i], &oids[i],
					  NULL);
		if (rc) {
			if (rc == -DER_UNREACH) {
				rc = 0;
				continue;
			}

			fprintf(stderr, "%d: %d oids alloc failed (%d)\n",
				i, num_oids[i], rc);
			goto check;
		}

		if (i % 100 == 0)
			reconnect(arg);

		/** Kill 2 servers at different times */
		if (i && i % (NUM_RGS/3 + 1) == 0) {
			daos_pool_info_t info = {0};

			par_barrier(PAR_COMM_WORLD);
			rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL,
					     NULL);
			assert_rc_equal(rc, 0);
			if (info.pi_ntargets - info.pi_ndisabled >= 2) {
				if (arg->myrank == 0)
					daos_kill_server(arg,
						arg->pool.pool_uuid,
						arg->group,
						arg->pool.svc, -1);
			}
			par_barrier(PAR_COMM_WORLD);
		}
		i++;
	}

check:
	if (arg->rank_size > 1) {
		par_allreduce(PAR_COMM_WORLD, &rc, &rc_reduce, 1, PAR_INT, PAR_MIN);
		rc = rc_reduce;
	}
	assert_int_equal(rc, 0);
	if (arg->myrank == 0)
		print_message("Allocation done. Verifying no overlaps...\n");

	rc = check_ranges(num_oids, oids, NUM_RGS, arg);
	assert_int_equal(rc, 0);
}

static void
cont_oid_prop(void **state)
{
	test_arg_t		*arg = *state;
	daos_prop_t		*prop;
	uint64_t		oid, alloced_oid;
	uuid_t			co_uuid;
	char			str[37];
	daos_handle_t		coh;
	daos_cont_info_t	co_info;
	int			rc = 0;

	if (arg->myrank != 0)
		return;

	uuid_clear(co_uuid);

	/** set max oid to 2 x 1024 x 1024 */
	alloced_oid = 2 * 1024 * 1024;
	prop = daos_prop_alloc(1);
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_ALLOCED_OID;
	prop->dpp_entries[0].dpe_val = alloced_oid;

	print_message("Create a container with alloced_oid "DF_U64"\n",
		      alloced_oid);
	rc = daos_cont_create(arg->pool.poh, &co_uuid, prop, NULL);
	assert_rc_equal(rc, 0);

	uuid_unparse(co_uuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh,
			    &co_info, NULL);
	assert_rc_equal(rc, 0);

	print_message("Allocate 1 OID, should be >= "DF_U64"\n", alloced_oid);
	rc = daos_cont_alloc_oids(coh, 1, &oid, NULL);
	assert_rc_equal(rc, 0);
	print_message("OID allocated = "DF_U64"\n", oid);
	assert_true(oid >= alloced_oid);

	print_message("GET max OID from container property\n");
	prop->dpp_entries[0].dpe_val = 0;
	rc = daos_cont_query(coh, NULL, prop, NULL);
	assert_rc_equal(rc, 0);
	print_message("MAX OID = "DF_U64"\n", prop->dpp_entries[0].dpe_val);
	assert_true(prop->dpp_entries[0].dpe_val > alloced_oid);

	print_message("Change alloc'ed oid with daos_cont_set_prop "
		      "(should fail)\n");
	rc = daos_cont_set_prop(coh, prop, NULL);
	assert_rc_equal(rc, -DER_NO_PERM);

	daos_prop_free(prop);
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, str, 1, NULL);
	assert_rc_equal(rc, 0);
}

static const struct CMUnitTest oid_alloc_tests[] = {
	{"OID_ALLOC1: Simple OID ALLOCATION (blocking)",
	 simple_oid_allocator, async_disable, NULL},
	{"OID_ALLOC2: Multiple Cont OID ALLOCATION (blocking)",
	 multi_cont_oid_allocator, async_disable, NULL},
	{"OID_ALLOC3: Fetch / Set MAX OID",
	 cont_oid_prop, async_disable, NULL},
	{"OID_ALLOC4: OID allocator with Multiple pool and cont handles",
	 oid_allocator_mult_hdls, async_disable, NULL},
	{"OID_ALLOC5: OID Allocator check (blocking)",
	 oid_allocator_checker, async_disable, NULL},
};

int
oid_alloc_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			  0, NULL);
}

int
run_daos_oid_alloc_test(int rank, int size)
{
	int rc = 0;

	par_barrier(PAR_COMM_WORLD);
	rc = cmocka_run_group_tests_name("DAOS_OID_Allocator", oid_alloc_tests,
					 oid_alloc_setup, test_teardown);
	par_barrier(PAR_COMM_WORLD);
	return rc;
}
