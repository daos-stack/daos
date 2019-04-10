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
 * tests/suite/pool.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_test.h"

/** connect to non-existing pool */
static void
pool_connect_nonexist(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	daos_handle_t	 poh;
	int		 rc;

	if (arg->myrank != 0)
		return;

	uuid_generate(uuid);
	rc = daos_pool_connect(uuid, arg->group, &arg->pool.svc, DAOS_PC_RW,
			       &poh, NULL /* info */, NULL /* ev */);
	assert_int_equal(rc, -DER_NONEXIST);
}

/** connect/disconnect to/from a valid pool */
static void
pool_connect(void **state)
{
	test_arg_t	*arg = *state;
	daos_handle_t	 poh;
	daos_event_t	 ev;
	daos_pool_info_t info;
	int		 rc;

	if (!arg->hdl_share && arg->myrank != 0)
		return;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	if (arg->myrank == 0) {
		/** connect to pool */
		print_message("rank 0 connecting to pool %ssynchronously ... ",
			      arg->async ? "a" : "");
		rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
				       &arg->pool.svc, DAOS_PC_RW, &poh, &info,
				       arg->async ? &ev : NULL /* ev */);
		assert_int_equal(rc, 0);
		WAIT_ON_ASYNC(arg, ev);
		assert_memory_equal(info.pi_uuid, arg->pool.pool_uuid,
				    sizeof(info.pi_uuid));
		/** TODO: assert_int_equal(info.pi_ntargets, arg->...); */
		assert_int_equal(info.pi_ndisabled, 0);
		assert_int_equal(info.pi_uid, arg->uid);
		assert_int_equal(info.pi_gid, arg->gid);
		assert_int_equal(info.pi_mode, arg->mode);
		print_message("success\n");

		print_message("rank 0 querying pool info... ");
		memset(&info, 'D', sizeof(info));
		info.pi_bits = DPI_ALL;
		rc = daos_pool_query(poh, NULL /* tgts */, &info, NULL,
				     arg->async ? &ev : NULL /* ev */);
		assert_int_equal(rc, 0);
		WAIT_ON_ASYNC(arg, ev);
		assert_int_equal(info.pi_ndisabled, 0);
		assert_int_equal(info.pi_uid, arg->uid);
		assert_int_equal(info.pi_gid, arg->gid);
		assert_int_equal(info.pi_mode, arg->mode);
		print_message("success\n");
	}

	if (arg->hdl_share)
		handle_share(&poh, HANDLE_POOL, arg->myrank, poh, 1);

	/** disconnect from pool */
	print_message("rank %d disconnecting from pool %ssynchronously ... ",
		      arg->myrank, arg->async ? "a" : "");
	rc = daos_pool_disconnect(poh, arg->async ? &ev : NULL /* ev */);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
		/* disable the async after testing done */
		arg->async = false;
	}
	print_message("rank %d success\n", arg->myrank);
}

/** connect exclusively to a pool */
static void
pool_connect_exclusively(void **state)
{
	test_arg_t	*arg = *state;
	daos_handle_t	 poh;
	daos_handle_t	 poh_ex;
	int		 rc;

	if (arg->myrank != 0)
		return;

	print_message("SUBTEST 1: other connections already exist; shall get "
		      "%d\n", -DER_BUSY);
	print_message("establishing a non-exclusive connection\n");
	rc = daos_pool_connect(arg->pool.pool_uuid, arg->group, &arg->pool.svc,
			       DAOS_PC_RW, &poh, NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);
	print_message("trying to establish an exclusive connection\n");
	rc = daos_pool_connect(arg->pool.pool_uuid, arg->group, &arg->pool.svc,
			       DAOS_PC_EX, &poh_ex, NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, -DER_BUSY);
	print_message("disconnecting the non-exclusive connection\n");
	rc = daos_pool_disconnect(poh, NULL /* ev */);
	assert_int_equal(rc, 0);

	print_message("SUBTEST 2: no other connections; shall succeed\n");
	print_message("establishing an exclusive connection\n");
	rc = daos_pool_connect(arg->pool.pool_uuid, arg->group, &arg->pool.svc,
			       DAOS_PC_EX, &poh_ex, NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	print_message("SUBTEST 3: shall prevent other connections (%d)\n",
		      -DER_BUSY);
	print_message("trying to establish a non-exclusive connection\n");
	rc = daos_pool_connect(arg->pool.pool_uuid, arg->group, &arg->pool.svc,
			       DAOS_PC_RW, &poh, NULL /* info */,
			       NULL /* ev */);
	assert_int_equal(rc, -DER_BUSY);
	print_message("disconnecting the exclusive connection\n");
	rc = daos_pool_disconnect(poh_ex, NULL /* ev */);
	assert_int_equal(rc, 0);
}

/** exclude a target from the pool */
static void
pool_exclude(void **state)
{
	test_arg_t	*arg = *state;
	daos_handle_t	 poh;
	daos_event_t	 ev;
	daos_pool_info_t info;
	struct d_tgt_list tgts;
	d_rank_t	 rank;
	int		 tgt = -1;
	int		 rc;

	if (1) {
		print_message("Skip it for now, because CaRT can't support "
			      "subgroup membership, excluding a node w/o "
			      "killing it will cause IV issue.\n");
		return;
	}

	if (arg->myrank != 0)
		return;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	/** connect to pool */
	print_message("rank 0 connecting to pool %ssynchronously... ",
		      arg->async ? "a" : "");
	rc = daos_pool_connect(arg->pool.pool_uuid, arg->group, &arg->pool.svc,
			       DAOS_PC_RW, &poh, &info,
			       arg->async ? &ev : NULL /* ev */);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("success\n");

	/** exclude last non-svc rank */
	if (info.pi_nnodes - 1 /* rank 0 */ <= arg->pool.svc.rl_nr) {
		print_message("not enough non-svc targets; skipping\n");
		goto disconnect;
	}
	rank = info.pi_nnodes - 1;
	tgts.tl_nr = 1;
	tgts.tl_ranks = &rank;
	tgts.tl_tgts = &tgt;

	print_message("rank 0 excluding rank %u... ", rank);
	rc = daos_pool_tgt_exclude(arg->pool.pool_uuid, arg->group,
				   &arg->pool.svc, &tgts,
				   arg->async ? &ev : NULL /* ev */);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("success\n");

	print_message("rank 0 querying pool info... ");
	memset(&info, 'D', sizeof(info));
	rc = daos_pool_query(poh, NULL /* tgts */, &info, NULL,
			     arg->async ? &ev : NULL /* ev */);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_int_equal(info.pi_ndisabled, 1);
	print_message("success\n");

disconnect:
	/** disconnect from pool */
	print_message("rank %d disconnecting from pool %ssynchronously ... ",
		      arg->myrank, arg->async ? "a" : "");
	rc = daos_pool_disconnect(poh, arg->async ? &ev : NULL /* ev */);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
		/* disable the async after testing done */
		arg->async = false;
	}
	print_message("rank %d success\n", arg->myrank);
}

#define BUFSIZE 10

static void
pool_attribute(void **state)
{
	test_arg_t *arg = *state;
	daos_event_t	 ev;
	int		 rc;

	char const *const names[] = { "AVeryLongName", "Name" };
	size_t const name_sizes[] = {
				strlen(names[0]) + 1,
				strlen(names[1]) + 1,
	};
	void const *const in_values[] = {
				"value",
				"this is a long value"
	};
	size_t const in_sizes[] = {
				strlen(in_values[0]),
				strlen(in_values[1])
	};
	int			 n = (int) ARRAY_SIZE(names);
	char			 out_buf[10 * BUFSIZE] = { 0 };
	void			*out_values[] = {
						  &out_buf[0 * BUFSIZE],
						  &out_buf[1 * BUFSIZE]
						};
	size_t			 out_sizes[] =	{ BUFSIZE, BUFSIZE };
	size_t			 total_size;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	print_message("setting pool attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_pool_set_attr(arg->pool.poh, n, names, in_values, in_sizes,
				arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("listing pool attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");

	total_size = 0;
	rc = daos_pool_list_attr(arg->pool.poh, NULL, &total_size,
				 arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying Total Name Length..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));

	total_size = BUFSIZE;
	rc = daos_pool_list_attr(arg->pool.poh, out_buf, &total_size,
				 arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying Small Name..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));
	assert_string_equal(out_buf, names[1]);

	total_size = 10*BUFSIZE;
	rc = daos_pool_list_attr(arg->pool.poh, out_buf, &total_size,
				 arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying All Names..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));
	assert_string_equal(out_buf, names[0]);
	assert_string_equal(out_buf + name_sizes[0], names[1]);

	print_message("getting pool attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");

	rc = daos_pool_get_attr(arg->pool.poh, n, names, out_values, out_sizes,
				arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying Name-Value (A)..\n");
	assert_int_equal(out_sizes[0], in_sizes[0]);
	assert_memory_equal(out_values[0], in_values[0], in_sizes[0]);

	print_message("Verifying Name-Value (B)..\n");
	assert_true(in_sizes[1] > BUFSIZE);
	assert_int_equal(out_sizes[1], in_sizes[1]);
	assert_memory_equal(out_values[1], in_values[1], BUFSIZE);

	rc = daos_pool_get_attr(arg->pool.poh, n, names, NULL, out_sizes,
				arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying with NULL buffer..\n");
	assert_int_equal(out_sizes[0], in_sizes[0]);
	assert_int_equal(out_sizes[1], in_sizes[1]);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}
}

/** reconnect to pool after re-initializing DAOS lib */
static void
init_fini_conn(void **state)
{
	test_arg_t		*arg = *state;
	int			 rc;

	rc = daos_pool_disconnect(arg->pool.poh, NULL /* ev */);
	arg->pool.poh = DAOS_HDL_INVAL;
	assert_int_equal(rc, 0);

	rc = daos_eq_destroy(arg->eq, 0);
	assert_int_equal(rc, 0);

	rc = daos_fini();
	if (rc)
		print_message("daos_fini() failed with %d\n", rc);
	assert_int_equal(rc, 0);

	rc = daos_init();
	if (rc)
		print_message("daos_init() failed with %d\n", rc);
	assert_int_equal(rc, 0);

	/* the eq should re-create after daos reinit, as the hash-table
	 * re-inited.
	 */
	rc = daos_eq_create(&arg->eq);
	assert_int_equal(rc, 0);

	rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
			       &arg->pool.svc, DAOS_PC_RW,
			       &arg->pool.poh, &arg->pool.pool_info,
			       NULL /* ev */);
	if (rc)
		print_message("daos_pool_connect failed, rc: %d\n", rc);
	else
		print_message("connected to pool, ntarget=%d\n",
			      arg->pool.pool_info.pi_ntargets);
	assert_int_equal(rc, 0);
}

/** create pool with properties and query */
static void
pool_properties(void **state)
{
	test_arg_t		*arg0 = *state;
	test_arg_t		*arg = NULL;
	char			*label = "test_pool_properties";
	uint64_t		 space_rb = 36;
	daos_prop_t		*prop;
	daos_prop_t		*prop_query;
	struct daos_prop_entry *entry;
	int			 rc;

	print_message("create pool with properties, and query it to verify.\n");
	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			DEFAULT_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	prop = daos_prop_alloc(2);
	prop->dpp_entries[0].dpe_type = DAOS_PROP_PO_LABEL;
	prop->dpp_entries[0].dpe_str = strdup(label);
	prop->dpp_entries[1].dpe_type = DAOS_PROP_PO_SPACE_RB;
	prop->dpp_entries[1].dpe_val = space_rb;

	while (!rc && arg->setup_state != SETUP_POOL_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, prop, NULL);
	assert_int_equal(rc, 0);

	prop_query = daos_prop_alloc(0);
	rc = daos_pool_query(arg->pool.poh, NULL, NULL, prop_query, NULL);
	assert_int_equal(rc, 0);

	assert_int_equal(prop_query->dpp_nr, 4);
	/* set properties should get the value user set */
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_LABEL);
	if (entry == NULL || strcmp(entry->dpe_str, label) != 0) {
		print_message("label verification filed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_SPACE_RB);
	if (entry == NULL || entry->dpe_val != space_rb) {
		print_message("space_rb verification filed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	/* not set properties should get default value */
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_SELF_HEAL);
	if (entry == NULL ||
	    entry->dpe_val != (DAOS_SELF_HEAL_AUTO_EXCLUDE |
			       DAOS_SELF_HEAL_AUTO_REBUILD)) {
		print_message("self-heal verification filed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_RECLAIM);
	if (entry == NULL || entry->dpe_val != DAOS_RECLAIM_SNAPSHOT) {
		print_message("reclaim verification filed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	daos_prop_free(prop);
	daos_prop_free(prop_query);
	test_teardown((void **)&arg);
}

static int
pool_setup_sync(void **state)
{
	async_disable(state);
	return test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			  NULL);
}

static int
pool_setup_async(void **state)
{
	async_enable(state);
	return test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			  NULL);
}

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CREATE, true, DEFAULT_POOL_SIZE,
			  NULL);
}

static const struct CMUnitTest pool_tests[] = {
	{ "POOL1: connect to non-existing pool",
	  pool_connect_nonexist, NULL, test_case_teardown},
	{ "POOL2: connect/disconnect to pool",
	  pool_connect, async_disable, test_case_teardown},
	{ "POOL3: connect/disconnect to pool (async)",
	  pool_connect, async_enable, test_case_teardown},
	{ "POOL4: pool handle local2global and global2local",
	  pool_connect, hdl_share_enable, test_case_teardown},
	{ "POOL5: exclusive connection",
	  pool_connect_exclusively, NULL, test_case_teardown},
	/* Keep this one at the end, as it excludes target rank 1. */
	{ "POOL6: exclude targets and query pool info",
	  pool_exclude, async_disable, NULL},
	{ "POOL7: set/get/list user-defined pool attributes (sync)",
	  pool_attribute, pool_setup_sync, test_case_teardown},
	{ "POOL8: set/get/list user-defined pool attributes (async)",
	  pool_attribute, pool_setup_async, test_case_teardown},
	{ "POOL9: pool reconnect after daos re-init",
	  init_fini_conn, NULL, test_case_teardown},
	{ "POOL10: pool create with properties and query",
	  pool_properties, NULL, test_case_teardown},
};

int
run_daos_pool_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("Pool tests", pool_tests,
					 setup, test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
