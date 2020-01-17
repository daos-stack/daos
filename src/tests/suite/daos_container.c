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
 * tests/suite/container.c
 */
#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"

/** create/destroy container */
static void
co_create(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	daos_handle_t	 coh;
	daos_cont_info_t info;
	daos_event_t	 ev;
	int		 rc;

	if (!arg->hdl_share && arg->myrank != 0)
		return;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	/** container uuid */
	uuid_generate(uuid);

	/** create container */
	if (arg->myrank == 0) {
		print_message("creating container %ssynchronously ...\n",
			      arg->async ? "a" : "");
		rc = daos_cont_create(arg->pool.poh, uuid, NULL,
				      arg->async ? &ev : NULL);
		assert_int_equal(rc, 0);
		WAIT_ON_ASYNC(arg, ev);
		print_message("container created\n");

		print_message("opening container %ssynchronously\n",
			      arg->async ? "a" : "");
		rc = daos_cont_open(arg->pool.poh, uuid, DAOS_COO_RW, &coh,
				    &info, arg->async ? &ev : NULL);
		assert_int_equal(rc, 0);
		WAIT_ON_ASYNC(arg, ev);
		print_message("contained opened\n");
	}

	if (arg->hdl_share)
		handle_share(&coh, HANDLE_CO, arg->myrank, arg->pool.poh, 1);

	print_message("closing container %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_close(coh, arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("container closed\n");

	if (arg->hdl_share)
		MPI_Barrier(MPI_COMM_WORLD);

	/** destroy container */
	if (arg->myrank == 0) {
		/* XXX check if this is a real leak or out-of-sync close */
		sleep(5);
		print_message("destroying container %ssynchronously ...\n",
			      arg->async ? "a" : "");
		rc = daos_cont_destroy(arg->pool.poh, uuid, 1 /* force */,
				    arg->async ? &ev : NULL);
		assert_int_equal(rc, 0);
		WAIT_ON_ASYNC(arg, ev);
		if (arg->async) {
			rc = daos_event_fini(&ev);
			assert_int_equal(rc, 0);
		}
		print_message("container destroyed\n");
	}
}

#define BUFSIZE 10

static void
co_attribute(void **state)
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

	print_message("setting container attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_set_attr(arg->coh, n, names, in_values, in_sizes,
				arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("listing container attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");

	total_size = 0;
	rc = daos_cont_list_attr(arg->coh, NULL, &total_size,
				 arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying Total Name Length..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));

	total_size = BUFSIZE;
	rc = daos_cont_list_attr(arg->coh, out_buf, &total_size,
				 arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying Small Name..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));
	assert_string_equal(out_buf, names[1]);

	total_size = 10*BUFSIZE;
	rc = daos_cont_list_attr(arg->coh, out_buf, &total_size,
				 arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying All Names..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));
	assert_string_equal(out_buf, names[0]);
	assert_string_equal(out_buf + name_sizes[0], names[1]);

	print_message("getting container attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");

	rc = daos_cont_get_attr(arg->coh, n, names, out_values, out_sizes,
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

	rc = daos_cont_get_attr(arg->coh, n, names, NULL, out_sizes,
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

static void
co_properties(void **state)
{
	test_arg_t		*arg0 = *state;
	test_arg_t		*arg = NULL;
	char			*label = "test_cont_properties";
	uint64_t		 snapshot_max = 128;
	daos_prop_t		*prop;
	daos_prop_t		*prop_query;
	struct daos_prop_entry	*entry;
	daos_pool_info_t	 info = {0};
	int			 rc;

	print_message("create container with properties, and query/verify.\n");
	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT, arg0->multi_rank,
			DEFAULT_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	prop = daos_prop_alloc(2);
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	prop->dpp_entries[0].dpe_str = strdup(label);
	prop->dpp_entries[1].dpe_type = DAOS_PROP_CO_SNAPSHOT_MAX;
	prop->dpp_entries[1].dpe_val = snapshot_max;

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL, NULL);
		assert_int_equal(rc, 0);
		rc = daos_mgmt_set_params(arg->group, info.pi_leader,
			DMG_KEY_FAIL_LOC, DAOS_FORCE_PROP_VERIFY, 0, NULL);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	const int prop_count = 6;

	prop_query = daos_prop_alloc(prop_count);
	prop_query->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	prop_query->dpp_entries[1].dpe_type = DAOS_PROP_CO_CSUM;
	prop_query->dpp_entries[2].dpe_type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
	prop_query->dpp_entries[3].dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	prop_query->dpp_entries[4].dpe_type = DAOS_PROP_CO_ENCRYPT;
	prop_query->dpp_entries[5].dpe_type = DAOS_PROP_CO_SNAPSHOT_MAX;
	rc = daos_cont_query(arg->coh, NULL, prop_query, NULL);
	assert_int_equal(rc, 0);

	assert_int_equal(prop_query->dpp_nr, prop_count);
	/* set properties should get the value user set */
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_LABEL);
	if (entry == NULL || strcmp(entry->dpe_str, label) != 0) {
		print_message("label verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_SNAPSHOT_MAX);
	if (entry == NULL || entry->dpe_val != snapshot_max) {
		print_message("snapshot_max verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	/* not set properties should get default value */
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_CSUM);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_CSUM_OFF) {
		print_message("csum verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_CSUM_CHUNK_SIZE);
	if (entry == NULL || entry->dpe_val != 32 * 1024) {
		print_message("csum chunk size verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	entry = daos_prop_entry_get(prop_query,
				    DAOS_PROP_CO_CSUM_SERVER_VERIFY);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_CSUM_SV_OFF) {
		print_message("csum server verify verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_ENCRYPT);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_ENCRYPT_OFF) {
		print_message("encrypt verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	daos_prop_free(prop);
	daos_prop_free(prop_query);
	test_teardown((void **)&arg);
}

static void
co_op_retry(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	daos_handle_t	 coh;
	daos_cont_info_t info;
	int		 rc;

	if (arg->myrank != 0)
		return;

	uuid_generate(uuid);

	print_message("creating container ... ");
	rc = daos_cont_create(arg->pool.poh, uuid, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("success\n");

	print_message("opening container ... ");
	rc = daos_cont_open(arg->pool.poh, uuid, DAOS_COO_RW, &coh, &info,
			    NULL);
	assert_int_equal(rc, 0);
	print_message("success\n");

	print_message("setting DAOS_CONT_QUERY_FAIL_CORPC ... ");
	rc = daos_mgmt_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				  DAOS_CONT_QUERY_FAIL_CORPC | DAOS_FAIL_ONCE,
				  0, NULL);
	assert_int_equal(rc, 0);
	print_message("success\n");

	print_message("querying container ... ");
	rc = daos_cont_query(coh, &info, NULL, NULL);
	assert_int_equal(rc, 0);
	print_message("success\n");

	print_message("setting DAOS_CONT_CLOSE_FAIL_CORPC ... ");
	rc = daos_mgmt_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				  DAOS_CONT_CLOSE_FAIL_CORPC | DAOS_FAIL_ONCE,
				  0, NULL);
	assert_int_equal(rc, 0);
	print_message("success\n");

	print_message("closing container ... ");
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
	print_message("success\n");

	print_message("setting DAOS_CONT_DESTROY_FAIL_CORPC ... ");
	rc = daos_mgmt_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				  DAOS_CONT_DESTROY_FAIL_CORPC | DAOS_FAIL_ONCE,
				  0, NULL);
	assert_int_equal(rc, 0);
	print_message("success\n");

	print_message("destroying container ... ");
	rc = daos_cont_destroy(arg->pool.poh, uuid, 1 /* force */, NULL);
	assert_int_equal(rc, 0);
	print_message("success\n");
}

static int
co_setup_sync(void **state)
{
	async_disable(state);
	return test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			  NULL);
}

static int
co_setup_async(void **state)
{
	async_enable(state);
	return test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			  NULL);
}

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			  NULL);
}

static const struct CMUnitTest co_tests[] = {
	{ "CONT1: create/open/close/destroy container",
	  co_create, async_disable, test_case_teardown},
	{ "CONT2: create/open/close/destroy container (async)",
	  co_create, async_enable, test_case_teardown},
	{ "CONT3: container handle local2glocal and global2local",
	  co_create, hdl_share_enable, test_case_teardown},
	{ "CONT4: set/get/list user-defined container attributes (sync)",
	  co_attribute, co_setup_sync, test_case_teardown},
	{ "CONT5: set/get/list user-defined container attributes (async)",
	  co_attribute, co_setup_async, test_case_teardown},
	{ "CONT6: create container with properties and query",
	  co_properties, NULL, test_case_teardown},
	{ "CONT7: retry CONT_{CLOSE,DESTROY,QUERY}",
	  co_op_retry, NULL, test_case_teardown}
};

int
run_daos_cont_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("DAOS container tests",
					 co_tests, setup, test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
