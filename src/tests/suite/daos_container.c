/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/container.c
 */
#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"
#include "daos_iotest.h"
#include <daos/placement.h>
#include <pwd.h>
#include <grp.h>

#define TEST_MAX_ATTR_LEN	(128)

/** create/destroy container */
static void
co_create(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	daos_handle_t	 coh;
	daos_handle_t	 poh_inval = DAOS_HDL_INVAL;
	daos_cont_info_t info;
	daos_event_t	 ev;
	char		 str[37];
	int		 rc;

	if (!arg->hdl_share && arg->myrank != 0)
		return;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}

	/** create container */
	if (arg->myrank == 0) {
		print_message("creating container %ssynchronously ...\n",
			      arg->async ? "a" : "");
		rc = daos_cont_create(arg->pool.poh, &uuid, NULL,
				      arg->async ? &ev : NULL);
		assert_rc_equal(rc, 0);
		WAIT_ON_ASYNC(arg, ev);
		print_message("container created\n");

		print_message("opening container %ssynchronously\n",
			      arg->async ? "a" : "");
		uuid_unparse(uuid, str);
		rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh,
				    &info, arg->async ? &ev : NULL);
		assert_rc_equal(rc, 0);
		WAIT_ON_ASYNC(arg, ev);
		assert_int_equal(uuid_compare(uuid, info.ci_uuid), 0);
		print_message("container opened\n");
	}

	if (arg->hdl_share)
		handle_share(&coh, HANDLE_CO, arg->myrank, arg->pool.poh, 1);

	/** query container */
	print_message("querying container %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_query(coh, &info, NULL, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_int_equal(uuid_compare(uuid, info.ci_uuid), 0);
	print_message("container queried\n");

	if (arg->hdl_share)
		par_barrier(PAR_COMM_WORLD);

	/** close container */
	print_message("closing container %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_close(coh, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("container closed\n");

	if (arg->hdl_share)
		par_barrier(PAR_COMM_WORLD);

	/** destroy container */
	if (arg->myrank == 0) {
		/* XXX check if this is a real leak or out-of-sync close */
		sleep(5);
		print_message("destroying container %ssynchronously ...\n",
			      arg->async ? "a" : "");
		rc = daos_cont_destroy(arg->pool.poh, str, 1 /* force */,
				    arg->async ? &ev : NULL);
		assert_rc_equal(rc, 0);
		WAIT_ON_ASYNC(arg, ev);
		if (arg->async) {
			rc = daos_event_fini(&ev);
			assert_rc_equal(rc, 0);
		}
		print_message("container destroyed\n");
	}

	if (arg->hdl_share)
		par_barrier(PAR_COMM_WORLD);

	/** negative - create container */
	if (arg->myrank == 0) {
		print_message("creating container with invalid pool %ssynchronously ...\n",
			      arg->async ? "a" : "");
		rc = daos_cont_create(poh_inval, &uuid, NULL, arg->async ? &ev : NULL);
		if (arg->async)
			assert_rc_equal(rc, 0);
		else
			assert_rc_equal(rc, -DER_NO_HDL);
		WAIT_ON_ASYNC_ERR(arg, ev, -DER_NO_HDL);
	}

	if (arg->hdl_share)
		par_barrier(PAR_COMM_WORLD);

	/** negative - query container */
	print_message("querying stale container handle %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_query(coh, &info, NULL, arg->async ? &ev : NULL);
	if (arg->async)
		assert_rc_equal(rc, 0);
	else
		assert_rc_equal(rc, -DER_NO_HDL);
	WAIT_ON_ASYNC_ERR(arg, ev, -DER_NO_HDL);

	if (arg->hdl_share)
		par_barrier(PAR_COMM_WORLD);

	if (arg->hdl_share)
		par_barrier(PAR_COMM_WORLD);

	/** negative - close container */
	print_message("closing stale container handle %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_close(coh, arg->async ? &ev : NULL);
	if (arg->async)
		assert_rc_equal(rc, 0);
	else
		assert_rc_equal(rc, -DER_NO_HDL);
	WAIT_ON_ASYNC_ERR(arg, ev, -DER_NO_HDL);
}

#define BUFSIZE 10

static void
co_attribute(void **state)
{
	test_arg_t *arg = *state;
	daos_event_t	 ev;
	int		 rc;

	char const *const names[] = { "AVeryLongName", "Name" };
	char const *const names_get[] = { "AVeryLongName", "Wrong", "Name" };
	size_t const name_sizes[] = {
				strlen(names[0]) + 1,
				strlen(names[1]) + 1,
	};
	void const *const in_values[] = {
				"value",
				"this is a long value",
	};
	size_t const in_sizes[] = {
				strlen(in_values[0]),
				strlen(in_values[1]),
	};
	int			 n = (int)ARRAY_SIZE(names);
	int			 m = (int)ARRAY_SIZE(names_get);
	char			 out_buf[10 * BUFSIZE] = { 0 };
	void			*out_values[] = {
						  &out_buf[0 * BUFSIZE],
						  &out_buf[1 * BUFSIZE],
						  &out_buf[2 * BUFSIZE],
						};
	size_t			 out_sizes[] =	{ BUFSIZE, BUFSIZE, BUFSIZE };
	size_t			 total_size;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}

	print_message("setting container attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_cont_set_attr(arg->coh, n, names, in_values, in_sizes,
				arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("listing container attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");

	total_size = 0;
	rc = daos_cont_list_attr(arg->coh, NULL, &total_size,
				 arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying Total Name Length..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));

	total_size = BUFSIZE;
	rc = daos_cont_list_attr(arg->coh, out_buf, &total_size,
				 arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying Small Name..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));
	assert_string_equal(out_buf, names[1]);

	total_size = 10 * BUFSIZE;
	rc = daos_cont_list_attr(arg->coh, out_buf, &total_size,
				 arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying All Names..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));
	assert_string_equal(out_buf, names[1]);
	assert_string_equal(out_buf + name_sizes[1], names[0]);

	print_message("getting container attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");

	rc = daos_cont_get_attr(arg->coh, m, names_get, out_values, out_sizes,
				arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying Name-Value (A)..\n");
	assert_int_equal(out_sizes[0], in_sizes[0]);
	assert_memory_equal(out_values[0], in_values[0], in_sizes[0]);

	print_message("Verifying Name-Value (B)..\n");
	assert_int_equal(out_sizes[1], 0);

	print_message("Verifying Name-Value (C)..\n");
	assert_true(in_sizes[1] > BUFSIZE);
	assert_int_equal(out_sizes[2], in_sizes[1]);
	assert_memory_equal(out_values[2], in_values[1], BUFSIZE);

	rc = daos_cont_get_attr(arg->coh, m, names_get, NULL, out_sizes,
				arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying with NULL buffer..\n");
	assert_int_equal(out_sizes[0], in_sizes[0]);
	assert_int_equal(out_sizes[1], 0);
	assert_int_equal(out_sizes[2], in_sizes[1]);

	rc = daos_cont_del_attr(arg->coh, m, names_get,
				arg->async ? &ev : NULL);
	/* should work even if "Wrong" do not exist */
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying all attributes deletion\n");
	total_size = 0;
	rc = daos_cont_list_attr(arg->coh, NULL, &total_size,
				 arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_int_equal(total_size, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
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
		print_message("ACE had perms: %#lx (expected: %#lx)\n",
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
	char			*label2 = "test_cont_prop_label2";
	char			*foo_label = "foo";
	char			*label2_v2 = "test_cont_prop_label2_version2";
	uuid_t			 cuuid2;
	daos_handle_t		 coh2;
	uuid_t			 cuuid3;
	daos_handle_t		 coh3;
	uuid_t			 cuuid4;
	daos_handle_t		 coh4;
	uuid_t			 cuuid5;
	uint64_t		 snapshot_max = 128;
	daos_prop_t		*prop;
	daos_prop_t		*prop_query;
	daos_prop_t		*prop_query2;
	struct daos_prop_entry	*entry;
	daos_pool_info_t	 info = {0};
	int			 rc;
	char			*exp_owner;
	char			*exp_owner_grp;
	char			 str[37];

	print_message("create container with properties, and query/verify.\n");
	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

	prop = daos_prop_alloc(2);
	assert_non_null(prop);
	/** setting the label on entries with no type should fail */
	rc = daos_prop_set_str(prop, DAOS_PROP_CO_LABEL, label, strlen(label));
	assert_rc_equal(rc, -DER_NONEXIST);

	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	/** setting the label as a pointer should fail */
	rc = daos_prop_set_ptr(prop, DAOS_PROP_CO_LABEL, label, strlen(label));
	assert_rc_equal(rc, -DER_INVAL);
	rc = daos_prop_set_str(prop, DAOS_PROP_CO_LABEL, label, strlen(label));
	assert_rc_equal(rc, 0);
	prop->dpp_entries[1].dpe_type = DAOS_PROP_CO_SNAPSHOT_MAX;
	prop->dpp_entries[1].dpe_val = snapshot_max;

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, prop);
	assert_success(rc);

	if (arg->myrank == 0) {
		rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_debug_set_params(arg->group, info.pi_leader,
			DMG_KEY_FAIL_LOC, DAOS_FORCE_PROP_VERIFY, 0, NULL);
		assert_rc_equal(rc, 0);
	}
	par_barrier(PAR_COMM_WORLD);

	prop_query = get_query_prop_all();
	rc = daos_cont_query(arg->coh, NULL, prop_query, NULL);
	assert_rc_equal(rc, 0);

	assert_int_equal(prop_query->dpp_nr, DAOS_PROP_CO_NUM);
	/* set properties should get the value user set */
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_LABEL);
	if (entry == NULL || strcmp(entry->dpe_str, label) != 0) {
		fail_msg("label verification failed.\n");
	}
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_SNAPSHOT_MAX);
	if (entry == NULL || entry->dpe_val != snapshot_max) {
		fail_msg("snapshot_max verification failed.\n");
	}
	/* not set properties should get default value */
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_CSUM);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_CSUM_OFF) {
		fail_msg("csum verification failed.\n");
	}
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_CSUM_CHUNK_SIZE);
	if (entry == NULL || entry->dpe_val != 32 * 1024) {
		fail_msg("csum chunk size verification failed.\n");
	}
	entry = daos_prop_entry_get(prop_query,
				    DAOS_PROP_CO_CSUM_SERVER_VERIFY);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_CSUM_SV_OFF) {
		fail_msg("csum server verify verification failed.\n");
	}
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_ENCRYPT);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_ENCRYPT_OFF) {
		fail_msg("encrypt verification failed.\n");
	}

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_ACL);
	if (entry == NULL || entry->dpe_val_ptr == NULL ||
	    !is_acl_prop_default((struct daos_acl *)entry->dpe_val_ptr)) {
		fail_msg("ACL prop verification failed.\n");
	}

	/* default owner */
	assert_int_equal(daos_acl_uid_to_principal(geteuid(), &exp_owner), 0);
	print_message("Checking owner set to default\n");
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_OWNER);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, exp_owner, DAOS_ACL_MAX_PRINCIPAL_LEN)) {
		fail_msg("Owner prop verification failed.\n");
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
		fail_msg("Owner-group prop verification failed.\n");
	}
	D_FREE(exp_owner_grp);

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_CO_SCRUBBER_DISABLED);
	if (entry == NULL || entry->dpe_val == true) {
		fail_msg("scrubber disabled failed.\n");
	}

	if (arg->myrank == 0) {
		uuid_t		 uuid;
		daos_prop_t	*prop2;

		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);

		/* Create container: same label - fail */
		print_message("Checking create: different UUID same label "
			      "(will fail)\n");
		rc = daos_cont_create(arg->pool.poh, &cuuid2, prop, NULL);
		assert_rc_equal(rc, -DER_EXIST);

		/* Create container C2: new label - pass */
		rc = daos_prop_set_str(prop, DAOS_PROP_CO_LABEL, label2, strlen(label2));
		assert_rc_equal(rc, 0);
		print_message("Checking create: different label\n");
		rc = daos_cont_create(arg->pool.poh, &cuuid2, prop, NULL);
		assert_rc_equal(rc, 0);
		print_message("created container C2: %s\n", label2);
		/* Open by label, and immediately close */
		rc = daos_cont_open(arg->pool.poh, label2, DAOS_COO_RW, &coh2,
				    NULL, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_cont_close(coh2, NULL /* ev */);
		assert_rc_equal(rc, 0);
		print_message("opened and closed container %s\n", label2);

		/* destroy the container C2 (will re-create it next) */
		rc = daos_cont_destroy(arg->pool.poh, label2, 0 /* force */,
				       NULL /* ev */);
		assert_rc_equal(rc, 0);
		print_message("destroyed container C2: %s\n", label2);

		/* Create C3 with an initial label, rename to old C2 label2
		 * Create container with label2  - fail.
		 */
		print_message("Checking set-prop and create label conflict "
			      "(will fail)\n");
		rc = daos_cont_create_with_label(arg->pool.poh, foo_label,
						 NULL /* prop */, &cuuid3,
						 NULL /* ev */);
		assert_rc_equal(rc, 0);
		print_message("step1: created container C3: %s : "
			      "UUID:"DF_UUIDF"\n", foo_label, DP_UUID(cuuid3));
		rc = daos_cont_open(arg->pool.poh, foo_label, DAOS_COO_RW,
				    &coh3, NULL, NULL);
		assert_rc_equal(rc, 0);
		print_message("step2: C3 set-prop, rename %s -> %s\n",
			      foo_label, prop->dpp_entries[0].dpe_str);
		rc = daos_cont_set_prop(coh3, prop, NULL);
		assert_rc_equal(rc, 0);
		print_message("step3: create cont with label: %s (will fail)\n",
			      prop->dpp_entries[0].dpe_str);
		rc = daos_cont_create(arg->pool.poh, &cuuid4, prop, NULL);
		assert_rc_equal(rc, -DER_EXIST);

		/* Container 3 set-prop label2_v2,
		 * container 1 set-prop label2 - pass
		 */
		print_message("Checking label rename and reuse\n");
		rc = daos_prop_set_str(prop, DAOS_PROP_CO_LABEL, label2_v2, strlen(label2_v2));
		assert_rc_equal(rc, 0);
		print_message("step: C3 set-prop change FROM %s TO %s\n",
			      label2, label2_v2);
		rc = daos_cont_set_prop(coh3, prop, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_prop_set_str(prop, DAOS_PROP_CO_LABEL, label2, strlen(label2));
		assert_rc_equal(rc, 0);
		print_message("step: C1 set-prop change FROM %s TO %s\n",
			      label, label2);
		rc = daos_cont_set_prop(arg->coh, prop, NULL);
		assert_rc_equal(rc, 0);

		/* destroy container C3 */
		rc = daos_cont_close(coh3, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_cont_destroy(arg->pool.poh, label2_v2, 0 /* force */,
				       NULL /* ev */);
		assert_rc_equal(rc, 0);
		print_message("destroyed container C3: %s : "
			      "UUID:"DF_UUIDF"\n", label2_v2, DP_UUID(cuuid3));

		/* Create a container without label*/
		print_message("Checking querying a container without a label\n");
		rc = daos_cont_create(arg->pool.poh, &cuuid5, NULL, NULL);
		assert_rc_equal(rc, 0);
		uuid_unparse(cuuid5, str);
		print_message("step1: created a container without a label. UUID: %s\n", str);
		rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh4, NULL, NULL);
		assert_rc_equal(rc, 0);
		print_message("step2: opened a container without a label\n");
		prop_query2 = get_query_prop_all();
		assert(prop_query2 != NULL);
		rc = daos_cont_query(coh4, NULL, prop_query2, NULL);
		assert_rc_equal(rc, 0);
		print_message("step3: queried container properties\n");
		entry = daos_prop_entry_get(prop_query2, DAOS_PROP_CO_LABEL);
		/* get_query_prop_all() queries all properties, so entry must be not NULL. */
		assert(entry != NULL);
		/* entry->dpe_str == NULL means container label is not set. */
		assert(entry->dpe_str == NULL);
		print_message("step4: checked container has a label not set\n");
		daos_prop_free(prop_query2);
		rc = daos_cont_close(coh4, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_cont_destroy(arg->pool.poh, str, 0, NULL);
		assert_rc_equal(rc, 0);
		print_message("destroyed container UUID: %s\n", str);

		print_message("SUBTEST: checking creation with owner other than current user\n");
		prop2 = daos_prop_alloc(1);
		assert_non_null(prop2);
		prop2->dpp_entries[0].dpe_type = DAOS_PROP_CO_OWNER;
		D_STRNDUP(prop2->dpp_entries->dpe_str, "fakeuser@", 10);
		assert_non_null(prop2->dpp_entries->dpe_str);
		rc = daos_cont_create(arg->pool.poh, &uuid, prop2, NULL);
		assert_rc_equal(rc, -DER_INVAL);
		daos_prop_free(prop2);
	}
	par_barrier(PAR_COMM_WORLD);

	daos_prop_free(prop);
	daos_prop_free(prop_query);
	test_teardown((void **)&arg);
}

static void
co_op_retry(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	char		 str[37];
	daos_handle_t	 coh;
	daos_cont_info_t info;
	int		 rc;

	if (arg->myrank != 0)
		return;

	print_message("creating container ... ");
	rc = daos_cont_create(arg->pool.poh, &uuid, NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("opening container ... ");
	uuid_unparse(uuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh, &info,
			    NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("setting DAOS_CONT_QUERY_FAIL_CORPC ... ");
	rc = daos_debug_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				  DAOS_CONT_QUERY_FAIL_CORPC | DAOS_FAIL_ONCE,
				  0, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("querying container ... ");
	rc = daos_cont_query(coh, &info, NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("setting DAOS_CONT_CLOSE_FAIL_CORPC ... ");
	rc = daos_debug_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				  DAOS_CONT_CLOSE_FAIL_CORPC | DAOS_FAIL_ONCE,
				  0, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("closing container ... ");
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("setting DAOS_CONT_DESTROY_FAIL_CORPC ... ");
	rc = daos_debug_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				  DAOS_CONT_DESTROY_FAIL_CORPC | DAOS_FAIL_ONCE,
				  0, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("destroying container ... ");
	rc = daos_cont_destroy(arg->pool.poh, str, 1 /* force */, NULL);
	assert_rc_equal(rc, 0);
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
	assert_rc_equal(rc, 0);

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
	assert_rc_equal(rc, 0);

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
	assert_rc_equal(rc, 0);
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
	char			 exp_owner[] = "fictionaluser@";
	char			 exp_owner_grp[] = "admins@";
	struct daos_acl		*exp_acl;
	struct daos_acl		*update_acl = NULL;
	struct daos_ace		*ace;
	char			*user;
	d_string_t		 name_to_remove = "friendlyuser@";
	uint8_t			 type_to_remove = DAOS_ACL_USER;

	print_message("create container with access props, and verify.\n");
	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

	print_message("CONTACL1: initial non-default ACL\n");
	/*
	 * This ACL gives the effective user permissions to interact
	 * with the ACL. This is the bare minimum required to run the tests.
	 */
	user = get_current_user_name();

	print_message("Creating ACL with entry for user %s\n", user);

	exp_acl = daos_acl_create(NULL, 0);
	assert_non_null(exp_acl);

	add_ace_with_perms(&exp_acl, DAOS_ACL_OWNER, NULL, DAOS_ACL_PERM_SET_OWNER);
	add_ace_with_perms(&exp_acl, DAOS_ACL_USER, user,
			   DAOS_ACL_PERM_GET_ACL | DAOS_ACL_PERM_SET_ACL);
	add_ace_with_perms(&exp_acl, DAOS_ACL_EVERYONE, NULL,
			   DAOS_ACL_PERM_READ);
	assert_rc_equal(daos_acl_validate(exp_acl), 0);

	/*
	 * Set up the container with non-default ACL
	 */
	prop_in = daos_prop_alloc(1);
	assert_non_null(prop_in);
	prop_in->dpp_entries[0].dpe_type = DAOS_PROP_CO_ACL;
	prop_in->dpp_entries[0].dpe_val_ptr = daos_acl_dup(exp_acl);

	while (!rc && arg->setup_state != SETUP_CONT_CREATE)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, prop_in);
	assert_success(rc);

	/* Update ownership for the rest of the test */
	rc = daos_cont_open(arg->pool.poh, arg->co_str, DAOS_COO_RW, &arg->coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_set_owner(arg->coh, exp_owner, exp_owner_grp, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_close(arg->coh, NULL);
	arg->coh = DAOS_HDL_INVAL;
	assert_rc_equal(rc, 0);

	/* reconnect with the new permissions */
	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, prop_in);
	assert_success(rc);

	if (arg->myrank == 0) {
		rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_debug_set_params(arg->group, info.pi_leader,
			DMG_KEY_FAIL_LOC, DAOS_FORCE_PROP_VERIFY, 0, NULL);
		assert_rc_equal(rc, 0);
	}
	par_barrier(PAR_COMM_WORLD);

	co_acl_get(arg, exp_acl, exp_owner, exp_owner_grp);

	print_message("CONTACL2: overwrite ACL with bad inputs\n");
	/* Invalid inputs */
	rc = daos_cont_overwrite_acl(arg->coh, NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	rc = daos_cont_overwrite_acl(DAOS_HDL_INVAL, exp_acl, NULL);
	assert_rc_equal(rc, -DER_NO_HDL);

	print_message("CONTACL3: overwrite ACL\n");
	/*
	 * Modify the existing ACL - don't want to clobber the user entry
	 * though.
	 */
	rc = daos_acl_remove_ace(&exp_acl, DAOS_ACL_EVERYONE, NULL);
	assert_rc_equal(rc, 0);

	add_ace_with_perms(&exp_acl, DAOS_ACL_OWNER, NULL,
			   DAOS_ACL_PERM_GET_PROP | DAOS_ACL_PERM_SET_PROP |
			   DAOS_ACL_PERM_DEL_CONT);
	add_ace_with_perms(&exp_acl, DAOS_ACL_GROUP, "testgroup@",
			   DAOS_ACL_PERM_GET_PROP | DAOS_ACL_PERM_READ |
			   DAOS_ACL_PERM_WRITE | DAOS_ACL_PERM_DEL_CONT);
	add_ace_with_perms(&exp_acl, DAOS_ACL_GROUP, "testgroup2@",
			   DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE);

	rc = daos_acl_get_ace_for_principal(exp_acl, DAOS_ACL_USER, user, &ace);
	assert_rc_equal(rc, 0);
	ace->dae_allow_perms |= DAOS_ACL_PERM_SET_OWNER;

	assert_rc_equal(daos_acl_validate(exp_acl), 0);

	rc = daos_cont_overwrite_acl(arg->coh, exp_acl, NULL);
	assert_rc_equal(rc, 0);

	co_acl_get(arg, exp_acl, exp_owner, exp_owner_grp);

	print_message("CONTACL4: update ACL with bad inputs\n");
	rc = daos_cont_update_acl(DAOS_HDL_INVAL, update_acl, NULL);
	assert_rc_equal(rc, -DER_INVAL);
	rc = daos_cont_update_acl(arg->coh, NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	print_message("CONTACL5: update ACL\n");
	/* Add one new entry and update an entry already in our ACL */
	update_acl = daos_acl_create(NULL, 0);
	add_ace_with_perms(&update_acl, DAOS_ACL_USER, "friendlyuser@",
			   DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE);
	add_ace_with_perms(&update_acl, DAOS_ACL_GROUP, "testgroup2@",
			   DAOS_ACL_PERM_READ);

	assert_rc_equal(daos_acl_validate(update_acl), 0);

	/* Update expected ACL to include changes */
	ace = daos_acl_get_next_ace(update_acl, NULL);
	while (ace != NULL) {
		assert_rc_equal(daos_acl_add_ace(&exp_acl, ace), 0);
		ace = daos_acl_get_next_ace(update_acl, ace);
	}

	rc = daos_cont_update_acl(arg->coh, update_acl, NULL);
	assert_rc_equal(rc, 0);

	co_acl_get(arg, exp_acl, exp_owner, exp_owner_grp);

	print_message("CONTACL6: delete entry from ACL with bad inputs\n");
	rc = daos_cont_delete_acl(DAOS_HDL_INVAL, type_to_remove,
				  name_to_remove, NULL);
	assert_rc_equal(rc, -DER_NO_HDL);

	rc = daos_cont_delete_acl(arg->coh, -1, name_to_remove, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	rc = daos_cont_delete_acl(arg->coh, type_to_remove, "bad", NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	print_message("CONTACL7: delete entry from ACL\n");

	/* Update expected ACL to remove the entry */
	assert_rc_equal(daos_acl_remove_ace(&exp_acl, type_to_remove,
					     name_to_remove), 0);

	rc = daos_cont_delete_acl(arg->coh, type_to_remove, name_to_remove,
				  NULL);
	assert_rc_equal(rc, 0);

	co_acl_get(arg, exp_acl, exp_owner, exp_owner_grp);

	print_message("CONTACL8: delete entry no longer in ACL\n");

	/* try deleting same entry again - should be gone */
	rc = daos_cont_delete_acl(arg->coh, type_to_remove, name_to_remove,
				  NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	/* Clean up */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	par_barrier(PAR_COMM_WORLD);

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
	const char		exp_label[] = "NEW_FANCY_LABEL";
	const char		exp_owner[] = "wonderfuluser@wonderfuldomain";

	print_message("create container with default props and modify them.\n");
	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, NULL);
	assert_success(rc);

	par_barrier(PAR_COMM_WORLD);

	/*
	 * Set some props
	 */
	prop_in = daos_prop_alloc(2);
	assert_non_null(prop_in);
	prop_in->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP_S(prop_in->dpp_entries[0].dpe_str, exp_label);
	prop_in->dpp_entries[1].dpe_type = DAOS_PROP_CO_OWNER;
	D_STRNDUP_S(prop_in->dpp_entries[1].dpe_str, exp_owner);

	print_message("Setting the container props\n");
	rc = daos_cont_set_prop(arg->coh, prop_in, NULL);
	assert_rc_equal(rc, 0);

	print_message("Querying all container props\n");
	prop_out = daos_prop_alloc(0);
	assert_non_null(prop_out);
	rc = daos_cont_query(arg->coh, NULL, prop_out, NULL);
	assert_rc_equal(rc, 0);

	assert_non_null(prop_out->dpp_entries);
	assert_true(prop_out->dpp_nr >= prop_in->dpp_nr);

	print_message("Checking label\n");
	entry = daos_prop_entry_get(prop_out, DAOS_PROP_CO_LABEL);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, exp_label,
		    DAOS_PROP_LABEL_MAX_LEN)) {
		fail_msg("Label prop verification failed.\n");
	}

	print_message("Checking owner\n");
	entry = daos_prop_entry_get(prop_out, DAOS_PROP_CO_OWNER);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, exp_owner,
		    DAOS_ACL_MAX_PRINCIPAL_LEN)) {
		fail_msg("Owner prop verification failed.\n");
	}

	par_barrier(PAR_COMM_WORLD);

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
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

	print_message("Try to create container on pool with no create perms\n");

	/* on the pool, write is an alias for create+del cont */
	prop = get_daos_prop_with_owner_acl_perms(DAOS_ACL_PERM_POOL_ALL &
						  ~DAOS_ACL_PERM_CREATE_CONT &
						  ~DAOS_ACL_PERM_WRITE,
						  DAOS_PROP_PO_ACL);

	while (!rc && arg->setup_state != SETUP_POOL_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, prop, NULL);

	if (arg->myrank == 0) {
		rc = daos_cont_create(arg->pool.poh, &arg->co_uuid, NULL, NULL);
		assert_rc_equal(rc, -DER_NO_PERM);
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
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

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
	assert_success(rc);

	if (arg->myrank == 0) {
		print_message("Try to delete container where pool and cont "
			      "deny access\n");
		rc = daos_cont_destroy(arg->pool.poh, arg->co_str, 1, NULL);
		assert_rc_equal(rc, -DER_NO_PERM);

		print_message("Delete with privs from container ACL only\n");

		cont_acl = daos_acl_dup(cont_prop->dpp_entries[0].dpe_val_ptr);
		assert_non_null(cont_acl);
		rc = daos_acl_get_ace_for_principal(cont_acl, DAOS_ACL_OWNER,
						    NULL,
						    &update_ace);
		assert_rc_equal(rc, 0);
		update_ace->dae_allow_perms = DAOS_ACL_PERM_CONT_ALL;

		print_message("- getting container handle\n");
		rc = daos_cont_open(arg->pool.poh, arg->co_str, DAOS_COO_RW,
				    &coh, NULL, NULL);
		assert_rc_equal(rc, 0);

		print_message("- updating cont ACL to restore delete privs\n");
		rc = daos_cont_update_acl(coh, cont_acl, NULL);
		assert_rc_equal(rc, 0);

		print_message("- closing container\n");
		rc = daos_cont_close(coh, NULL);
		assert_rc_equal(rc, 0);

		print_message("Deleting container now should succeed\n");
		rc = daos_cont_destroy(arg->pool.poh, arg->co_str, 1, NULL);
		assert_rc_equal(rc, 0);

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
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

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
	assert_success(rc);

	if (arg->myrank == 0) {
		print_message("Deleting container with only pool-level "
			      "perms\n");
		rc = daos_cont_destroy(arg->pool.poh, arg->co_str, 1, NULL);
		assert_rc_equal(rc, 0);

		/* Clear cont uuid since we already deleted it */
		uuid_clear(arg->co_uuid);
	}

	daos_prop_free(pool_prop);
	daos_prop_free(cont_prop);
	test_teardown((void **)&arg);
}

static void
create_cont_with_user_perms(test_arg_t *arg, uint64_t perms)
{
	struct daos_acl	*acl = NULL;
	daos_handle_t	 coh;
	int		 rc = 0;

	while (!rc && arg->setup_state != SETUP_CONT_CREATE)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, NULL);

	rc = daos_cont_open(arg->pool.poh, arg->co_str, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	/* remove current user's ownership, so they don't have owner perms */
	rc = daos_cont_set_owner(coh, "nobody@", NULL, NULL);
	assert_rc_equal(rc, 0);

	acl = get_daos_acl_with_user_perms(perms);
	rc = daos_cont_overwrite_acl(coh, acl, NULL);
	assert_rc_equal(rc, 0);
	daos_acl_free(acl);

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
}

static void
expect_cont_open_access(test_arg_t *arg, uint64_t perms, uint64_t flags,
			int exp_result)
{
	int	rc = 0;

	create_cont_with_user_perms(arg, perms);

	arg->cont_open_flags = flags;
	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, NULL);

	if (arg->myrank == 0) {
		/* Make sure we actually got to the container open step */
		assert_int_equal(arg->setup_state, SETUP_CONT_CONNECT);
		assert_rc_equal(rc, exp_result);
	}

	/* Cleanup */
	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
co_open_access(void **state)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	int		rc;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

	print_message("cont ACL gives the user no permissions\n");
	expect_cont_open_access(arg, 0, DAOS_COO_RO, -DER_NO_PERM);

	print_message("cont ACL gives the user RO, they want RW\n");
	expect_cont_open_access(arg, DAOS_ACL_PERM_READ, DAOS_COO_RW,
				-DER_NO_PERM);

	print_message("cont ACL gives the user RO + DEL, they want RW\n");
	expect_cont_open_access(arg, DAOS_ACL_PERM_READ | DAOS_ACL_PERM_DEL_CONT, DAOS_COO_RW,
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
	daos_cont_info_t	 info;
	int			 rc = 0;

	create_cont_with_user_perms(arg, perms);

	arg->cont_open_flags = DAOS_COO_RO;
	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, NULL);
	assert_success(rc);

	if (arg->myrank == 0) {
		rc = daos_cont_query(arg->coh, &info, query_prop, NULL);
		assert_rc_equal(rc, exp_result);
	}

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
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

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
	daos_prop_t	*acl_prop;
	int		 rc = 0;

	create_cont_with_user_perms(arg, perms);

	arg->cont_open_flags = DAOS_COO_RO;
	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, NULL);
	assert_success(rc);

	if (arg->myrank == 0) {
		rc = daos_cont_get_acl(arg->coh, &acl_prop, NULL);
		assert_rc_equal(rc, exp_result);

		if (rc == 0)
			daos_prop_free(acl_prop);
	}

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
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

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
	int	rc = 0;

	create_cont_with_user_perms(arg, perms);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, NULL);
	assert_success(rc);

	if (arg->myrank == 0) {
		rc = daos_cont_set_prop(arg->coh, prop, NULL);
		assert_rc_equal(rc, exp_result);
	}

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
			     "My_container");

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
			     "Container_1");
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
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

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
	struct daos_acl	*acl = NULL;
	int		 rc = 0;

	create_cont_with_user_perms(arg, perms);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, NULL);
	assert_success(rc);

	if (arg->myrank == 0) {
		acl = get_daos_acl_with_owner_perms(DAOS_ACL_PERM_CONT_ALL);

		rc = daos_cont_overwrite_acl(arg->coh, acl, NULL);
		assert_rc_equal(rc, exp_result);

		daos_acl_free(acl);
	}

	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
expect_co_update_acl_access(test_arg_t *arg, uint64_t perms, int exp_result)
{
	struct daos_acl	*acl = NULL;
	int		 rc = 0;

	create_cont_with_user_perms(arg, perms);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, NULL);
	assert_success(rc);

	if (arg->myrank == 0) {
		acl = get_daos_acl_with_owner_perms(DAOS_ACL_PERM_CONT_ALL);

		rc = daos_cont_update_acl(arg->coh, acl, NULL);
		assert_rc_equal(rc, exp_result);

		daos_acl_free(acl);
	}

	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
expect_co_delete_acl_access(test_arg_t *arg, uint64_t perms, int exp_result)
{
	int	rc = 0;

	create_cont_with_user_perms(arg, perms);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, NULL);
	assert_success(rc);

	if (arg->myrank == 0) {
		rc = daos_cont_delete_acl(arg->coh, DAOS_ACL_OWNER, NULL, NULL);
		assert_rc_equal(rc, exp_result);
	}

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
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

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
	assert_rc_equal(rc, 0);

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
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

	/*
	 * To start with, the euid/egid are the owner user/group.
	 */
	assert_rc_equal(daos_acl_uid_to_principal(geteuid(), &original_user),
			0);
	assert_rc_equal(daos_acl_gid_to_principal(getegid(), &original_grp),
			0);

	if (arg->myrank == 0) {
		print_message("Set owner with null params\n");
		rc = daos_cont_set_owner(arg->coh, NULL, NULL, NULL);
		assert_rc_equal(rc, -DER_INVAL);

		print_message("Set owner with invalid user\n");
		rc = daos_cont_set_owner(arg->coh, "not_a_valid_user", new_grp,
					 NULL);
		assert_rc_equal(rc, -DER_INVAL);

		print_message("Set owner with invalid grp\n");
		rc = daos_cont_set_owner(arg->coh, new_user, "not_a_valid_grp",
					 NULL);
		assert_rc_equal(rc, -DER_INVAL);

		print_message("Set owner user\n");
		rc = daos_cont_set_owner(arg->coh, new_user, NULL, NULL);
		assert_rc_equal(rc, 0);
		expect_ownership(arg, new_user, original_grp);

		print_message("Change owner user back\n");
		rc = daos_cont_set_owner(arg->coh, original_user, NULL, NULL);
		assert_rc_equal(rc, 0);
		expect_ownership(arg, original_user, original_grp);

		print_message("Set owner group\n");
		rc = daos_cont_set_owner(arg->coh, NULL, new_grp, NULL);
		assert_rc_equal(rc, 0);
		expect_ownership(arg, original_user, new_grp);

		print_message("Change owner group back\n");
		rc = daos_cont_set_owner(arg->coh, NULL, original_grp, NULL);
		assert_rc_equal(rc, 0);
		expect_ownership(arg, original_user, original_grp);

		print_message("Set both owner user and group\n");
		rc = daos_cont_set_owner(arg->coh, new_user, new_grp, NULL);
		assert_rc_equal(rc, 0);
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
	assert_success(rc);

	if (arg->myrank == 0) {
		rc = daos_cont_set_owner(arg->coh, user, grp, NULL);
		assert_rc_equal(rc, exp_result);
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
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

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
	char		 str[37];
	daos_handle_t	 coh;
	daos_cont_info_t info;
	int		 rc;

	if (arg->myrank != 0)
		return;

	print_message("creating container\n");
	rc = daos_cont_create(arg->pool.poh, &uuid, NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("container "DF_UUIDF" created\n",
		      DP_UUID(uuid));

	print_message("opening container\n");
	uuid_unparse(uuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh,
			    &info, NULL);
	assert_rc_equal(rc, 0);

	print_message("destroying container (force=false): should err\n");
	rc = daos_cont_destroy(arg->pool.poh, str, 0 /* force */, NULL);
	assert_rc_equal(rc, -DER_BUSY);

	print_message("destroying container (force=true): should succeed\n");
	rc = daos_cont_destroy(arg->pool.poh, str, 1 /* force */, NULL);
	assert_rc_equal(rc, 0);

	print_message("closing container: should succeed\n");
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
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
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  owner_deny_prop);
	assert_success(rc);

	print_message("Owner has no permissions for non-ACL access\n");

	print_message("- Verify get-prop denied\n");
	tmp_prop = daos_prop_alloc(0);
	rc = daos_cont_query(arg->coh, NULL, tmp_prop, NULL);
	assert_rc_equal(rc, -DER_NO_PERM);
	daos_prop_free(tmp_prop);

	print_message("- Verify set-prop denied\n");
	tmp_prop = daos_prop_alloc(1);
	assert_non_null(tmp_prop);
	tmp_prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	D_STRNDUP_S(tmp_prop->dpp_entries[0].dpe_str, "My_Label");
	rc = daos_cont_set_prop(arg->coh, tmp_prop, NULL);
	assert_rc_equal(rc, -DER_NO_PERM);
	daos_prop_free(tmp_prop);

	print_message("- Verify set-owner denied\n");
	rc = daos_cont_set_owner(arg->coh, "somebody@", "somegroup@", NULL);
	assert_rc_equal(rc, -DER_NO_PERM);

	print_message("Owner has get-ACL access implicitly\n");
	rc = daos_cont_get_acl(arg->coh, &acl_prop, NULL);
	assert_rc_equal(rc, 0);

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
	assert_rc_equal(rc, 0);

	print_message("- Verify update-ACL\n");
	rc = daos_cont_update_acl(arg->coh, acl, NULL);
	assert_rc_equal(rc, 0);

	print_message("- Verify delete-ACL\n");
	rc = daos_cont_delete_acl(arg->coh, DAOS_ACL_OWNER, NULL, NULL);
	assert_rc_equal(rc, 0);

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
	const char	 value_buf[] = "This is the value";
	const char	*value = value_buf;
	const size_t	 size = sizeof(value_buf) - 1;

	cont_prop = get_daos_prop_with_owner_acl_perms(perms,
						       DAOS_PROP_CO_ACL);

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_success(rc);

	if (arg->myrank == 0) {
		/* Trivial case - just to see if we have access */
		rc = daos_cont_set_attr(arg->coh, 1, &name,
					(const void * const*)&value,
					&size,
					NULL);
		assert_rc_equal(rc, exp_result);
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
	void		*valptr = &value;

	cont_prop = get_daos_prop_with_owner_acl_perms(perms,
						       DAOS_PROP_CO_ACL);

	arg->cont_open_flags = DAOS_COO_RO;
	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL,
					  cont_prop);
	assert_success(rc);

	if (arg->myrank == 0) {
		/* Trivial case - just to see if we have access */
		rc = daos_cont_get_attr(arg->coh, 1, &name,
					(void * const*)&valptr,
					&val_size,
					NULL);

		/* 0 size means non-existing attr and this is possible
		 * only because we do not support empty attrs for now
		 */
		if (val_size == 0)
			rc = -DER_NONEXIST;

		assert_rc_equal(rc, exp_result);
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
	assert_success(rc);

	if (arg->myrank == 0) {
		rc = daos_cont_list_attr(arg->coh, buf, &bufsize, NULL);
		assert_rc_equal(rc, exp_result);
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
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

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

static void
co_open_fail_destroy(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	char		 str[37];
	daos_handle_t	 coh;
	daos_cont_info_t info;
	int		 rc;

	FAULT_INJECTION_REQUIRED();

	if (arg->myrank != 0)
		return;

	print_message("creating container ... ");
	rc = daos_cont_create(arg->pool.poh, &uuid, NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("setting DAOS_CONT_OPEN_FAIL ... ");
	rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				  DAOS_CONT_OPEN_FAIL | DAOS_FAIL_ONCE,
				  0, NULL);
	assert_rc_equal(rc, 0);

	uuid_unparse(uuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh, &info,
			    NULL);
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      0, 0, NULL);
	assert_rc_equal(rc, -DER_IO);
	print_message("destroying container ... ");
	rc = daos_cont_destroy(arg->pool.poh, str, 1 /* force */, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");
}

static void
co_rf_simple(void **state)
{
#define STACK_BUF_LEN	(128)
	test_arg_t		*arg0 = *state;
	test_arg_t		*arg = NULL;
	daos_obj_id_t		 oid;
	daos_handle_t		 coh, oh, coh_g2l;
	d_iov_t			 ghdl = { NULL, 0, 0 };
	daos_prop_t		*prop = NULL;
	struct daos_prop_entry	*entry;
	struct daos_co_status	 stat = { 0 };
	daos_cont_info_t	 info = { 0 };
	daos_obj_id_t		 io_oid;
	daos_handle_t		 io_oh;
	d_iov_t			 dkey;
	char			 stack_buf[STACK_BUF_LEN];
	d_sg_list_t		 sgl;
	d_iov_t			 sg_iov;
	daos_iod_t		 iod;
	daos_recx_t		 recx;
	int			 rc;

	/* needs 3 alive nodes after excluding 3 */
	if (!test_runable(arg0, 6))
		skip();

	print_message("create container with properties, and query/verify.\n");
	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

	prop = daos_prop_alloc(1);
	assert_non_null(prop);
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RF2;

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, prop);
	assert_success(rc);

	/* test 1 - cont rf and obj redundancy */
	print_message("verify cont rf is set and can be queried ...\n");
	if (arg->myrank == 0) {
		daos_prop_t	*prop_out = daos_prop_alloc(1);

		assert_non_null(prop_out);
		prop_out->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;

		rc = daos_cont_query(arg->coh, &info, prop_out, NULL);
		assert_rc_equal(rc, 0);
		assert_int_equal(prop_out->dpp_entries[0].dpe_val, DAOS_PROP_CO_REDUN_RF2);

		daos_prop_free(prop_out);
	}
	par_barrier(PAR_COMM_WORLD);

	print_message("verify cont rf and obj open ...\n");
	oid = daos_test_oid_gen(arg->coh, OC_RP_2G1, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	oid = daos_test_oid_gen(arg->coh, OC_EC_2P1G1, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	oid = daos_test_oid_gen(arg->coh, OC_RP_3G1, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	oid = daos_test_oid_gen(arg->coh, OC_EC_2P2G1, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	/* test 2 - cont rf and pool map */
	print_message("verify cont rf and pool map ...\n");
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_STATUS;
	rc = daos_cont_query(arg->coh, NULL, prop, NULL);
	assert_rc_equal(rc, 0);
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_STATUS);
	daos_prop_val_2_co_status(entry->dpe_val, &stat);
	assert_int_equal(stat.dcs_status, DAOS_PROP_CO_HEALTHY);

	if (arg->myrank == 0) {
		daos_debug_set_params(NULL, -1, DMG_KEY_FAIL_LOC,
				      DAOS_REBUILD_DELAY | DAOS_FAIL_ALWAYS,
				      0, NULL);
		rc = dmg_pool_exclude(arg->dmg_config, arg->pool.pool_uuid,
				      arg->group, 5, -1);
		assert_success(rc);
		rc = dmg_pool_exclude(arg->dmg_config, arg->pool.pool_uuid,
				      arg->group, 4, -1);
		assert_success(rc);
	}
	par_barrier(PAR_COMM_WORLD);
	rc = daos_cont_query(arg->coh, NULL, prop, NULL);
	assert_rc_equal(rc, 0);
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_STATUS);
	daos_prop_val_2_co_status(entry->dpe_val, &stat);
	assert_int_equal(stat.dcs_status, DAOS_PROP_CO_HEALTHY);
	rc = daos_cont_open(arg->pool.poh, arg->co_str, arg->cont_open_flags,
			    &coh, &arg->co_info, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	/* IO testing */
	io_oid = daos_test_oid_gen(arg->coh, OC_RP_4G1, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, io_oid, DAOS_OO_RW, &io_oh, NULL);
	assert_rc_equal(rc, 0);

	d_iov_set(&dkey, "dkey", strlen("dkey"));
	dts_buf_render(stack_buf, STACK_BUF_LEN);
	d_iov_set(&sg_iov, stack_buf, STACK_BUF_LEN);
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 1;
	sgl.sg_iovs	= &sg_iov;
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	recx.rx_idx = 0;
	recx.rx_nr  = STACK_BUF_LEN;
	iod.iod_size	= 1;
	iod.iod_nr	= 1;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;
	print_message("obj update should success before RF broken\n");
	rc = daos_obj_update(io_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			     NULL);
	assert_rc_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = dmg_pool_exclude(arg->dmg_config, arg->pool.pool_uuid,
				      arg->group, 3, -1);
		assert_success(rc);
	}
	par_barrier(PAR_COMM_WORLD);
	rc = daos_cont_query(arg->coh, NULL, prop, NULL);
	assert_rc_equal(rc, 0);
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_STATUS);
	daos_prop_val_2_co_status(entry->dpe_val, &stat);
	assert_int_equal(stat.dcs_status, DAOS_PROP_CO_UNCLEAN);
	rc = daos_cont_open(arg->pool.poh, arg->co_str, arg->cont_open_flags,
			    &coh, NULL, NULL);
	assert_rc_equal(rc, -DER_RF);
	print_message("obj update should fail after RF broken\n");
	rc = daos_obj_update(io_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			     NULL);
	assert_rc_equal(rc, -DER_RF);
	print_message("obj fetch should fail after RF broken\n");
	rc = daos_obj_fetch(io_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL,
			    NULL);
	assert_rc_equal(rc, -DER_RF);

	if (arg->myrank == 0) {
		daos_debug_set_params(NULL, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
		test_rebuild_wait(&arg, 1);
		rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group,
					  3, -1);
		assert_success(rc);
		rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group,
					  4, -1);
		assert_success(rc);
		rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group,
					  5, -1);
		assert_success(rc);
		test_rebuild_wait(&arg, 1);
	}
	par_barrier(PAR_COMM_WORLD);

	print_message("obj update should still fail with DER_RF after re-integrate\n");
	rc = daos_obj_update(io_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			     NULL);
	assert_rc_equal(rc, -DER_RF);

	/* clear the UNCLEAN status */
	rc = daos_cont_status_clear(arg->coh, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_query(arg->coh, NULL, prop, NULL);
	assert_rc_equal(rc, 0);
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_STATUS);
	daos_prop_val_2_co_status(entry->dpe_val, &stat);
	assert_int_equal(stat.dcs_status, DAOS_PROP_CO_HEALTHY);
	rc = daos_cont_open(arg->pool.poh, arg->co_str, arg->cont_open_flags,
			    &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_local2global(coh, &ghdl);
	assert_rc_equal(rc, 0);
	ghdl.iov_buf = malloc(ghdl.iov_buf_len);
	assert_non_null(ghdl.iov_buf);
	ghdl.iov_len = ghdl.iov_buf_len;
	rc = daos_cont_local2global(coh, &ghdl);
	assert_rc_equal(rc, 0);

	rc = daos_cont_global2local(arg->pool.poh, ghdl, &coh_g2l);
	assert_rc_equal(rc, 0);
	rc = daos_cont_close(coh_g2l, NULL);
	assert_success(rc);
	free(ghdl.iov_buf);

	rc = daos_obj_close(io_oh, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	daos_prop_free(prop);
	test_teardown((void **)&arg);
}

static void
co_global2local_fail_test(void **state)
{
	test_arg_t   *arg0 = *state;
	test_arg_t   *arg  = NULL;
	d_iov_t       ghdl = {NULL, 0, 0};
	daos_handle_t coh_g2l;
	int           rc;

	FAULT_INJECTION_REQUIRED();

	rc = test_setup((void **)&arg, SETUP_CONT_CONNECT, arg0->multi_rank, SMALL_POOL_SIZE, 0,
			NULL);
	assert_success(rc);

	rc = daos_cont_local2global(arg->coh, &ghdl);
	assert_rc_equal(rc, 0);
	ghdl.iov_buf = malloc(ghdl.iov_buf_len);
	assert_non_null(ghdl.iov_buf);
	ghdl.iov_len = ghdl.iov_buf_len;
	rc = daos_cont_local2global(arg->coh, &ghdl);
	assert_rc_equal(rc, 0);

	daos_fail_loc_set(DAOS_CONT_G2L_FAIL | DAOS_FAIL_ONCE);
	rc = daos_cont_global2local(arg->pool.poh, ghdl, &coh_g2l);
	assert_rc_equal(rc, -DER_NO_HDL);
	daos_fail_loc_set(0);

	free(ghdl.iov_buf);

	test_teardown((void **)&arg);
}

static void
delet_container_during_aggregation(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_pool_info_t pinfo;
	int		 i;
	int		 rc;

	/* Prepare records */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);

	print_message("Initial Pool Query\n");
	pool_storage_info(arg, &pinfo);

	/* Aggregation will be Hold */
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      DAOS_VOS_AGG_BLOCKED | DAOS_FAIL_ALWAYS, 0, NULL);

	/* Write/fetch and Punch Data with 2K size */
	for (i = 0; i <= 5000; i++)
		io_simple_internal(state, oid, IO_SIZE_SCM * 32, DAOS_IOD_ARRAY,
				   "io_simple_scm_array dkey",
				   "io_simple_scm_array akey");

	/**
	 * Run Pool query every 5 seconds for Total 30 seconds
	 * Aggregation will be ready to run by this time
	 */
	for (i = 0; i <= 5; i++) {
		pool_storage_info(arg, &pinfo);
		sleep(5);
	}

	/* Aggregation will continue */
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);

	/* Destroy the container while Aggregation is running */
	rc = test_teardown_cont(arg);
	assert_rc_equal(rc, 0);

	/* Run Pool query at the end */
	pool_storage_info(arg, &pinfo);
}

static void
co_api_compat(void **state)
{
	test_arg_t		*arg = *state;
	uuid_t			uuid1;
	uuid_t			uuid2;
	char			*label = "test_api_compat_label1";
	daos_handle_t		coh;
	daos_cont_info_t	info;
	int			rc;
	char			uuid_str1[37];
	char			uuid_str2[37];

	if (arg->myrank != 0)
		return;

	uuid_generate(uuid1);
	uuid_clear(uuid2);

	print_message("creating container with uuid specified ... ");
	rc = daos_cont_create(arg->pool.poh, &uuid1, NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");
	uuid_unparse(uuid1, uuid_str1);

	print_message("creating container with a uuid pointer ... ");
	rc = daos_cont_create(arg->pool.poh, &uuid2, NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");
	uuid_unparse(uuid2, uuid_str2);

	print_message("creating container with a NULL pointer ... ");
	rc = daos_cont_create(arg->pool.poh, NULL, NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("creating container with a Label ... ");
	rc = daos_cont_create_with_label(arg->pool.poh, label, NULL, NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("opening container using uuid ... ");
	rc = daos_cont_open(arg->pool.poh, uuid_str1, DAOS_COO_RW, &coh, &info, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	print_message("opening container using Label ... ");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RW, &coh, &info, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	print_message("destroying container using uuid ... ");
	rc = daos_cont_destroy(arg->pool.poh, uuid_str1, 0, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, uuid_str2, 0, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("destroying container using label ... ");
	rc = daos_cont_destroy(arg->pool.poh, label, 0, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");
}

static int
nrank_per_node_get(struct pool_map *poolmap)
{
	struct pool_domain	*dom;
	int			 rc;

	rc = pool_map_find_domain(poolmap, PO_COMP_TP_NODE, PO_COMP_ID_ALL, &dom);
	print_message("system with %d domains of PO_COMP_TP_NODE, %d RANKs per NODE\n",
		      rc, dom->do_comp.co_nr);

	return dom->do_comp.co_nr;
}

static int
ranks_on_same_node(struct pool_map *poolmap, int src_rank, int *ranks)
{
	struct pool_domain	*node_doms;
	struct pool_domain	*rank_doms;
	struct pool_domain	*node_d, *rank_d;
	int			 nnodes, nrank_per_node;
	int			 i, j, k;
	int			 rc;

	rc = pool_map_find_domain(poolmap, PO_COMP_TP_NODE, PO_COMP_ID_ALL, &node_doms);
	nnodes = rc;
	nrank_per_node = node_doms->do_comp.co_nr;

	for (i = 0; i < nnodes; i++) {
		node_d = node_doms + i;
		assert_int_equal(node_d->do_comp.co_nr, nrank_per_node);
		rank_doms = node_d->do_children;
		for (j = 0; j < nrank_per_node; j++) {
			rank_d = rank_doms + j;
			if (rank_d->do_comp.co_rank != src_rank)
				continue;
			for (k = 0; k < nrank_per_node; k++) {
				rank_d = rank_doms + k;
				ranks[k] = rank_d->do_comp.co_rank;
				if (ranks[k] != src_rank)
					print_message("rank %d on same node of rank %d\n",
						      ranks[k], src_rank);
			}
			return 0;
		}
	}

	return -1;
}

static void
co_redun_lvl(void **state)
{
#define STACK_BUF_LEN	(128)
	test_arg_t		*arg0 = *state;
	test_arg_t		*arg = NULL;
	daos_obj_id_t		 oid;
	daos_handle_t		 coh, oh, coh_g2l;
	d_iov_t			 ghdl = { NULL, 0, 0 };
	daos_prop_t		*prop = NULL;
	daos_prop_t		*prop_out = NULL;
	struct daos_prop_entry	*entry;
	struct daos_co_status	 stat = { 0 };
	daos_cont_info_t	 info = { 0 };
	struct pl_map		*plmap = NULL;
	struct pool_map		*poolmap = NULL;
	daos_obj_id_t		 io_oid;
	daos_handle_t		 io_oh;
	d_iov_t			 dkey;
	char			 stack_buf[STACK_BUF_LEN];
	d_sg_list_t		 sgl;
	d_iov_t			 sg_iov;
	daos_iod_t		 iod;
	daos_recx_t		 recx;
	int			 nrank_per_node, ndom;
	int			 ranks[3];
	int			 i, rc;

	if (!test_runable(arg0, 8))
		skip();

	print_message("create container with properties, and query/verify.\n");
	rc = test_setup((void **)&arg, SETUP_POOL_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

	prop = daos_prop_alloc(2);
	assert_non_null(prop);
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_LVL;
	prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_NODE;
	prop->dpp_entries[1].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[1].dpe_val = DAOS_PROP_CO_REDUN_RF1;

	while (!rc && arg->setup_state != SETUP_CONT_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, NULL, prop);
	assert_success(rc);

	/* test 1 - cont rf and obj redundancy */
	print_message("verify cont rf is set and can be queried ...\n");
	if (arg->myrank == 0) {
		prop_out = daos_prop_alloc(2);
		assert_non_null(prop_out);
		prop_out->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_LVL;
		prop_out->dpp_entries[1].dpe_type = DAOS_PROP_CO_REDUN_FAC;

		rc = daos_cont_query(arg->coh, &info, prop_out, NULL);
		assert_rc_equal(rc, 0);
		/* Verify DAOS_PROP_CO_REDUN_LVL and DAOS_PROP_CO_REDUN_FAC values */
		assert_int_equal(prop_out->dpp_entries[0].dpe_val, DAOS_PROP_CO_REDUN_NODE);
		assert_int_equal(prop_out->dpp_entries[1].dpe_val, DAOS_PROP_CO_REDUN_RF1);
	}
	par_barrier(PAR_COMM_WORLD);

	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	plmap = pl_map_find(arg->pool.pool_uuid, oid);
	poolmap = plmap->pl_poolmap;

	ndom = pool_map_find_domain(poolmap, PO_COMP_TP_NODE, PO_COMP_ID_ALL, NULL);
	nrank_per_node = nrank_per_node_get(poolmap);
	print_message("system with ndom %d, nrank_per_node %d\n", ndom, nrank_per_node);

	/* CI test's ftest/daos_test/suite.yaml with 2 ranks per node */
	if (nrank_per_node != 2)
		goto out;
	rc = ranks_on_same_node(poolmap, 7, ranks);
	assert_rc_equal(rc, 0);
	for (i = 5; i > 0; i++) {
		if (i != ranks[0] && i != ranks[1]) {
			ranks[2] = i;
			break;
		}
	}

	print_message("verify cont rf and obj open ...\n");
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	oid = daos_test_oid_gen(arg->coh, OC_EC_2P1G1, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	oid = daos_test_oid_gen(arg->coh, OC_RP_3G1, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	/* test 2 - cont rf and pool map */
	print_message("verify cont rf and pool map ...\n");
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_STATUS;
	rc = daos_cont_query(arg->coh, NULL, prop, NULL);
	assert_rc_equal(rc, 0);
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_STATUS);
	daos_prop_val_2_co_status(entry->dpe_val, &stat);
	assert_int_equal(stat.dcs_status, DAOS_PROP_CO_HEALTHY);

	/* exclude two engined on same node, as redun_lvl set as DAOS_PROP_CO_REDUN_NODE,
	 * should not cause RF broken.
	 */
	if (arg->myrank == 0) {
		daos_debug_set_params(NULL, -1, DMG_KEY_FAIL_LOC,
				      DAOS_REBUILD_DELAY | DAOS_FAIL_ALWAYS, 0, NULL);
		rc = dmg_pool_exclude(arg->dmg_config, arg->pool.pool_uuid, arg->group,
				      ranks[0], -1);
		assert_success(rc);
		rc = dmg_pool_exclude(arg->dmg_config, arg->pool.pool_uuid, arg->group,
				      ranks[1], -1);
		assert_success(rc);
	}
	par_barrier(PAR_COMM_WORLD);
	rc = daos_cont_query(arg->coh, NULL, prop, NULL);
	assert_rc_equal(rc, 0);
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_STATUS);
	daos_prop_val_2_co_status(entry->dpe_val, &stat);
	assert_int_equal(stat.dcs_status, DAOS_PROP_CO_HEALTHY);
	rc = daos_cont_open(arg->pool.poh, arg->co_str, arg->cont_open_flags,
			    &coh, &arg->co_info, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	/* IO testing */
	d_iov_set(&dkey, "dkey", strlen("dkey"));
	dts_buf_render(stack_buf, STACK_BUF_LEN);
	d_iov_set(&sg_iov, stack_buf, STACK_BUF_LEN);
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 1;
	sgl.sg_iovs	= &sg_iov;
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	recx.rx_idx = 0;
	recx.rx_nr  = STACK_BUF_LEN;
	iod.iod_size	= 1;
	iod.iod_nr	= 1;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	if (ndom < 5) {
		print_message("OC_EC_4P1G1 obj layout create should fail if ndom < 5\n");
		io_oid = daos_test_oid_gen(arg->coh, OC_EC_4P1G1, 0, 0, arg->myrank);
		/* grp_size > ndom, should fail in dc_obj_open()->obj_layout_create */
		rc = daos_obj_open(arg->coh, io_oid, DAOS_OO_RW, &io_oh, NULL);
		assert_rc_equal(rc, -DER_INVAL);
	}

	print_message("obj update should success before RF broken\n");
	io_oid = daos_test_oid_gen(arg->coh, OC_EC_2P2G1, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, io_oid, DAOS_OO_RW, &io_oh, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_obj_update(io_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			     NULL);
	assert_rc_equal(rc, 0);

	/* exclude one more rank on another NODE dom */
	if (arg->myrank == 0) {
		rc = dmg_pool_exclude(arg->dmg_config, arg->pool.pool_uuid,
				      arg->group, ranks[2], -1);
		assert_success(rc);
	}

	par_barrier(PAR_COMM_WORLD);
	rc = daos_cont_query(arg->coh, NULL, prop, NULL);
	assert_rc_equal(rc, 0);
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_STATUS);
	daos_prop_val_2_co_status(entry->dpe_val, &stat);
	assert_int_equal(stat.dcs_status, DAOS_PROP_CO_UNCLEAN);
	rc = daos_cont_open(arg->pool.poh, arg->co_str, arg->cont_open_flags, &coh, NULL, NULL);
	assert_rc_equal(rc, -DER_RF);
	print_message("obj update should fail after RF broken\n");
	rc = daos_obj_update(io_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, -DER_RF);
	print_message("obj fetch should fail after RF broken\n");
	rc = daos_obj_fetch(io_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_rc_equal(rc, -DER_RF);

	if (arg->myrank == 0) {
		daos_debug_set_params(NULL, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
		test_rebuild_wait(&arg, 1);
		rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group,
					  ranks[2], -1);
		assert_success(rc);
		rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group,
					  ranks[1], -1);
		assert_success(rc);
		rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group,
					  ranks[0], -1);
		assert_success(rc);
		test_rebuild_wait(&arg, 1);
	}
	par_barrier(PAR_COMM_WORLD);

	print_message("obj update should still fail with DER_RF after re-integrate\n");
	rc = daos_obj_update(io_oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, -DER_RF);

	rc = daos_obj_close(io_oh, NULL);
	assert_rc_equal(rc, 0);

	/* clear the UNCLEAN status */
	rc = daos_cont_status_clear(arg->coh, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_query(arg->coh, NULL, prop, NULL);
	assert_rc_equal(rc, 0);
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_STATUS);
	daos_prop_val_2_co_status(entry->dpe_val, &stat);
	assert_int_equal(stat.dcs_status, DAOS_PROP_CO_HEALTHY);

out:
	print_message("cont prop should be able to query for g2l handle\n");
	rc = daos_cont_open(arg->pool.poh, arg->co_str, arg->cont_open_flags,
			    &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_cont_local2global(coh, &ghdl);
	assert_rc_equal(rc, 0);
	ghdl.iov_buf = malloc(ghdl.iov_buf_len);
	assert_non_null(ghdl.iov_buf);
	ghdl.iov_len = ghdl.iov_buf_len;
	rc = daos_cont_local2global(coh, &ghdl);
	assert_rc_equal(rc, 0);

	rc = daos_cont_global2local(arg->pool.poh, ghdl, &coh_g2l);
	assert_rc_equal(rc, 0);

	if (arg->myrank == 0) {
		prop_out->dpp_entries[0].dpe_val = 0;
		prop_out->dpp_entries[1].dpe_val = 0;

		rc = daos_cont_query(coh_g2l, &info, prop_out, NULL);
		assert_rc_equal(rc, 0);
		/* Verify DAOS_PROP_CO_REDUN_LVL and DAOS_PROP_CO_REDUN_FAC values */
		assert_int_equal(prop_out->dpp_entries[0].dpe_val, DAOS_PROP_CO_REDUN_NODE);
		assert_int_equal(prop_out->dpp_entries[1].dpe_val, DAOS_PROP_CO_REDUN_RF1);
	}

	rc = daos_cont_close(coh_g2l, NULL);
	assert_success(rc);
	free(ghdl.iov_buf);

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	if (plmap != NULL)
		pl_map_decref(plmap);
	daos_prop_free(prop);
	daos_prop_free(prop_out);
	test_teardown((void **)&arg);
}

static void
co_mdtimes(void **state)
{
	test_arg_t	       *arg = *state;
	daos_prop_t	       *prop = NULL;
	d_string_t		label;
	daos_event_t		ev;
	uint64_t		prev_otime;
	uint64_t		prev_mtime;
	daos_handle_t		coh;
	daos_cont_info_t	cinfo;
	daos_epoch_t		epc;
	daos_epoch_range_t	epr;
	int			rc;

	if (arg->myrank != 0)
		return;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}

	D_ASSERT(arg->co_str != NULL);
	print_message("open container %s (RO_MDSTATS flag)\n", arg->co_str);
	rc = daos_cont_open(arg->pool.poh, arg->co_str, DAOS_COO_RO | DAOS_COO_RO_MDSTATS, &coh,
			    &cinfo, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("initial container metadata otime (0x"DF_X64") mtime (0x"DF_X64")\n",
		      cinfo.ci_md_otime, cinfo.ci_md_otime);
	prev_otime = cinfo.ci_md_otime;
	prev_mtime = cinfo.ci_md_mtime;

	print_message("close container\n");
	rc = daos_cont_close(coh, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("open container again (RO_MDSTATS), verify metadata times unchanged\n");
	rc = daos_cont_open(arg->pool.poh, arg->co_str, DAOS_COO_RO | DAOS_COO_RO_MDSTATS, &coh,
			    &cinfo, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_true(cinfo.ci_md_otime == prev_otime);
	assert_true(cinfo.ci_md_mtime == prev_mtime);

	print_message("query container, verify metadata times unchanged\n");
	/* Query for the container label, then open by label from this point */
	prop = daos_prop_alloc(1);
	assert_ptr_not_equal(prop, NULL);
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	rc = daos_cont_query(coh, &cinfo, prop, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_true(cinfo.ci_md_otime == prev_otime);
	assert_true(cinfo.ci_md_mtime == prev_mtime);
	label = prop->dpp_entries[0].dpe_str;

	print_message("close container\n");
	rc = daos_cont_close(coh, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	/* open updates the otime and returns updated time in daos_cont_info_t */
	print_message("open container %s (%s) again (no special flags), confirm updated otime\n",
		      label, arg->co_str);
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RO, &coh, &cinfo,
			    arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_true(cinfo.ci_md_otime > prev_otime);
	assert_true(cinfo.ci_md_mtime == prev_mtime);
	prev_otime = cinfo.ci_md_otime;

	rc = daos_cont_query(coh, &cinfo, NULL /* prop */, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_true(cinfo.ci_md_otime == prev_otime);
	assert_true(cinfo.ci_md_mtime == prev_mtime);
	print_message("query also returned updated otime (0x"DF_X64")\n", cinfo.ci_md_otime);

	print_message("close container\n");
	rc = daos_cont_close(coh, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("open container %s (%s) again (RW), verify otime (open) and mtime (close) "
		      "updated\n", label, arg->co_str);
	cinfo.ci_md_otime = cinfo.ci_md_mtime = 0;
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RW, &coh,
			    &cinfo, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_true(cinfo.ci_md_otime > prev_otime);
	assert_true(cinfo.ci_md_mtime > prev_mtime);
	prev_otime = cinfo.ci_md_otime;
	prev_mtime = cinfo.ci_md_mtime;
	print_message("open returned updated otime (0x"DF_X64"), mtime (0x"DF_X64")\n",
		      cinfo.ci_md_otime, cinfo.ci_md_mtime);

	print_message("create container snapshot, query and verify updated mtime\n");
	rc = daos_cont_create_snap(coh, &epc, NULL /* name */, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	rc = daos_cont_query(coh, &cinfo, NULL /* prop */, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_true(cinfo.ci_md_otime == prev_otime);
	assert_true(cinfo.ci_md_mtime > prev_mtime);
	prev_mtime = cinfo.ci_md_mtime;
	print_message("created snapshot 0x"DF_X64", query returned updated mtime (0x"DF_X64")\n",
		      epc, cinfo.ci_md_mtime);

	print_message("destroy container snapshot, query and verify updated mtime\n");
	epr.epr_lo = epr.epr_hi = epc;
	rc = daos_cont_destroy_snap(coh, epr, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	rc = daos_cont_query(coh, &cinfo, NULL /* prop */, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_true(cinfo.ci_md_otime == prev_otime);
	assert_true(cinfo.ci_md_mtime > prev_mtime);
	prev_mtime = cinfo.ci_md_mtime;
	print_message("destroyed snapshot 0x"DF_X64", query returned updated mtime (0x"DF_X64")\n",
		      epc, cinfo.ci_md_mtime);

	print_message("close container\n");
	rc = daos_cont_close(coh, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	/* TODO: perform other metadata RO operations verify mtime unchanged.
	 * (e.g., attr-list, attr-get, list-snap, get-acl)
	 */

	/* TODO: perform other metadata RW operations, verify mtime updated.
	 * (attr-set/del, set-prop, update/overwrite/delete-acl)
	 */

	daos_prop_free(prop);
}

static void
co_nhandles(void **state)
{
	test_arg_t	       *arg = *state;
	daos_event_t		ev;
	const char	       *clbl =  "nhandles_test_cont";
	uuid_t			cuuid;
	daos_handle_t		coh;		/* opened on 0, shared to all ranks (nhandles=1) */
	daos_handle_t		coh2;		/* opened by all ranks (nhandles=num_ranks) */
	daos_cont_info_t	cinfo;
	uint32_t		expect_nhandles;
	int			rc;

	if (!arg->hdl_share && arg->myrank != 0)
		return;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}

	expect_nhandles = 1;

	if (arg->myrank == 0) {
		print_message("creating container %ssynchronously ...\n",
			      arg->async ? "a" : "");

		rc = daos_cont_create_with_label(arg->pool.poh, clbl, NULL,
						 &cuuid, arg->async ? &ev : NULL);
		assert_rc_equal(rc, 0);
		WAIT_ON_ASYNC(arg, ev);
		print_message("container created: %s\n", clbl);

		/* Open, and expect 1 handles  */
		print_message("rank 0: opening container %ssynchronously\n",
			      arg->async ? "a" : "");
		cinfo.ci_nhandles = 0;
		rc = daos_cont_open(arg->pool.poh, clbl, DAOS_COO_RW, &coh,
				    &cinfo, arg->async ? &ev : NULL);
		assert_rc_equal(rc, 0);
		WAIT_ON_ASYNC(arg, ev);
		assert_int_equal(cinfo.ci_nhandles, expect_nhandles);
		print_message("rank 0: container opened (hdl coh)\n");
	}

	/* handle share coh among all ranks, all ranks call query and verify nhandles */
	if (arg->hdl_share)
		handle_share(&coh, HANDLE_CO, arg->myrank, arg->pool.poh, 0 /* verbose */);

	print_message("rank %d: query container (hdl coh), expect nhandles=%d\n", arg->myrank,
		      expect_nhandles);
	cinfo.ci_nhandles = 0;
	rc = daos_cont_query(coh, &cinfo, NULL, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_int_equal(cinfo.ci_nhandles, expect_nhandles);

	par_barrier(PAR_COMM_WORLD);

	/* all ranks open coh2, then query and verify nhandles (incremented by num ranks) */
	expect_nhandles += arg->rank_size;
	print_message("rank %d: open  container (hdl coh2), expect nhandles<=%d\n", arg->myrank,
		      expect_nhandles);
	cinfo.ci_nhandles = 0;
	rc = daos_cont_open(arg->pool.poh, clbl, DAOS_COO_RO, &coh2,
			    &cinfo, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	/* ranks independently opening; nhandles will not reach expectation until after barrier */
	assert_true(cinfo.ci_nhandles <= expect_nhandles);
	print_message("rank %d: container opened (hdl coh2), nhandles=%d\n", arg->myrank,
		      cinfo.ci_nhandles);

	par_barrier(PAR_COMM_WORLD);

	print_message("rank %d: query container (hdl coh2), expect nhandles=%d\n", arg->myrank,
		      expect_nhandles);
	cinfo.ci_nhandles = 0;
	rc = daos_cont_query(coh2, &cinfo, NULL, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_int_equal(cinfo.ci_nhandles, expect_nhandles);

	print_message("rank %d: query container (hdl coh), expect nhandles=%d\n", arg->myrank,
		      expect_nhandles);
	cinfo.ci_nhandles = 0;
	rc = daos_cont_query(coh, &cinfo, NULL, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_int_equal(cinfo.ci_nhandles, expect_nhandles);

	par_barrier(PAR_COMM_WORLD);

	/* all ranks close coh2, then query and verify nhandles (decremented by num ranks) */
	expect_nhandles -= arg->rank_size;
	print_message("rank %d: close container (hdl coh2)\n", arg->myrank);
	rc = daos_cont_close(coh2, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	par_barrier(PAR_COMM_WORLD);

	print_message("rank %d: query container (hdl coh), expect nhandles=%d\n", arg->myrank,
		      expect_nhandles);
	cinfo.ci_nhandles = 0;
	rc = daos_cont_query(coh, &cinfo, NULL, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_int_equal(cinfo.ci_nhandles, expect_nhandles);

	par_barrier(PAR_COMM_WORLD);

	/* all ranks call close on coh (all except rank 0 handles are global2local handles) */
	print_message("rank %d: close container (hdl coh)\n", arg->myrank);
	rc = daos_cont_close(coh, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	par_barrier(PAR_COMM_WORLD);

	/** destroy container */
	if (arg->myrank == 0) {
		print_message("destroying container %ssynchronously ...\n",
			      arg->async ? "a" : "");
		rc = daos_cont_destroy(arg->pool.poh, clbl, 1 /* force */,
				       arg->async ? &ev : NULL);
		assert_rc_equal(rc, 0);
		WAIT_ON_ASYNC(arg, ev);
		if (arg->async) {
			rc = daos_event_fini(&ev);
			assert_rc_equal(rc, 0);
		}
		print_message("container destroyed\n");
	}

}

static void
alloc_group_list(uid_t uid, gid_t gid, gid_t **groups, size_t *nr_groups)
{
	struct passwd	*pw;
	int		tmp = 0;
	int		rc;

	pw = getpwuid(uid);
	assert_non_null(pw);

	rc = getgrouplist(pw->pw_name, gid, NULL, &tmp);
	if (rc != -1) {
		D_PRINT("getting the number of groups failed\n");
		assert_true(false);
	}

	*nr_groups = (size_t)tmp;
	D_ALLOC_ARRAY(*groups, *nr_groups);
	assert_non_null(*groups);

	rc = getgrouplist(pw->pw_name, gid, *groups, &tmp);
	assert_true(rc > 0);
}

static void
co_get_perms(void **state)
{
	test_arg_t	*arg = *state;
	daos_prop_t	*pool_prop = NULL;
	daos_prop_t	*cont_prop = NULL;
	uid_t		uid = geteuid();
	gid_t		gid = getegid();
	gid_t		*gids;
	size_t		nr_gids;
	uint64_t	perms = 0;
	int		rc;

	alloc_group_list(uid, gid, &gids, &nr_gids);

	print_message("creating container with default ACLs\n");
	rc = daos_cont_create(arg->pool.poh, &arg->co_uuid, NULL, NULL);
	assert_rc_equal(rc, 0);

	print_message("opening container\n");
	uuid_unparse(arg->co_uuid, arg->co_str);
	rc = daos_cont_open(arg->pool.poh, arg->co_str, DAOS_COO_RW, &arg->coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	print_message("querying pool ACL/ownership\n");
	pool_prop = daos_prop_alloc(3);
	assert_non_null(pool_prop);
	pool_prop->dpp_entries[0].dpe_type = DAOS_PROP_PO_ACL;
	pool_prop->dpp_entries[1].dpe_type = DAOS_PROP_PO_OWNER;
	pool_prop->dpp_entries[2].dpe_type = DAOS_PROP_PO_OWNER_GROUP;
	rc = daos_pool_query(arg->pool.poh, NULL, NULL, pool_prop, NULL);
	assert_rc_equal(rc, 0);

	print_message("getting pool permissions for uid %d\n", uid);
	rc = daos_pool_get_perms(pool_prop, uid, gids, nr_gids, &perms);
	assert_rc_equal(rc, 0);
	/* uid running this is the owner */
	assert_int_equal(perms, DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE);

	print_message("querying container ACL/ownership\n");
	cont_prop = daos_prop_alloc(3);
	assert_non_null(cont_prop);
	cont_prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_ACL;
	cont_prop->dpp_entries[1].dpe_type = DAOS_PROP_CO_OWNER;
	cont_prop->dpp_entries[2].dpe_type = DAOS_PROP_CO_OWNER_GROUP;
	rc = daos_cont_query(arg->coh, NULL, cont_prop, NULL);
	assert_rc_equal(rc, 0);

	print_message("getting cont permissions for uid %d\n", uid);
	rc = daos_cont_get_perms(cont_prop, uid, gids, nr_gids, &perms);
	assert_rc_equal(rc, 0);
	assert_int_equal(perms, DAOS_ACL_PERM_CONT_ALL); /* uid running this is the owner */

	D_FREE(gids);
	daos_prop_free(pool_prop);
	daos_prop_free(cont_prop);

	test_teardown_cont_hdl(arg);
	test_teardown_cont(arg);
}

static void
co_exclusive_open(void **state)
{
	test_arg_t	*arg = *state;
	char		*label = "exclusive_open";
	uuid_t		 uuid;
	daos_handle_t	 coh;
	daos_handle_t	 coh_ex;
	int		 rc;

	if (arg->myrank != 0)
		return;

	rc = daos_cont_create_with_label(arg->pool.poh, label, NULL, &uuid, NULL);
	assert_rc_equal(rc, 0);
	print_message("created container '%s' ("DF_UUIDF")\n", label, DP_UUID(uuid));

	print_message("SUBTEST: EX conflicts with RO\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RO | DAOS_COO_EX, &coh, NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	print_message("SUBTEST: EX conflicts with RW\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RW | DAOS_COO_EX, &coh, NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	print_message("SUBTEST: other handles already exist; shall get "DF_RC"\n",
		      DP_RC(-DER_BUSY));
	print_message(" performing a non-exclusive open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);
	print_message(" trying to perform an exclusive open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_EX, &coh_ex, NULL, NULL);
	assert_rc_equal(rc, -DER_BUSY);
	print_message(" closing the non-exclusive handle\n");
	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);

	print_message("SUBTEST: no other handles; shall succeed\n");
	print_message(" performing an exclusive open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_EX, &coh_ex, NULL, NULL);
	assert_rc_equal(rc, 0);

	print_message("SUBTEST: shall prevent other handles ("DF_RC")\n", DP_RC(-DER_BUSY));
	print_message(" trying to perform a non-exclusive open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, -DER_BUSY);
	print_message(" closing the exclusive handle\n");
	rc = daos_cont_close(coh_ex, NULL);
	assert_rc_equal(rc, 0);

	print_message("destroying container '%s'\n", label);
	rc = daos_cont_destroy(arg->pool.poh, label, 0 /* force */, NULL);
	assert_rc_equal(rc, 0);
}

static void
co_evict_hdls(void **state)
{
	test_arg_t	*arg = *state;
	char		*label = "evict_hdls";
	uuid_t		 uuid;
	daos_handle_t	 coh0;
	daos_handle_t	 coh1;
	daos_handle_t	 coh2;
	daos_cont_info_t info;
	int		 rc;

	if (arg->myrank != 0)
		return;

	rc = daos_cont_create_with_label(arg->pool.poh, label, NULL, &uuid, NULL);
	assert_rc_equal(rc, 0);
	print_message("created container '%s' ("DF_UUIDF")\n", label, DP_UUID(uuid));

	print_message("SUBTEST: EVICT conflicts with EVICT_ALL\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RO | DAOS_COO_EVICT | DAOS_COO_EVICT_ALL,
			    &coh0, NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RW | DAOS_COO_EVICT | DAOS_COO_EVICT_ALL,
			    &coh0, NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_EX | DAOS_COO_EVICT | DAOS_COO_EVICT_ALL,
			    &coh0, NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	print_message("SUBTEST: EX|EVICT is not supported\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_EX | DAOS_COO_EVICT, &coh0, NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	print_message("SUBTEST: EVICT no handle; shall succeed\n");
	print_message(" performing an RO|EVICT open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RO | DAOS_COO_EVICT, &coh0, &info, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(info.ci_nhandles, 1);
	print_message(" closing the handle\n");
	rc = daos_cont_close(coh0, NULL);
	assert_rc_equal(rc, 0);

	print_message("SUBTEST: EVICT my own exclusive handle; shall succeed\n");
	print_message(" performing an EX open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_EX, &coh1, &info, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(info.ci_nhandles, 1);
	print_message(" performing an RW|EVICT open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RW | DAOS_COO_EVICT, &coh0, &info, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(info.ci_nhandles, 1);
	print_message(" closing the evicted EX handle to avoid local resource leaks\n");
	rc = daos_cont_close(coh1, NULL);
	/*
	 * Closing an evicted handle returns zero. See "already closed" in
	 * cont_close.
	 */
	assert_rc_equal(rc, 0);
	print_message(" closing the RW|EVICT handle\n");
	rc = daos_cont_close(coh0, NULL);
	assert_rc_equal(rc, 0);

	print_message("SUBTEST: EVICT my own RO and RW handles; shall succeed\n");
	print_message(" performing an RO open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RO, &coh1, &info, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(info.ci_nhandles, 1);
	print_message(" performing an RW open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RW, &coh2, &info, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(info.ci_nhandles, 2);
	print_message(" performing an RW|EVICT open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RW | DAOS_COO_EVICT, &coh0, &info, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(info.ci_nhandles, 1);
	print_message(" closing the RW handle to avoid local resource leaks\n");
	rc = daos_cont_close(coh2, NULL);
	assert_rc_equal(rc, 0);
	print_message(" closing the RO handle to avoid local resource leaks\n");
	rc = daos_cont_close(coh1, NULL);
	assert_rc_equal(rc, 0);
	print_message(" closing the RW|EVICT handle\n");
	rc = daos_cont_close(coh0, NULL);
	assert_rc_equal(rc, 0);

	print_message("SUBTEST: RO|EVICT_ALL and RW|EVICT_ALL are not supported\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RO | DAOS_COO_EVICT_ALL, &coh0, NULL,
			    NULL);
	assert_rc_equal(rc, -DER_INVAL);
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RW | DAOS_COO_EVICT_ALL, &coh0, NULL,
			    NULL);
	assert_rc_equal(rc, -DER_INVAL);

	print_message("SUBTEST: EVICT_ALL no existing handles; shall succeed\n");
	print_message(" performing an EX|EVICT_ALL open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_EX | DAOS_COO_EVICT_ALL, &coh0, &info,
			    NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(info.ci_nhandles, 1);
	print_message(" closing the handle\n");
	rc = daos_cont_close(coh0, NULL);
	assert_rc_equal(rc, 0);

	print_message("SUBTEST: EVICT_ALL my own exclusive handle; shall succeed\n");
	print_message(" performing an EX open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_EX, &coh1, &info, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(info.ci_nhandles, 1);
	print_message(" performing an EX|EVICT_ALL open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_EX | DAOS_COO_EVICT_ALL, &coh0, &info,
			    NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(info.ci_nhandles, 1);
	print_message(" closing the EX handle to avoid local resource leaks\n");
	rc = daos_cont_close(coh1, NULL);
	assert_rc_equal(rc, 0);
	print_message(" closing the EX|EVICT_ALL handle\n");
	rc = daos_cont_close(coh0, NULL);
	assert_rc_equal(rc, 0);

	print_message("SUBTEST: EVICT_ALL my own RO and RW handles; shall succeed\n");
	print_message(" performing an RO open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RO, &coh1, &info, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(info.ci_nhandles, 1);
	print_message(" performing an RW open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_RW, &coh2, &info, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(info.ci_nhandles, 2);
	print_message(" performing an EX|EVICT_ALL open\n");
	rc = daos_cont_open(arg->pool.poh, label, DAOS_COO_EX | DAOS_COO_EVICT_ALL, &coh0, &info,
			    NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(info.ci_nhandles, 1);
	print_message(" closing the RW handle to avoid local resource leaks\n");
	rc = daos_cont_close(coh2, NULL);
	assert_rc_equal(rc, 0);
	print_message(" closing the RO handle to avoid local resource leaks\n");
	rc = daos_cont_close(coh1, NULL);
	assert_rc_equal(rc, 0);
	print_message(" closing the EX|EVICT_ALL handle\n");
	rc = daos_cont_close(coh0, NULL);
	assert_rc_equal(rc, 0);

	print_message("destroying container '%s'\n", label);
	rc = daos_cont_destroy(arg->pool.poh, label, 0 /* force */, NULL);
	assert_rc_equal(rc, 0);
}

static int
co_setup_sync(void **state)
{
	async_disable(state);
	return test_setup(state, SETUP_CONT_CONNECT, true, SMALL_POOL_SIZE,
			  0, NULL);
}

static int
co_setup_async(void **state)
{
	async_enable(state);
	return test_setup(state, SETUP_CONT_CONNECT, true, SMALL_POOL_SIZE,
			  0, NULL);
}

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CONNECT, true, SMALL_POOL_SIZE,
			  0, NULL);
}

static const struct CMUnitTest co_tests[] = {
    {"CONT1: create/open/query/close/destroy container", co_create, async_disable,
     test_case_teardown},
    {"CONT2: create/open/query/close/destroy container (async)", co_create, async_enable,
     test_case_teardown},
    {"CONT3: container handle local2glocal and global2local", co_create, hdl_share_enable,
     test_case_teardown},
    {"CONT4: set/get/list user-defined container attributes (sync)", co_attribute, co_setup_sync,
     test_case_teardown},
    {"CONT5: set/get/list user-defined container attributes (async)", co_attribute, co_setup_async,
     test_case_teardown},
    {"CONT6: create container with properties and query", co_properties, NULL, test_case_teardown},
    {"CONT7: retry CONT_{CLOSE,DESTROY,QUERY}", co_op_retry, NULL, test_case_teardown},
    {"CONT8: get/set container ACL", co_acl, NULL, test_case_teardown},
    {"CONT9: container set prop", co_set_prop, NULL, test_case_teardown},
    {"CONT10: container create access denied", co_create_access_denied, NULL, test_case_teardown},
    {"CONT11: container destroy access denied", co_destroy_access_denied, NULL, test_case_teardown},
    {"CONT12: container destroy allowed by pool ACL only", co_destroy_allowed_by_pool, NULL,
     test_case_teardown},
    {"CONT13: container open access by ACL", co_open_access, NULL, test_case_teardown},
    {"CONT14: container query access by ACL", co_query_access, NULL, test_case_teardown},
    {"CONT15: container get-acl access by ACL", co_get_acl_access, NULL, test_case_teardown},
    {"CONT16: container set-prop access by ACL", co_set_prop_access, NULL, test_case_teardown},
    {"CONT17: container overwrite/update/delete ACL access by ACL", co_modify_acl_access, NULL,
     test_case_teardown},
    {"CONT18: container set owner", co_set_owner, NULL, test_case_teardown},
    {"CONT19: container set-owner access by ACL", co_set_owner_access, NULL, test_case_teardown},
    {"CONT20: container destroy force", co_destroy_force, NULL, test_case_teardown},
    {"CONT21: container owner has implicit ACL access", co_owner_implicit_access, NULL,
     test_case_teardown},
    {"CONT22: container get/set attribute access by ACL", co_attribute_access, NULL,
     test_case_teardown},
    {"CONT23: container open failed/destroy", co_open_fail_destroy, NULL, test_case_teardown},
    {"CONT24: container RF simple test", co_rf_simple, NULL, test_case_teardown},
    {"CONT25: Delete Container during Aggregation", delet_container_during_aggregation,
     co_setup_async, test_case_teardown},
    {"CONT26: container API compat", co_api_compat, NULL, test_case_teardown},
    {"CONT27: container REDUN_LVL and RF test", co_redun_lvl, NULL, test_case_teardown},
    {"CONT28: container metadata times test (sync)", co_mdtimes, co_setup_sync, test_case_teardown},
    {"CONT29: container metadata times test (async)", co_mdtimes, co_setup_async,
     test_case_teardown},
    {"CONT30: daos_cont_global2local failure test", co_global2local_fail_test, NULL,
     test_case_teardown},
    {"CONT31: container open/query number of handles (hdl_share)", co_nhandles, hdl_share_enable,
     test_case_teardown},
    {"CONT32: container get perms", co_get_perms, NULL, test_case_teardown},
    {"CONT33: exclusive open", co_exclusive_open, NULL, test_case_teardown},
    {"CONT34: evict handles", co_evict_hdls, NULL, test_case_teardown},
};

int
run_daos_cont_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(co_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests("DAOS_Container", co_tests,
				ARRAY_SIZE(co_tests), sub_tests, sub_tests_size,
				setup, test_teardown);

	par_barrier(PAR_COMM_WORLD);
	return rc;
}
