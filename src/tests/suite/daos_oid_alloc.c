/**
 * (C) Copyright 2018-2020 Intel Corporation.
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
 * This file is part of daos_m
 *
 * src/tests/suite/
 */

#include "daos_test.h"

static void
reconnect(test_arg_t *arg) {
	int rc;
	unsigned int flags;

	MPI_Barrier(MPI_COMM_WORLD);
	rc = daos_cont_close(arg->coh, NULL);
	assert_int_equal(rc, 0);
	MPI_Barrier(MPI_COMM_WORLD);
	rc = daos_pool_disconnect(arg->pool.poh, NULL /* ev */);
	arg->pool.poh = DAOS_HDL_INVAL;
	assert_int_equal(rc, 0);
	MPI_Barrier(MPI_COMM_WORLD);

	flags = (DAOS_COO_RW | DAOS_COO_FORCE);
	if (arg->myrank == 0) {
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       NULL /* svc */, DAOS_PC_RW,
				       &arg->pool.poh, &arg->pool.pool_info,
				       NULL /* ev */);
		if (rc)
			goto bcast;
		rc = daos_cont_open(arg->pool.poh, arg->co_uuid, flags,
				    &arg->coh, &arg->co_info, NULL);
	}
bcast:
	/** broadcast container open result */
	if (arg->rank_size > 1)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, 0);
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
		MPI_Barrier(MPI_COMM_WORLD);
		if (arg->myrank == 0)
			fprintf(stderr, "%d ---------------------\n", i);
		MPI_Barrier(MPI_COMM_WORLD);

		rc = daos_cont_alloc_oids(arg->coh, num_oids, &oid, NULL);
		if (rc)
			print_message("OID alloc failed (%d)\n", rc);
		assert_int_equal(rc, 0);

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

	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank != 0)
		goto verify_rc;

	for (i = 0; i < 10; i++) {
		uuid_t		co_uuid;
		daos_handle_t	coh;
		daos_cont_info_t co_info;

		print_message("Cont %d ---------------------\n", i);

		uuid_clear(co_uuid);
		uuid_generate(co_uuid);
		rc = daos_cont_create(arg->pool.poh, co_uuid, NULL, NULL);
		if (rc) {
			print_message("Cont create failed\n");
			goto verify_rc;
		}

		rc = daos_cont_open(arg->pool.poh, co_uuid, DAOS_COO_RW,
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

		rc = daos_cont_destroy(arg->pool.poh, co_uuid, 1, NULL);
		if (rc)
			goto verify_rc;
	}

verify_rc:
	MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
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

	rc = MPI_Gather(num_oids, num_rgs, MPI_INT, g_num_oids, num_rgs,
			MPI_INT, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

	rc = MPI_Gather(oids, num_rgs, MPI_UINT64_T, g_oids, num_rgs,
			MPI_UINT64_T, 0, MPI_COMM_WORLD);
	assert_int_equal(rc, MPI_SUCCESS);

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
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	return rc;
}

#define NUM_RGS 1000

static void
oid_allocator_checker(void **state)
{
	test_arg_t	*arg = *state;
	uint64_t	oids[NUM_RGS];
	int		num_oids[NUM_RGS];
	int		i;
	int		rc, rc_reduce;

	/* skipped until bug DAOS-5999 is fixed */
	skip();

	srand(time(NULL));
	reconnect(arg);

	if (arg->myrank == 0)
		print_message("Allocating %d OID ranges per rank\n", NUM_RGS);

	for (i = 0; i < NUM_RGS; i++) {
		num_oids[i] = rand() % 256 + 1;
		rc = daos_cont_alloc_oids(arg->coh, num_oids[i], &oids[i],
					  NULL);
		if (rc) {
			fprintf(stderr, "%d: %d oids alloc failed (%d)\n",
				i, num_oids[i], rc);
			goto check;
		}

		if (i % 100 == 0)
			reconnect(arg);

		/** Kill 2 servers at different times */
		if (i && i % (NUM_RGS/3 + 1) == 0) {
			daos_pool_info_t info = {0};

			MPI_Barrier(MPI_COMM_WORLD);
			rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL,
					     NULL);
			assert_int_equal(rc, 0);
			if (info.pi_ntargets - info.pi_ndisabled >= 2) {
				if (arg->myrank == 0)
					daos_kill_server(arg,
						arg->pool.pool_uuid,
						arg->group,
						arg->pool.svc, -1);
			}
			MPI_Barrier(MPI_COMM_WORLD);
		}
	}

check:
	if (arg->rank_size > 1) {
		MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN,
			      MPI_COMM_WORLD);
		rc = rc_reduce;
	}
	assert_int_equal(rc, 0);
	if (arg->myrank == 0)
		print_message("Allocation done. Verifying no overlaps...\n");

	rc = check_ranges(num_oids, oids, NUM_RGS, arg);
	assert_int_equal(rc, 0);
}

static const struct CMUnitTest oid_alloc_tests[] = {
	{"OID_ALLOC1: Simple OID ALLOCATION (blocking)",
	 simple_oid_allocator, async_disable, NULL},
	{"OID_ALLOC2: Multiple Cont OID ALLOCATION (blocking)",
	 multi_cont_oid_allocator, async_disable, NULL},
	{"OID_ALLOC3: OID Allocator check (blocking)",
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

	MPI_Barrier(MPI_COMM_WORLD);
	rc = cmocka_run_group_tests_name("OID Allocator tests", oid_alloc_tests,
					 oid_alloc_setup, test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
