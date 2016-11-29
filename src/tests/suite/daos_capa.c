/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of daos
 *
 * tests/suite/daos_capa.c
 */

#include "daos_test.h"

void
poh_invalidate_local(daos_handle_t *poh)
{
	daos_iov_t	ghdl = { NULL, 0, 0 };
	int		rc;

	/** fetch size of global handle */
	rc = daos_pool_local2global(*poh, &ghdl);
	assert_int_equal(rc, 0);

	/** allocate buffer for global pool handle */
	ghdl.iov_buf = malloc(ghdl.iov_buf_len);
	ghdl.iov_len = ghdl.iov_buf_len;

	/** generate global handle */
	rc = daos_pool_local2global(*poh, &ghdl);
	assert_int_equal(rc, 0);

	/** close local handle */
	rc = daos_pool_disconnect(*poh, NULL);
	assert_int_equal(rc, 0);

	/** recreate it ... although it is not valid on the server */
	rc = daos_pool_global2local(ghdl, poh);
	assert_int_equal(rc, 0);
}

/** query with invalid pool handle */
static void
query(void **state)
{
	test_arg_t		*arg = *state;
	daos_pool_info_t	 info;
	daos_handle_t		 poh;
	int			 rc;

	if (arg->myrank != 0)
		return;

	/** connect to the pool */
	rc = daos_pool_connect(arg->pool_uuid, NULL /* grp */,
			       &arg->svc, DAOS_PC_RW, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** query pool info with valid handle */
	rc = daos_pool_query(poh, NULL, &info, NULL);
	assert_int_equal(rc, 0);

	/** invalidate local pool handle */
	poh_invalidate_local(&poh);

	/** query pool info with invalid handle */
	rc = daos_pool_query(poh, NULL, &info, NULL);
	assert_int_equal(rc, -DER_NO_HDL);

	/** close local handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);
}

/** create container with invalid pool handle */
static void
create(void **state)
{
	test_arg_t		*arg = *state;
	daos_handle_t		 poh;
	uuid_t			 uuid;
	int			 rc;

	if (arg->myrank != 0)
		return;

	/** connect to the pool in read-only mode */
	rc = daos_pool_connect(arg->pool_uuid, NULL /* grp */,
			       &arg->svc, DAOS_PC_RO, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** create container with read-only handle */
	uuid_generate(uuid);
	rc = daos_cont_create(poh, uuid, NULL);
	assert_int_equal(rc, -DER_NO_PERM);

	/** close local RO handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);

	/** connect to the pool in read-write mode */
	rc = daos_pool_connect(arg->pool_uuid, NULL /* grp */,
			       &arg->svc, DAOS_PC_RW, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** invalidate local pool handle */
	poh_invalidate_local(&poh);

	/** create container with invalid handle */
	uuid_generate(uuid);
	rc = daos_cont_create(poh, uuid, NULL);
	assert_int_equal(rc, -DER_NO_HDL);

	/** close local handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);
}

/** destroy container with invalid pool handle */
static void
destroy(void **state)
{
	test_arg_t		*arg = *state;
	daos_handle_t		 poh;
	uuid_t			 uuid;
	int			 rc;

	if (arg->myrank != 0)
		return;

	/** connect to the pool in read-write mode */
	rc = daos_pool_connect(arg->pool_uuid, NULL /* grp */,
			       &arg->svc, DAOS_PC_RW, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** create container */
	uuid_generate(uuid);
	rc = daos_cont_create(poh, uuid, NULL);
	assert_int_equal(rc, 0);

	/** invalidate local pool handle */
	poh_invalidate_local(&poh);

	/** destroy container with invalid handle */
	rc = daos_cont_destroy(poh, uuid, true, NULL);
	assert_int_equal(rc, -DER_NO_HDL);

	/** close local handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);

	/** connect to the pool in read-only mode */
	rc = daos_pool_connect(arg->pool_uuid, NULL /* grp */,
			       &arg->svc, DAOS_PC_RO, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** destroy container with RO handle */
	rc = daos_cont_destroy(poh, uuid, true, NULL);
	assert_int_equal(rc, -DER_NO_PERM);

	/** close local RO handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);

	/** connect to the pool in read-write mode */
	rc = daos_pool_connect(arg->pool_uuid, NULL /* grp */,
			       &arg->svc, DAOS_PC_RW, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** destroy container with valid handle */
	rc = daos_cont_destroy(poh, uuid, true, NULL);
	assert_int_equal(rc, 0);

	/** close local handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);
}

/** open container with invalid pool handle */
static void
open(void **state)
{
	test_arg_t		*arg = *state;
	daos_handle_t		 poh;
	daos_handle_t		 coh;
	uuid_t			 uuid;
	int			 rc;

	if (arg->myrank != 0)
		return;

	/** connect to the pool in read-write mode */
	rc = daos_pool_connect(arg->pool_uuid, NULL /* grp */,
			       &arg->svc, DAOS_PC_RW, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** create container */
	uuid_generate(uuid);
	rc = daos_cont_create(poh, uuid, NULL);
	assert_int_equal(rc, 0);

	/** invalidate pool handle */
	poh_invalidate_local(&poh);

	/** open container while pool handle has been revoked */
	rc = daos_cont_open(poh, uuid, DAOS_COO_RW, &coh, NULL, NULL);
	assert_int_equal(rc, -DER_NO_HDL);

	/** close pool handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);

	/** reconnect to the pool in read-only mode */
	rc = daos_pool_connect(arg->pool_uuid, NULL /* grp */,
			       &arg->svc, DAOS_PC_RO, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** open container in read/write mode */
	rc = daos_cont_open(poh, uuid, DAOS_COO_RW, &coh, NULL, NULL);
	assert_int_equal(rc, -DER_NO_PERM);

	/** invalidate pool handle */
	poh_invalidate_local(&poh);

	/** open container while pool handle has been revoked */
	rc = daos_cont_open(poh, uuid, DAOS_COO_RO, &coh, NULL, NULL);
	assert_int_equal(rc, -DER_NO_HDL);

	/** close pool handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);

	/** connect to the pool in read-write mode */
	rc = daos_pool_connect(arg->pool_uuid, NULL /* grp */,
			       &arg->svc, DAOS_PC_RW, &poh,
			       NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/** destroy container with valid handle */
	rc = daos_cont_destroy(poh, uuid, true, NULL);
	assert_int_equal(rc, 0);

	/** close local handle */
	rc = daos_pool_disconnect(poh, NULL);
	assert_int_equal(rc, 0);
}

static const struct CMUnitTest capa_tests[] = {
	{ "CAPA1: query pool with invalid pool handle",
	  query, NULL, NULL},
	{ "CAPA2: create container with invalid pool handle",
	  create, NULL, NULL},
	{ "CAPA3: destroy container with invalid pool handle",
	  destroy, NULL, NULL},
	{ "CAPA4: open container with invalid pool handle",
	  open, NULL, NULL},
};

static int
setup(void **state)
{
	test_arg_t	*arg;
	int		 rc;

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		return -1;

	rc = daos_eq_create(&arg->eq);
	if (rc)
		return rc;

	arg->svc.rl_nr.num = 8;
	arg->svc.rl_nr.num_out = 0;
	arg->svc.rl_ranks = arg->ranks;

	arg->hdl_share = false;
	uuid_clear(arg->pool_uuid);
	MPI_Comm_rank(MPI_COMM_WORLD, &arg->myrank);
	MPI_Comm_size(MPI_COMM_WORLD, &arg->rank_size);

	if (arg->myrank == 0) {
		/** create pool with minimal size */
		rc = daos_pool_create(0731, geteuid(), getegid(), "srv_grp",
				      NULL, "pmem", 0, &arg->svc,
				      arg->pool_uuid, NULL);
	}

	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	*state = arg;
	return 0;
}

static int
teardown(void **state) {
	test_arg_t	*arg = *state;
	int		 rc;

	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank == 0)
		rc = daos_pool_destroy(arg->pool_uuid, "srv_grp", 1, NULL);

	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	rc = daos_eq_destroy(arg->eq, 0);
	if (rc)
		return rc;

	D_FREE_PTR(arg);
	return 0;
}
int
run_daos_capa_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("DAOS capability tests",
					 capa_tests, setup, teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
