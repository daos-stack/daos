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

#define TEST_MAX_ATTR_LEN	(128)

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

	rc = daos_cont_del_attr(arg->coh, n, names, arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying all attributes deletion\n");
	total_size = 0;
	rc = daos_cont_list_attr(arg->coh, NULL, &total_size,
				 arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_int_equal(total_size, 0);

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

static daos_prop_t *
get_query_prop_all(void)
{
	daos_prop_t	*prop;
	const int	prop_count = DAOS_PROP_CO_NUM;
	int		i;

	prop = daos_prop_alloc(prop_count);
	assert_non_null(prop);

	for (i = 0; i < prop_count; i++) {
		prop->dpp_entries[i].dpe_type = DAOS_PROP_CO_MIN + 1 + i;
		assert_true(prop->dpp_entries[i].dpe_type < DAOS_PROP_CO_MAX);
	}

	return prop;
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
	char			*exp_owner;
	char			*exp_owner_grp;

	print_message("create container with properties, and query/verify.\n");
	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
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

	prop_query = get_query_prop_all();
	rc = daos_cont_query(arg->coh, NULL, prop_query, NULL);
	assert_int_equal(rc, 0);

	assert_int_equal(prop_query->dpp_nr, DAOS_PROP_CO_NUM);
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
	assert_int_equal(daos_acl_uid_to_principal(geteuid(), &exp_owner), 0);
	print_message("Checking owner set to default\n");
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_OWNER);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, exp_owner, DAOS_ACL_MAX_PRINCIPAL_LEN)) {
		print_message("Owner prop verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	D_FREE(exp_owner);

	/* default owner-group */
	assert_int_equal(daos_acl_gid_to_principal(getegid(), &exp_owner_grp),
			 0);
	print_message("Checking owner-group set to default\n");
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_OWNER_GROUP);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, exp_owner_grp,
		    DAOS_ACL_MAX_PRINCIPAL_LEN)) {
		print_message("Owner-group prop verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	D_FREE(exp_owner_grp);

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

static char *
get_current_user_name(void)
{
	uid_t	uid;
	int	rc;
	char	*user = NULL;

	uid = geteuid();
	rc = daos_acl_uid_to_principal(uid, &user);
	assert_int_equal(rc, 0);
	assert_non_null(user);

	return user;
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
	struct daos_acl		*update_acl;
	struct daos_ace		*ace;
	char			*user;
	d_string_t		 name_to_remove = "friendlyuser@";
	uint8_t			 type_to_remove = DAOS_ACL_USER;

	print_message("create container with access props, and verify.\n");
	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	print_message("Case 1: initial non-default ACL/ownership\n");
	/*
	 * Want to set up with a non-default ACL and owner/group.
	 * This ACL gives the effective user permissions to interact
	 * with the ACL. This is the bare minimum required to run the tests.
	 */
	user = get_current_user_name();

	print_message("Creating ACL with entry for user %s\n", user);

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

	print_message("Case 3: update ACL\n");

	/* Add one new entry and update an entry already in our ACL */
	update_acl = daos_acl_create(NULL, 0);
	add_ace_with_perms(&update_acl, DAOS_ACL_USER, "friendlyuser@",
			   DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE);
	add_ace_with_perms(&update_acl, DAOS_ACL_GROUP, "testgroup2@",
			   DAOS_ACL_PERM_READ);

	assert_int_equal(daos_acl_cont_validate(update_acl), 0);

	/* Update expected ACL to include changes */
	ace = daos_acl_get_next_ace(update_acl, NULL);
	while (ace != NULL) {
		assert_int_equal(daos_acl_add_ace(&exp_acl, ace), 0);
		ace = daos_acl_get_next_ace(update_acl, ace);
	}

	rc = daos_cont_update_acl(arg->coh, update_acl, NULL);
	assert_int_equal(rc, 0);

	co_acl_get(arg, exp_acl, exp_owner, exp_owner_grp);

	print_message("Case 4: delete entry from ACL with bad handle\n");
	rc = daos_cont_delete_acl(DAOS_HDL_INVAL, type_to_remove,
				  name_to_remove, NULL);
	assert_int_equal(rc, -DER_NO_HDL);

	print_message("Case 5: delete entry from ACL\n");

	/* Update expected ACL to remove the entry */
	assert_int_equal(daos_acl_remove_ace(&exp_acl, type_to_remove,
					     name_to_remove), 0);

	rc = daos_cont_delete_acl(arg->coh, type_to_remove, name_to_remove,
				  NULL);
	assert_int_equal(rc, 0);

	co_acl_get(arg, exp_acl, exp_owner, exp_owner_grp);

	print_message("Case 6: delete entry no longer in ACL\n");

	/* try deleting same entry again - should be gone */
	rc = daos_cont_delete_acl(arg->coh, type_to_remove, name_to_remove,
				  NULL);
	assert_int_equal(rc, -DER_NONEXIST);

	/* Clean up */
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);

	daos_prop_free(prop_in);
	daos_acl_free(exp_acl);
	daos_acl_free(update_acl);
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
			SMALL_POOL_SIZE, NULL);
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

static void
co_create_access_denied(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	daos_prop_t	*prop;
	int		 rc;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	print_message("Try to create container on pool with no create perms\n");

	/* on the pool, write is an alias for create+del cont */
	prop = get_daos_prop_with_owner_acl_perms(DAOS_ACL_PERM_POOL_ALL &
						  ~DAOS_ACL_PERM_CREATE_CONT &
						  ~DAOS_ACL_PERM_WRITE,
						  DAOS_PROP_PO_ACL);

	while (!rc && arg->setup_state != SETUP_POOL_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, prop, NULL);

	if (arg->myrank == 0) {
		uuid_generate(arg->co_uuid);
		rc = daos_cont_create(arg->pool.poh, arg->co_uuid, NULL, NULL);
		assert_int_equal(rc, -DER_NO_PERM);

		/*
		 * Clear the UUID to avoid attempts to destroy, since it wasn't
		 * created
		 */
		uuid_clear(arg->co_uuid);
	}

	uuid_clear(arg->co_uuid); /* wasn't actually created */

	daos_prop_free(prop);
	test_teardown((void **)&arg);
}

static void
co_destroy_access_denied(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	daos_prop_t	*pool_prop;
	daos_prop_t	*cont_prop;
	int		 rc;
	struct daos_acl	*cont_acl = NULL;
	struct daos_ace	*update_ace;
	daos_handle_t	coh;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	/*
	 * Pool doesn't give the owner delete cont privs. For the pool, write
	 * is an alias for create+del container.
	 */
	pool_prop = get_daos_prop_with_owner_acl_perms(DAOS_ACL_PERM_POOL_ALL &
						       ~DAOS_ACL_PERM_DEL_CONT &
						       ~DAOS_ACL_PERM_WRITE,
						       DAOS_PROP_PO_ACL);

	/* container doesn't give delete privs to the owner */
	cont_prop = get_daos_prop_with_owner_acl_perms(DAOS_ACL_PERM_CONT_ALL &
						       ~DAOS_ACL_PERM_DEL_CONT,
						       DAOS_PROP_CO_ACL);

	while (!rc && arg->setup_state != SETUP_CONT_CREATE)
		rc = test_setup_next_step((void **)&arg, NULL, pool_prop,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		print_message("Try to delete container where pool and cont "
			      "deny access\n");
		rc = daos_cont_destroy(arg->pool.poh, arg->co_uuid, 1, NULL);
		assert_int_equal(rc, -DER_NO_PERM);

		print_message("Delete with privs from container ACL only\n");

		cont_acl = daos_acl_dup(cont_prop->dpp_entries[0].dpe_val_ptr);
		assert_non_null(cont_acl);
		rc = daos_acl_get_ace_for_principal(cont_acl, DAOS_ACL_OWNER,
						    NULL,
						    &update_ace);
		assert_int_equal(rc, 0);
		update_ace->dae_allow_perms = DAOS_ACL_PERM_CONT_ALL;

		print_message("- getting container handle\n");
		rc = daos_cont_open(arg->pool.poh, arg->co_uuid, DAOS_COO_RW,
				    &coh, NULL, NULL);
		assert_int_equal(rc, 0);

		print_message("- updating cont ACL to restore delete privs\n");
		rc = daos_cont_update_acl(coh, cont_acl, NULL);
		assert_int_equal(rc, 0);

		print_message("- closing container\n");
		rc = daos_cont_close(coh, NULL);
		assert_int_equal(rc, 0);

		print_message("Deleting container now should succeed\n");
		rc = daos_cont_destroy(arg->pool.poh, arg->co_uuid, 1, NULL);
		assert_int_equal(rc, 0);

		/* Clear cont uuid since we already deleted it */
		uuid_clear(arg->co_uuid);
	}

	daos_acl_free(cont_acl);
	daos_prop_free(pool_prop);
	daos_prop_free(cont_prop);
	test_teardown((void **)&arg);
}

static void
co_destroy_allowed_by_pool(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	daos_prop_t	*pool_prop;
	daos_prop_t	*cont_prop;
	int		 rc;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	/* pool gives the owner all privs, including delete cont */
	pool_prop = get_daos_prop_with_owner_acl_perms(DAOS_ACL_PERM_POOL_ALL,
						       DAOS_PROP_PO_ACL);

	/* container doesn't give delete privs to the owner */
	cont_prop = get_daos_prop_with_owner_acl_perms(DAOS_ACL_PERM_CONT_ALL &
						       ~DAOS_ACL_PERM_DEL_CONT,
						       DAOS_PROP_CO_ACL);

	while (!rc && arg->setup_state != SETUP_CONT_CREATE)
		rc = test_setup_next_step((void **)&arg, NULL, pool_prop,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		print_message("Deleting container with only pool-level "
			      "perms\n");
		rc = daos_cont_destroy(arg->pool.poh, arg->co_uuid, 1, NULL);
		assert_int_equal(rc, 0);

		/* Clear cont uuid since we already deleted it */
		uuid_clear(arg->co_uuid);
	}

	daos_prop_free(pool_prop);
	daos_prop_free(cont_prop);
	test_teardown((void **)&arg);
}

static void
expect_cont_open_access(test_arg_t *arg, uint64_t perms, uint64_t flags,
			int exp_result)
{
	daos_prop_t	*prop;
	int		 rc = 0;

	arg->cont_open_flags = flags;
	prop = get_daos_prop_with_user_acl_perms(perms);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, prop);

	if (arg->myrank == 0) {
		/* Make sure we actually got to the container open step */
		assert_int_equal(arg->setup_state, SETUP_CONT_CONNECT);
		assert_int_equal(rc, exp_result);
	}

	/* Cleanup */
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
	daos_prop_free(prop);
}

static void
co_open_access(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	int		rc;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	print_message("cont ACL gives the user no permissions\n");
	expect_cont_open_access(arg, 0, DAOS_COO_RO, -DER_NO_PERM);

	print_message("cont ACL gives the user RO, they want RW\n");
	expect_cont_open_access(arg, DAOS_ACL_PERM_READ, DAOS_COO_RW,
				   -DER_NO_PERM);

	print_message("cont ACL gives the user RO, they want RO\n");
	expect_cont_open_access(arg, DAOS_ACL_PERM_READ, DAOS_COO_RO,
				   0);

	print_message("cont ACL gives the user RW, they want RO\n");
	expect_cont_open_access(arg,
				   DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				   DAOS_COO_RO,
				   0);

	print_message("cont ACL gives the user RW, they want RW\n");
	expect_cont_open_access(arg,
				   DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				   DAOS_COO_RW,
				   0);

	test_teardown((void **)&arg);
}

static void
expect_co_query_access(test_arg_t *arg, daos_prop_t *query_prop,
		       uint64_t perms, int exp_result)
{
	daos_prop_t		*cont_prop;
	daos_cont_info_t	 info;
	int			 rc = 0;

	cont_prop = get_daos_prop_with_user_acl_perms(perms);

	arg->cont_open_flags = DAOS_COO_RO;
	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = daos_cont_query(arg->coh, &info, query_prop, NULL);
		assert_int_equal(rc, exp_result);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static daos_prop_t *
get_single_query_prop(uint32_t type)
{
	daos_prop_t	*prop;

	prop = daos_prop_alloc(1);
	assert_non_null(prop);

	prop->dpp_entries[0].dpe_type = type;

	return prop;
}

static void
co_query_access(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	daos_prop_t	*prop;
	int		rc;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	print_message("Not asking for any props\n");
	expect_co_query_access(arg, NULL,
			       DAOS_ACL_PERM_CONT_ALL &
			       ~DAOS_ACL_PERM_GET_PROP &
			       ~DAOS_ACL_PERM_GET_ACL,
			       -0);

	print_message("Empty prop object (all props), but no get-prop\n");
	prop = daos_prop_alloc(0);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL & ~DAOS_ACL_PERM_GET_PROP,
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Empty prop object (all props), but no get-ACL\n");
	prop = daos_prop_alloc(0);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL & ~DAOS_ACL_PERM_GET_ACL,
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Empty prop object (all props), with access\n");
	prop = daos_prop_alloc(0);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_PROP | DAOS_ACL_PERM_GET_ACL,
			       0);
	daos_prop_free(prop);

	print_message("All props with no get-prop access\n");
	prop = get_query_prop_all();
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL & ~DAOS_ACL_PERM_GET_PROP,
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("All props with no get-ACL access\n");
	prop = get_query_prop_all();
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL & ~DAOS_ACL_PERM_GET_ACL,
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("All props with only prop and ACL access\n");
	prop = get_query_prop_all();
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_PROP | DAOS_ACL_PERM_GET_ACL,
			       0);
	daos_prop_free(prop);

	/*
	 * ACL props can only be accessed by users with get-ACL permission
	 */
	print_message("ACL prop with no get-ACL access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_ACL);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL & ~DAOS_ACL_PERM_GET_ACL,
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("ACL prop with only get-ACL access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_ACL);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_ACL,
			       0);
	daos_prop_free(prop);

	/*
	 * Props unrelated to access/ACLs can only be accessed by users with
	 * the get-prop permission
	 */
	print_message("Non-access-related prop with no get-prop access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_LABEL);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL & ~DAOS_ACL_PERM_GET_PROP,
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Non-access-related prop with only prop access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_LABEL);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_PROP,
			       0);
	daos_prop_free(prop);

	/*
	 * Ownership props can be accessed by users with either get-prop or
	 * get-acl access
	 */
	print_message("Owner with only prop access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_OWNER);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_PROP,
			       0);
	daos_prop_free(prop);

	print_message("Owner with only ACL access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_OWNER);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_ACL,
			       0);
	daos_prop_free(prop);

	print_message("Owner with neither get-prop nor get-acl access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_OWNER);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL &
			       ~(DAOS_ACL_PERM_GET_PROP |
				 DAOS_ACL_PERM_GET_ACL),
			       -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Owner-group with only prop access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_OWNER_GROUP);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_PROP,
			       0);
	daos_prop_free(prop);

	print_message("Owner-group with only ACL access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_OWNER_GROUP);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_GET_ACL,
			       0);
	daos_prop_free(prop);

	print_message("Owner-group with no get-prop or get-acl access\n");
	prop = get_single_query_prop(DAOS_PROP_CO_OWNER_GROUP);
	expect_co_query_access(arg, prop,
			       DAOS_ACL_PERM_CONT_ALL &
			       ~(DAOS_ACL_PERM_GET_PROP |
				 DAOS_ACL_PERM_GET_ACL),
			       -DER_NO_PERM);
	daos_prop_free(prop);

	test_teardown((void **)&arg);
}

static void
expect_co_get_acl_access(test_arg_t *arg, uint64_t perms, int exp_result)
{
	daos_prop_t		*cont_prop;
	daos_prop_t		*acl_prop;
	int			 rc = 0;

	cont_prop = get_daos_prop_with_user_acl_perms(perms);

	arg->cont_open_flags = DAOS_COO_RO;
	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = daos_cont_get_acl(arg->coh, &acl_prop, NULL);
		assert_int_equal(rc, exp_result);

		if (rc == 0)
			daos_prop_free(acl_prop);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
co_get_acl_access(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	int		rc;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	print_message("No get-ACL permissions\n");
	expect_co_get_acl_access(arg,
				 DAOS_ACL_PERM_CONT_ALL &
				 ~DAOS_ACL_PERM_GET_ACL,
				 -DER_NO_PERM);

	print_message("Only get-ACL permissions\n");
	expect_co_get_acl_access(arg, DAOS_ACL_PERM_GET_ACL, 0);

	test_teardown((void **)&arg);
}

static void
expect_co_set_prop_access(test_arg_t *arg, daos_prop_t *prop, uint64_t perms,
			  int exp_result)
{
	daos_prop_t	*cont_prop;
	int		 rc = 0;

	cont_prop = get_daos_prop_with_user_acl_perms(perms);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = daos_cont_set_prop(arg->coh, prop, NULL);
		assert_int_equal(rc, exp_result);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
setup_str_prop_entry(struct daos_prop_entry *entry, uint32_t type,
		     const char *val)
{
	entry->dpe_type = type;
	D_STRNDUP(entry->dpe_str, val, DAOS_ACL_MAX_PRINCIPAL_LEN);
}

static daos_prop_t *
get_daos_prop_with_owner(const char *user, const char *group)
{
	uint32_t	nr = 0;
	uint32_t	i = 0;
	daos_prop_t	*prop;

	if (user != NULL)
		nr++;
	if (group != NULL)
		nr++;

	assert_true(nr > 0); /* test error! */

	prop = daos_prop_alloc(nr);
	assert_non_null(prop);

	if (user != NULL) {
		setup_str_prop_entry(&prop->dpp_entries[i], DAOS_PROP_CO_OWNER,
				     user);
		i++;
	}

	if (group != NULL) {
		setup_str_prop_entry(&prop->dpp_entries[i],
				     DAOS_PROP_CO_OWNER_GROUP, group);
		i++;
	}

	return prop;
}

static daos_prop_t *
get_daos_prop_with_label(void)
{
	daos_prop_t	*prop;

	prop = daos_prop_alloc(1);
	assert_non_null(prop);

	setup_str_prop_entry(&prop->dpp_entries[0], DAOS_PROP_CO_LABEL,
			     "My container");

	return prop;
}

static daos_prop_t *
get_daos_prop_with_all_prop_categories(void)
{
	daos_prop_t	*prop;
	struct daos_acl	*acl;

	prop = daos_prop_alloc(4);
	assert_non_null(prop);

	setup_str_prop_entry(&prop->dpp_entries[0], DAOS_PROP_CO_LABEL,
			     "Container 1");
	setup_str_prop_entry(&prop->dpp_entries[1], DAOS_PROP_CO_OWNER,
			     "niceuser@");
	setup_str_prop_entry(&prop->dpp_entries[2], DAOS_PROP_CO_OWNER_GROUP,
			     "nicegroup@");

	acl = get_daos_acl_with_owner_perms(DAOS_ACL_PERM_CONT_ALL);
	prop->dpp_entries[3].dpe_type = DAOS_PROP_CO_ACL;
	prop->dpp_entries[3].dpe_val_ptr = acl;

	return prop;
}

static void
co_set_prop_access(void **state)
{
	test_arg_t	*arg0 = *state;
	daos_prop_t	*prop;
	test_arg_t	*arg = NULL;
	int		 rc;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	/*
	 * ACL modification through set-prop only works if you have set-ACL
	 * permissions
	 */
	print_message("No set-ACL permissions\n");
	prop = get_daos_prop_with_owner_acl_perms(DAOS_ACL_PERM_CONT_ALL,
						  DAOS_PROP_CO_ACL);
	expect_co_set_prop_access(arg, prop,
				  DAOS_ACL_PERM_CONT_ALL &
				  ~DAOS_ACL_PERM_SET_ACL,
				  -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Has set-ACL permissions\n");
	prop = get_daos_prop_with_owner_acl_perms(DAOS_ACL_PERM_CONT_ALL,
						  DAOS_PROP_CO_ACL);
	expect_co_set_prop_access(arg, prop,
				  DAOS_ACL_PERM_READ |
				  DAOS_ACL_PERM_SET_ACL,
				  0);
	daos_prop_free(prop);

	/*
	 * Owner modification through set-prop only works if you have set-owner
	 * permissions
	 */
	print_message("Set owner only, with no set-owner perms\n");
	prop = get_daos_prop_with_owner("someuser@", NULL);
	expect_co_set_prop_access(arg, prop,
				  DAOS_ACL_PERM_CONT_ALL &
				  ~DAOS_ACL_PERM_SET_OWNER,
				  -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Set owner-group only, with no set-owner perms\n");
	prop = get_daos_prop_with_owner(NULL, "somegroup@");
	expect_co_set_prop_access(arg, prop,
				  DAOS_ACL_PERM_CONT_ALL &
				  ~DAOS_ACL_PERM_SET_OWNER,
				  -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Set both owner and group, with no set-owner perms\n");
	prop = get_daos_prop_with_owner("someuser@", "somegroup@");
	expect_co_set_prop_access(arg, prop,
				  DAOS_ACL_PERM_CONT_ALL &
				  ~DAOS_ACL_PERM_SET_OWNER,
				  -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Set both owner and group, with set-owner perms\n");
	prop = get_daos_prop_with_owner("someuser@", "somegroup@");
	expect_co_set_prop_access(arg, prop,
				  DAOS_ACL_PERM_READ | DAOS_ACL_PERM_SET_OWNER,
				  0);
	daos_prop_free(prop);

	/*
	 * Setting regular props requires set-prop permission
	 */
	print_message("Set label, with no set-prop perms\n");
	prop = get_daos_prop_with_label();
	expect_co_set_prop_access(arg, prop,
				  DAOS_ACL_PERM_CONT_ALL &
				  ~DAOS_ACL_PERM_SET_PROP,
				  -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Set label, with set-prop perms\n");
	prop = get_daos_prop_with_label();
	expect_co_set_prop_access(arg, prop,
				  DAOS_ACL_PERM_READ | DAOS_ACL_PERM_SET_PROP,
				  0);
	daos_prop_free(prop);

	/*
	 * Set all three categories requires all three permissions
	 */
	print_message("Set multiple, with no set-prop perms\n");
	prop = get_daos_prop_with_all_prop_categories();
	expect_co_set_prop_access(arg, prop,
				  DAOS_ACL_PERM_CONT_ALL &
				  ~DAOS_ACL_PERM_SET_PROP,
				  -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Set multiple, with no set-owner perms\n");
	prop = get_daos_prop_with_all_prop_categories();
	expect_co_set_prop_access(arg, prop,
				  DAOS_ACL_PERM_CONT_ALL &
				  ~DAOS_ACL_PERM_SET_OWNER,
				  -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Set multiple, with no set-ACL perms\n");
	prop = get_daos_prop_with_all_prop_categories();
	expect_co_set_prop_access(arg, prop,
				  DAOS_ACL_PERM_CONT_ALL &
				  ~DAOS_ACL_PERM_SET_OWNER,
				  -DER_NO_PERM);
	daos_prop_free(prop);

	print_message("Set multiple, with all required perms\n");
	prop = get_daos_prop_with_all_prop_categories();
	expect_co_set_prop_access(arg, prop,
				  DAOS_ACL_PERM_READ |
				  DAOS_ACL_PERM_SET_PROP |
				  DAOS_ACL_PERM_SET_OWNER |
				  DAOS_ACL_PERM_SET_ACL,
				  0);
	daos_prop_free(prop);

	test_teardown((void **)&arg);
}

static void
expect_co_overwrite_acl_access(test_arg_t *arg, uint64_t perms, int exp_result)
{
	daos_prop_t	*cont_prop;
	struct daos_acl	*acl = NULL;
	int		 rc = 0;

	cont_prop = get_daos_prop_with_user_acl_perms(perms);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		acl = get_daos_acl_with_owner_perms(DAOS_ACL_PERM_CONT_ALL);

		rc = daos_cont_overwrite_acl(arg->coh, acl, NULL);
		assert_int_equal(rc, exp_result);

		daos_acl_free(acl);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
expect_co_update_acl_access(test_arg_t *arg, uint64_t perms, int exp_result)
{
	daos_prop_t	*cont_prop;
	struct daos_acl	*acl = NULL;
	int		 rc = 0;

	cont_prop = get_daos_prop_with_user_acl_perms(perms);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		acl = get_daos_acl_with_owner_perms(DAOS_ACL_PERM_CONT_ALL);

		rc = daos_cont_update_acl(arg->coh, acl, NULL);
		assert_int_equal(rc, exp_result);

		daos_acl_free(acl);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
expect_co_delete_acl_access(test_arg_t *arg, uint64_t perms, int exp_result)
{
	daos_prop_t	*cont_prop;
	int		 rc = 0;

	cont_prop = get_daos_prop_with_user_acl_perms(perms);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = daos_cont_delete_acl(arg->coh, DAOS_ACL_OWNER, NULL, NULL);
		assert_int_equal(rc, exp_result);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
co_modify_acl_access(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	int		 rc;
	uint64_t	 no_set_acl_perm = DAOS_ACL_PERM_CONT_ALL &
					   ~DAOS_ACL_PERM_SET_ACL;
	uint64_t	 min_set_acl_perm = DAOS_ACL_PERM_READ |
					    DAOS_ACL_PERM_SET_ACL;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	print_message("Overwrite ACL denied with no set-ACL perm\n");
	expect_co_overwrite_acl_access(arg, no_set_acl_perm,
				       -DER_NO_PERM);

	print_message("Overwrite ACL allowed with set-ACL perm\n");
	expect_co_overwrite_acl_access(arg, min_set_acl_perm,
				       0);

	print_message("Update ACL denied with no set-ACL perm\n");
	expect_co_update_acl_access(arg,
				    DAOS_ACL_PERM_CONT_ALL &
				    ~DAOS_ACL_PERM_SET_ACL,
				    -DER_NO_PERM);

	print_message("Update ACL allowed with set-ACL perm\n");
	expect_co_update_acl_access(arg,
				    DAOS_ACL_PERM_READ |
				    DAOS_ACL_PERM_SET_ACL,
				    0);

	print_message("Delete ACL denied with no set-ACL perm\n");
	expect_co_delete_acl_access(arg,
				    DAOS_ACL_PERM_CONT_ALL &
				    ~DAOS_ACL_PERM_SET_ACL,
				    -DER_NO_PERM);

	print_message("Delete ACL allowed with set-ACL perm\n");
	expect_co_delete_acl_access(arg,
				    DAOS_ACL_PERM_READ |
				    DAOS_ACL_PERM_SET_ACL,
				    0);

	test_teardown((void **)&arg);
}

static void
expect_ownership(test_arg_t *arg, d_string_t user, d_string_t grp)
{
	int			 rc;
	daos_prop_t		*prop;
	struct daos_prop_entry	*entry;

	prop = daos_prop_alloc(2);
	assert_non_null(prop);

	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_OWNER;
	prop->dpp_entries[1].dpe_type = DAOS_PROP_CO_OWNER_GROUP;

	rc = daos_cont_query(arg->coh, NULL, prop, NULL);
	assert_int_equal(rc, 0);

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_OWNER);
	assert_non_null(entry);
	assert_string_equal(entry->dpe_str, user);

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_OWNER_GROUP);
	assert_non_null(entry);
	assert_string_equal(entry->dpe_str, grp);

	daos_prop_free(prop);
}

static void
co_set_owner(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	d_string_t	 original_user;
	d_string_t	 original_grp;
	d_string_t	 new_user = "newuser@";
	d_string_t	 new_grp = "newgrp@";
	int		 rc;

	rc = test_setup((void **)&arg, SETUP_CONT_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	/*
	 * To start with, the euid/egid are the owner user/group.
	 */
	assert_int_equal(daos_acl_uid_to_principal(geteuid(), &original_user),
			 0);
	assert_int_equal(daos_acl_gid_to_principal(getegid(), &original_grp),
			 0);

	if (arg->myrank == 0) {
		print_message("Set owner with null params\n");
		rc = daos_cont_set_owner(arg->coh, NULL, NULL, NULL);
		assert_int_equal(rc, -DER_INVAL);

		print_message("Set owner with invalid user\n");
		rc = daos_cont_set_owner(arg->coh, "not_a_valid_user", new_grp,
					 NULL);
		assert_int_equal(rc, -DER_INVAL);

		print_message("Set owner with invalid grp\n");
		rc = daos_cont_set_owner(arg->coh, new_user, "not_a_valid_grp",
					 NULL);
		assert_int_equal(rc, -DER_INVAL);

		print_message("Set owner user\n");
		rc = daos_cont_set_owner(arg->coh, new_user, NULL, NULL);
		assert_int_equal(rc, 0);
		expect_ownership(arg, new_user, original_grp);

		print_message("Change owner user back\n");
		rc = daos_cont_set_owner(arg->coh, original_user, NULL, NULL);
		assert_int_equal(rc, 0);
		expect_ownership(arg, original_user, original_grp);

		print_message("Set owner group\n");
		rc = daos_cont_set_owner(arg->coh, NULL, new_grp, NULL);
		assert_int_equal(rc, 0);
		expect_ownership(arg, original_user, new_grp);

		print_message("Change owner group back\n");
		rc = daos_cont_set_owner(arg->coh, NULL, original_grp, NULL);
		assert_int_equal(rc, 0);
		expect_ownership(arg, original_user, original_grp);

		print_message("Set both owner user and group\n");
		rc = daos_cont_set_owner(arg->coh, new_user, new_grp, NULL);
		assert_int_equal(rc, 0);
		expect_ownership(arg, new_user, new_grp);
	}

	D_FREE(original_user);
	D_FREE(original_grp);
	test_teardown((void **)&arg);
}

static void
expect_co_set_owner_access(test_arg_t *arg, d_string_t user, d_string_t grp,
			   uint64_t perms, int exp_result)
{
	daos_prop_t	*cont_prop;
	int		 rc = 0;

	cont_prop = get_daos_prop_with_owner_acl_perms(perms,
						       DAOS_PROP_CO_ACL);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = daos_cont_set_owner(arg->coh, user, grp, NULL);
		assert_int_equal(rc, exp_result);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
co_set_owner_access(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	int		 rc;
	uint64_t	 no_perm = DAOS_ACL_PERM_CONT_ALL &
				   ~DAOS_ACL_PERM_SET_OWNER;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	print_message("Set owner user denied with no set-owner perm\n");
	expect_co_set_owner_access(arg, "user@", NULL, no_perm,
				   -DER_NO_PERM);

	print_message("Set owner group denied with no set-owner perm\n");
	expect_co_set_owner_access(arg, NULL, "group@", no_perm,
				   -DER_NO_PERM);

	print_message("Set both owner and grp denied with no set-owner perm\n");
	expect_co_set_owner_access(arg, "user@", "group@", no_perm,
				   -DER_NO_PERM);

	print_message("Set owner allowed with set-owner perm\n");
	expect_co_set_owner_access(arg, "user@", "group@",
				   DAOS_ACL_PERM_READ |
				   DAOS_ACL_PERM_SET_OWNER,
				   0);

	test_teardown((void **)&arg);
}

static void
co_destroy_force(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	daos_handle_t	 coh;
	daos_cont_info_t info;
	int		 rc;

	if (arg->myrank != 0)
		return;

	uuid_generate(uuid);

	print_message("creating container "DF_UUIDF"\n",
		      DP_UUID(uuid));
	rc = daos_cont_create(arg->pool.poh, uuid, NULL, NULL);
	assert_int_equal(rc, 0);

	print_message("opening container\n");
	rc = daos_cont_open(arg->pool.poh, uuid, DAOS_COO_RW, &coh,
			    &info, NULL);
	assert_int_equal(rc, 0);

	print_message("destroying container (force=false): should err\n");
	rc = daos_cont_destroy(arg->pool.poh, uuid, 0 /* force */, NULL);
	assert_int_equal(rc, -DER_BUSY);

	print_message("destroying container (force=true): should succeed\n");
	rc = daos_cont_destroy(arg->pool.poh, uuid, 1 /* force */, NULL);
	assert_int_equal(rc, 0);

	print_message("closing container: should succeed\n");
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
}

static void
co_owner_implicit_access(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	int		 rc;
	daos_prop_t	*owner_deny_prop;
	struct daos_acl	*acl;
	daos_prop_t	*tmp_prop;
	daos_prop_t	*acl_prop;

	/*
	 * An owner with no permissions still has get/set ACL access
	 * implicitly
	 */
	owner_deny_prop = get_daos_prop_with_owner_acl_perms(0,
							     DAOS_PROP_CO_ACL);

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  owner_deny_prop);
	assert_int_equal(rc, 0);

	print_message("Owner has no permissions for non-ACL access\n");

	print_message("- Verify get-prop denied\n");
	tmp_prop = daos_prop_alloc(0);
	rc = daos_cont_query(arg->coh, NULL, tmp_prop, NULL);
	assert_int_equal(rc, -DER_NO_PERM);
	daos_prop_free(tmp_prop);

	print_message("- Verify set-prop denied\n");
	tmp_prop = daos_prop_alloc(1);
	tmp_prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP(tmp_prop->dpp_entries[0].dpe_str, "My Label", 16);
	rc = daos_cont_set_prop(arg->coh, tmp_prop, NULL);
	assert_int_equal(rc, -DER_NO_PERM);
	daos_prop_free(tmp_prop);

	print_message("- Verify set-owner denied\n");
	rc = daos_cont_set_owner(arg->coh, "somebody@", "somegroup@", NULL);
	assert_int_equal(rc, -DER_NO_PERM);

	print_message("Owner has get-ACL access implicitly\n");
	rc = daos_cont_get_acl(arg->coh, &acl_prop, NULL);
	assert_int_equal(rc, 0);

	/* sanity check */
	assert_non_null(daos_prop_entry_get(acl_prop, DAOS_PROP_CO_ACL));
	assert_non_null(daos_prop_entry_get(acl_prop, DAOS_PROP_CO_OWNER));
	assert_non_null(daos_prop_entry_get(acl_prop,
					    DAOS_PROP_CO_OWNER_GROUP));
	daos_prop_free(acl_prop);

	print_message("Owner has set-ACL implicitly\n");
	/* Just a copy of the current ACL */
	acl = daos_acl_dup(owner_deny_prop->dpp_entries[0].dpe_val_ptr);

	print_message("- Verify overwrite-ACL\n");
	rc = daos_cont_overwrite_acl(arg->coh, acl, NULL);
	assert_int_equal(rc, 0);

	print_message("- Verify update-ACL\n");
	rc = daos_cont_update_acl(arg->coh, acl, NULL);
	assert_int_equal(rc, 0);

	print_message("- Verify delete-ACL\n");
	rc = daos_cont_delete_acl(arg->coh, DAOS_ACL_OWNER, NULL, NULL);
	assert_int_equal(rc, 0);

	daos_acl_free(acl);
	daos_prop_free(owner_deny_prop);
	test_teardown((void **)&arg);
}

static void
expect_co_set_attr_access(test_arg_t *arg, uint64_t perms, int exp_result)
{
	daos_prop_t	*cont_prop;
	int		 rc = 0;
	const char	*name = "AttrName";
	const char	*value = "This is the value";
	const size_t	 size = strnlen(value, TEST_MAX_ATTR_LEN);

	cont_prop = get_daos_prop_with_owner_acl_perms(perms,
						       DAOS_PROP_CO_ACL);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		/* Trivial case - just to see if we have access */
		rc = daos_cont_set_attr(arg->coh, 1, &name,
					(const void * const*)&value,
					&size,
					NULL);
		assert_int_equal(rc, exp_result);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
expect_co_get_attr_access(test_arg_t *arg, uint64_t perms, int exp_result)
{
	daos_prop_t	*cont_prop;
	int		 rc = 0;
	const char	*name = "AttrName";
	size_t		 val_size = TEST_MAX_ATTR_LEN;
	char		 value[val_size];

	cont_prop = get_daos_prop_with_owner_acl_perms(perms,
						       DAOS_PROP_CO_ACL);

	arg->cont_open_flags = DAOS_COO_RO;
	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		/* Trivial case - just to see if we have access */
		rc = daos_cont_get_attr(arg->coh, 1, &name,
					(void * const*)&value,
					&val_size,
					NULL);
		assert_int_equal(rc, exp_result);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
expect_co_list_attr_access(test_arg_t *arg, uint64_t perms, int exp_result)
{
	daos_prop_t	*cont_prop;
	int		 rc = 0;
	char		 buf[TEST_MAX_ATTR_LEN];
	size_t		 bufsize = sizeof(buf);

	cont_prop = get_daos_prop_with_owner_acl_perms(perms,
						       DAOS_PROP_CO_ACL);

	arg->cont_open_flags = DAOS_COO_RO;
	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_int_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = daos_cont_list_attr(arg->coh, buf, &bufsize, NULL);
		assert_int_equal(rc, exp_result);
	}

	daos_prop_free(cont_prop);
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
co_attribute_access(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	int		 rc;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, NULL);
	assert_int_equal(rc, 0);

	print_message("Set attr denied with no write-data perms\n");
	expect_co_set_attr_access(arg,
				  DAOS_ACL_PERM_CONT_ALL &
				  ~DAOS_ACL_PERM_WRITE,
				  -DER_NO_PERM);

	print_message("Set attr allowed with RW data access\n");
	expect_co_set_attr_access(arg, DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				  0);

	print_message("Set attr allowed with write-data access\n");
	expect_co_set_attr_access(arg, DAOS_ACL_PERM_GET_PROP |
				  DAOS_ACL_PERM_WRITE,
				  0);

	print_message("Get attr denied with no read-data perms\n");
	expect_co_get_attr_access(arg,
				  DAOS_ACL_PERM_CONT_ALL &
				  ~DAOS_ACL_PERM_READ,
				  -DER_NO_PERM);

	print_message("Get attr allowed with RW access\n");
	/* Attr isn't set, but we get past the permissions check */
	expect_co_get_attr_access(arg,
				  DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				  -DER_NONEXIST);

	print_message("Get attr allowed with RO data access\n");
	/* Attr isn't set, but we get past the permissions check */
	expect_co_get_attr_access(arg, DAOS_ACL_PERM_READ,
				  -DER_NONEXIST);

	print_message("List attr denied with no read-data perms\n");
	expect_co_list_attr_access(arg,
				   DAOS_ACL_PERM_CONT_ALL &
				   ~DAOS_ACL_PERM_READ,
				   -DER_NO_PERM);

	print_message("List attr allowed with RW access\n");
	expect_co_list_attr_access(arg,
				   DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				   0);

	print_message("List attr allowed with RO data access\n");
	expect_co_list_attr_access(arg, DAOS_ACL_PERM_READ,
				   0);

	test_teardown((void **)&arg);
}

static int
co_setup_sync(void **state)
{
	async_disable(state);
	return test_setup(state, SETUP_CONT_CONNECT, true, SMALL_POOL_SIZE,
			  NULL);
}

static int
co_setup_async(void **state)
{
	async_enable(state);
	return test_setup(state, SETUP_CONT_CONNECT, true, SMALL_POOL_SIZE,
			  NULL);
}

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CONNECT, true, SMALL_POOL_SIZE,
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
	{ "CONT10: container create access denied",
	  co_create_access_denied, NULL, test_case_teardown},
	{ "CONT11: container destroy access denied",
	  co_destroy_access_denied, NULL, test_case_teardown},
	{ "CONT12: container destroy allowed by pool ACL only",
	  co_destroy_allowed_by_pool, NULL, test_case_teardown},
	{ "CONT13: container open access by ACL",
	  co_open_access, NULL, test_case_teardown},
	{ "CONT14: container query access by ACL",
	  co_query_access, NULL, test_case_teardown},
	{ "CONT15: container get-acl access by ACL",
	  co_get_acl_access, NULL, test_case_teardown},
	{ "CONT16: container set-prop access by ACL",
	  co_set_prop_access, NULL, test_case_teardown},
	{ "CONT17: container overwrite/update/delete ACL access by ACL",
	  co_modify_acl_access, NULL, test_case_teardown},
	{ "CONT18: container set owner",
	  co_set_owner, NULL, test_case_teardown},
	{ "CONT19: container set-owner access by ACL",
	  co_set_owner_access, NULL, test_case_teardown},
	{ "CONT20: container destroy force",
	  co_destroy_force, NULL, test_case_teardown},
	{ "CONT21: container owner has implicit ACL access",
	  co_owner_implicit_access, NULL, test_case_teardown},
	{ "CONT22: container get/set attribute access by ACL",
	  co_attribute_access, NULL, test_case_teardown},
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
