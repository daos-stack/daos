/**
 * (C) Copyright 2016-2020 Intel Corporation.
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

static bool
ace_has_permissions(struct daos_ace *ace, uint64_t exp_perms)
{
	if (ace->dae_access_types != DAOS_ACL_ACCESS_ALLOW) {
		print_message("Expected access type allow for ACE\n");
		daos_ace_dump(ace, 0);
		return false;
	}

	if (ace->dae_allow_perms != exp_perms) {
		print_message("ACE had perms: 0x%lx (expected: 0x%lx)\n",
			      ace->dae_allow_perms, exp_perms);
		daos_ace_dump(ace, 0);
		return false;
	}

	return true;
}

static bool
is_acl_prop_default(struct daos_acl *prop)
{
	struct daos_ace *ace;
	ssize_t		acl_expected_len = 0;

	if (daos_acl_validate(prop) != 0) {
		print_message("ACL property not valid\n");
		daos_acl_dump(prop);
		return false;
	}

	if (daos_acl_get_ace_for_principal(prop, DAOS_ACL_OWNER,
					   NULL, &ace) != 0) {
		print_message("Owner ACE not found\n");
		return false;
	}

	acl_expected_len += daos_ace_get_size(ace);

	/* Owner should have full control of the container by default */
	if (!ace_has_permissions(ace, DAOS_ACL_PERM_CONT_ALL)) {
		print_message("Owner ACE was wrong\n");
		return false;
	}

	if (daos_acl_get_ace_for_principal(prop, DAOS_ACL_OWNER_GROUP,
					   NULL, &ace) != 0) {
		print_message("Owner Group ACE not found\n");
		return false;
	}

	acl_expected_len += daos_ace_get_size(ace);

	/* Owner-group should have basic access */
	if (!ace_has_permissions(ace,
				 DAOS_ACL_PERM_READ |
				 DAOS_ACL_PERM_WRITE |
				 DAOS_ACL_PERM_GET_PROP |
				 DAOS_ACL_PERM_SET_PROP)) {
		print_message("Owner Group ACE was wrong\n");
		return false;
	}

	if (prop->dal_len != acl_expected_len) {
		print_message("More ACEs in list than expected, expected len = "
			      "%ld, actual len = %u\n", acl_expected_len,
			      prop->dal_len);
		return false;
	}

	print_message("ACL prop matches expected defaults\n");
	return true;
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

	const int prop_count = 9;

	prop_query = daos_prop_alloc(prop_count);
	prop_query->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	prop_query->dpp_entries[1].dpe_type = DAOS_PROP_CO_CSUM;
	prop_query->dpp_entries[2].dpe_type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
	prop_query->dpp_entries[3].dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	prop_query->dpp_entries[4].dpe_type = DAOS_PROP_CO_ENCRYPT;
	prop_query->dpp_entries[5].dpe_type = DAOS_PROP_CO_SNAPSHOT_MAX;
	prop_query->dpp_entries[6].dpe_type = DAOS_PROP_CO_ACL;
	prop_query->dpp_entries[7].dpe_type = DAOS_PROP_CO_OWNER;
	prop_query->dpp_entries[8].dpe_type = DAOS_PROP_CO_OWNER_GROUP;
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

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_ACL);
	if (entry == NULL || entry->dpe_val_ptr == NULL ||
	    !is_acl_prop_default((struct daos_acl *)entry->dpe_val_ptr)) {
		print_message("ACL prop verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	/* default owner */
	print_message("Checking owner set to default\n");
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_OWNER);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, "NOBODY@",
		    DAOS_ACL_MAX_PRINCIPAL_LEN)) {
		print_message("Owner prop verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	/* default owner-group */
	print_message("Checking owner-group set to default\n");
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_OWNER_GROUP);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, "NOBODY@",
		    DAOS_ACL_MAX_PRINCIPAL_LEN)) {
		print_message("Owner-group prop verification failed.\n");
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

static void
co_acl_get(test_arg_t *arg, struct daos_acl *exp_acl,
	   const char *exp_owner, const char *exp_owner_grp)
{
	int			rc;
	daos_prop_t		*acl_prop = NULL;
	struct daos_prop_entry	*entry;
	struct daos_acl		*actual_acl;

	print_message("Getting the container ACL\n");
	rc = daos_cont_get_acl(arg->coh, &acl_prop, NULL);
	assert_int_equal(rc, 0);

	assert_non_null(acl_prop);
	assert_int_equal(acl_prop->dpp_nr, 3);

	print_message("Checking ACL\n");
	entry = daos_prop_entry_get(acl_prop, DAOS_PROP_CO_ACL);
	if (entry == NULL || entry->dpe_val_ptr == NULL) {
		print_message("ACL prop wasn't returned.\n");
		assert_false(true); /* fail the test */
	}
	actual_acl = entry->dpe_val_ptr;
	assert_int_equal(actual_acl->dal_ver, exp_acl->dal_ver);
	assert_int_equal(actual_acl->dal_len, exp_acl->dal_len);
	assert_memory_equal(actual_acl->dal_ace, exp_acl->dal_ace,
			    exp_acl->dal_len);

	print_message("Checking owner\n");
	entry = daos_prop_entry_get(acl_prop, DAOS_PROP_CO_OWNER);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, exp_owner,
		    DAOS_ACL_MAX_PRINCIPAL_LEN)) {
		print_message("Owner prop verification failed.\n");
		assert_false(true); /* fail the test */
	}

	print_message("Checking owner-group\n");
	entry = daos_prop_entry_get(acl_prop, DAOS_PROP_CO_OWNER_GROUP);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, exp_owner_grp,
		    DAOS_ACL_MAX_PRINCIPAL_LEN)) {
		print_message("Owner-group prop verification failed.\n");
		assert_false(true); /* fail the test */
	}

	daos_prop_free(acl_prop);
}

static void
add_ace_with_perms(struct daos_acl **acl, enum daos_acl_principal_type type,
		   const char *name, uint64_t perms)
{
	struct daos_ace *ace;
	int		rc;

	ace = daos_ace_create(type, name);
	assert_non_null(ace);
	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms = perms;

	rc = daos_acl_add_ace(acl, ace);
	assert_int_equal(rc, 0);

	daos_ace_free(ace);
}

static void
co_acl(void **state)
{
	test_arg_t		*arg0 = *state;
	test_arg_t		*arg = NULL;
	daos_prop_t		*prop_in;
	daos_pool_info_t	 info = {0};
	int			 rc;
	const char		*exp_owner = "fictionaluser@";
	const char		*exp_owner_grp = "admins@";
	struct daos_acl		*exp_acl;
	struct daos_ace		*ace;
	uid_t			uid;
	char			*user;

	print_message("create container with access props, and verify.\n");
	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT, arg0->multi_rank,
			DEFAULT_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	print_message("Case 1: initial non-default ACL/ownership\n");
	/*
	 * Want to set up with a non-default ACL and owner/group.
	 * This ACL gives the effective user permissions to interact
	 * with the ACL. This is the bare minimum required to run the tests.
	 */
	uid = geteuid();
	rc = daos_acl_uid_to_principal(uid, &user);
	assert_int_equal(rc, 0);
	assert_non_null(user);

	exp_acl = daos_acl_create(NULL, 0);
	assert_non_null(exp_acl);

	add_ace_with_perms(&exp_acl, DAOS_ACL_USER, user,
			   DAOS_ACL_PERM_GET_ACL | DAOS_ACL_PERM_SET_ACL);
	add_ace_with_perms(&exp_acl, DAOS_ACL_EVERYONE, NULL,
			   DAOS_ACL_PERM_READ);
	assert_int_equal(daos_acl_cont_validate(exp_acl), 0);

	/*
	 * Set up the container with non-default owner/group and ACL values
	 */
	prop_in = daos_prop_alloc(3);
	assert_non_null(prop_in);
	prop_in->dpp_entries[0].dpe_type = DAOS_PROP_CO_OWNER;
	D_STRNDUP(prop_in->dpp_entries[0].dpe_str, exp_owner,
		  DAOS_ACL_MAX_PRINCIPAL_BUF_LEN);
	prop_in->dpp_entries[1].dpe_type = DAOS_PROP_CO_OWNER_GROUP;
	D_STRNDUP(prop_in->dpp_entries[1].dpe_str, exp_owner_grp,
		  DAOS_ACL_MAX_PRINCIPAL_BUF_LEN);
	prop_in->dpp_entries[2].dpe_type = DAOS_PROP_CO_ACL;
	prop_in->dpp_entries[2].dpe_val_ptr = daos_acl_dup(exp_acl);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, prop_in);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL, NULL);
		assert_int_equal(rc, 0);
		rc = daos_mgmt_set_params(arg->group, info.pi_leader,
			DMG_KEY_FAIL_LOC, DAOS_FORCE_PROP_VERIFY, 0, NULL);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	co_acl_get(arg, exp_acl, exp_owner, exp_owner_grp);

	print_message("Case 2: overwrite ACL\n");
	/*
	 * Modify the existing ACL - don't want to clobber the user entry
	 * though.
	 */
	rc = daos_acl_remove_ace(&exp_acl, DAOS_ACL_EVERYONE, NULL);
	assert_int_equal(rc, 0);

	add_ace_with_perms(&exp_acl, DAOS_ACL_OWNER, NULL,
			   DAOS_ACL_PERM_GET_PROP | DAOS_ACL_PERM_SET_PROP |
			   DAOS_ACL_PERM_DEL_CONT);
	add_ace_with_perms(&exp_acl, DAOS_ACL_GROUP, "testgroup@",
			   DAOS_ACL_PERM_GET_PROP | DAOS_ACL_PERM_READ |
			   DAOS_ACL_PERM_WRITE | DAOS_ACL_PERM_DEL_CONT);
	add_ace_with_perms(&exp_acl, DAOS_ACL_GROUP, "testgroup2@",
			   DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE);

	rc = daos_acl_get_ace_for_principal(exp_acl, DAOS_ACL_USER, user, &ace);
	assert_int_equal(rc, 0);
	ace->dae_allow_perms |= DAOS_ACL_PERM_SET_OWNER;

	assert_int_equal(daos_acl_cont_validate(exp_acl), 0);

	rc = daos_cont_overwrite_acl(arg->coh, exp_acl, NULL);
	assert_int_equal(rc, 0);

	co_acl_get(arg, exp_acl, exp_owner, exp_owner_grp);

	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	daos_prop_free(prop_in);
	daos_acl_free(exp_acl);
	D_FREE(user);
	test_teardown((void **)&arg);
}

static void
co_set_prop(void **state)
{
	test_arg_t		*arg0 = *state;
	test_arg_t		*arg = NULL;
	daos_prop_t		*prop_in;
	daos_prop_t		*prop_out = NULL;
	struct daos_prop_entry	*entry;
	int			 rc;
	const char		*exp_label = "NEW_FANCY_LABEL";
	const char		*exp_owner = "wonderfuluser@wonderfuldomain";

	print_message("create container with default props and modify them.\n");
	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT, arg0->multi_rank,
			DEFAULT_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, NULL);
	assert_int_equal(rc, 0);

	MPI_Barrier(MPI_COMM_WORLD);

	/*
	 * Set some props
	 */
	prop_in = daos_prop_alloc(2);
	assert_non_null(prop_in);
	prop_in->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP(prop_in->dpp_entries[0].dpe_str, exp_label,
		  DAOS_PROP_LABEL_MAX_LEN);
	prop_in->dpp_entries[1].dpe_type = DAOS_PROP_CO_OWNER;
	D_STRNDUP(prop_in->dpp_entries[1].dpe_str, exp_owner,
		  DAOS_ACL_MAX_PRINCIPAL_LEN);

	print_message("Setting the container props\n");
	rc = daos_cont_set_prop(arg->coh, prop_in, NULL);
	assert_int_equal(rc, 0);

	print_message("Querying all container props\n");
	prop_out = daos_prop_alloc(0);
	assert_non_null(prop_out);
	rc = daos_cont_query(arg->coh, NULL, prop_out, NULL);
	assert_int_equal(rc, 0);

	assert_non_null(prop_out->dpp_entries);
	assert_true(prop_out->dpp_nr >= prop_in->dpp_nr);

	print_message("Checking label\n");
	entry = daos_prop_entry_get(prop_out, DAOS_PROP_CO_LABEL);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, exp_label,
		    DAOS_PROP_LABEL_MAX_LEN)) {
		print_message("Label prop verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	print_message("Checking owner\n");
	entry = daos_prop_entry_get(prop_out, DAOS_PROP_CO_OWNER);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, exp_owner,
		    DAOS_ACL_MAX_PRINCIPAL_LEN)) {
		print_message("Owner prop verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	MPI_Barrier(MPI_COMM_WORLD);

	daos_prop_free(prop_in);
	daos_prop_free(prop_out);
	test_teardown((void **)&arg);
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
	  co_op_retry, NULL, test_case_teardown},
	{ "CONT8: get/set container ACL",
	  co_acl, NULL, test_case_teardown},
	{ "CONT9: container set prop",
	  co_set_prop, NULL, test_case_teardown},
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
