/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/pool.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include <daos_security.h>
#include "daos_test.h"
#include "daos_iotest.h"

/** connect to non-existing pool */
static void
pool_connect_nonexist(void **state)
{
	test_arg_t	*arg = *state;
	uuid_t		 uuid;
	char		 str[37];
	daos_handle_t	 poh;
	int		 rc;

	par_barrier(PAR_COMM_WORLD);

	if (arg->myrank != 0)
		return;

	/* Contact pool service replicas as returned by pool create */
	uuid_generate(uuid);
	uuid_unparse(uuid, str);
	rc = daos_pool_connect(str, arg->group, DAOS_PC_RW,
			       &poh, NULL /* info */, NULL /* ev */);
	assert_rc_equal(rc, -DER_NONEXIST);
}

/** connect/disconnect to/from a valid pool */
static void
pool_connect(void **state)
{
	test_arg_t	*arg = *state;
	daos_handle_t	 poh;
	daos_event_t	 ev;
	daos_pool_info_t info = {0};
	int		 rc;

	par_barrier(PAR_COMM_WORLD);

	if (!arg->hdl_share && arg->myrank != 0)
		return;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}

	if (arg->myrank == 0) {
		/** connect to pool */
		print_message("rank 0 connecting to pool %ssynchronously ... ",
			      arg->async ? "a" : "");
		rc = daos_pool_connect(arg->pool.pool_str, arg->group,
				       DAOS_PC_RW, &poh, &info,
				       arg->async ? &ev : NULL /* ev */);
		assert_rc_equal(rc, 0);
		WAIT_ON_ASYNC(arg, ev);
		assert_memory_equal(info.pi_uuid, arg->pool.pool_uuid,
				    sizeof(info.pi_uuid));
		/** TODO: assert_int_equal(info.pi_ntargets, arg->...); */
		assert_int_equal(info.pi_ndisabled, 0);
		print_message("success\n");

		print_message("rank 0 querying pool info... ");
		memset(&info, 'D', sizeof(info));
		info.pi_bits = DPI_ALL;
		rc = daos_pool_query(poh, NULL /* ranks */, &info, NULL,
				     arg->async ? &ev : NULL /* ev */);
		assert_rc_equal(rc, 0);
		WAIT_ON_ASYNC(arg, ev);
		assert_int_equal(info.pi_ndisabled, 0);
		print_message("success\n");
	}

	if (arg->hdl_share)
		handle_share(&poh, HANDLE_POOL, arg->myrank, poh, 1);

	/** disconnect from pool */
	print_message("rank %d disconnecting from pool %ssynchronously ... ",
		      arg->myrank, arg->async ? "a" : "");
	rc = daos_pool_disconnect(poh, arg->async ? &ev : NULL /* ev */);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("success\n");

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
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

	par_barrier(PAR_COMM_WORLD);

	if (arg->myrank != 0)
		return;

	print_message("SUBTEST 1: other connections already exist; shall get "
		      "%d\n", -DER_BUSY);
	print_message("establishing a non-exclusive connection\n");
	rc = daos_pool_connect(arg->pool.pool_str, arg->group,
			       DAOS_PC_RW, &poh, NULL /* info */,
			       NULL /* ev */);
	assert_rc_equal(rc, 0);
	print_message("trying to establish an exclusive connection\n");
	rc = daos_pool_connect(arg->pool.pool_str, arg->group,
			       DAOS_PC_EX, &poh_ex, NULL /* info */,
			       NULL /* ev */);
	assert_rc_equal(rc, -DER_BUSY);
	print_message("disconnecting the non-exclusive connection\n");
	rc = daos_pool_disconnect(poh, NULL /* ev */);
	assert_rc_equal(rc, 0);

	print_message("SUBTEST 2: no other connections; shall succeed\n");
	print_message("establishing an exclusive connection\n");
	rc = daos_pool_connect(arg->pool.pool_str, arg->group,
			       DAOS_PC_EX, &poh_ex, NULL /* info */,
			       NULL /* ev */);
	assert_rc_equal(rc, 0);

	print_message("SUBTEST 3: shall prevent other connections (%d)\n",
		      -DER_BUSY);
	print_message("trying to establish a non-exclusive connection\n");
	rc = daos_pool_connect(arg->pool.pool_str, arg->group,
			       DAOS_PC_RW, &poh, NULL /* info */,
			       NULL /* ev */);
	assert_rc_equal(rc, -DER_BUSY);
	print_message("disconnecting the exclusive connection\n");
	rc = daos_pool_disconnect(poh_ex, NULL /* ev */);
	assert_rc_equal(rc, 0);
}

/** exclude a target from the pool */
static void
pool_exclude(void **state)
{
	test_arg_t	*arg = *state;
	daos_handle_t	 poh;
	daos_event_t	 ev;
	daos_pool_info_t info = {0};
	d_rank_t	 rank;
	int		 tgt = -1;
	int		 rc;
	int		 idx;

	par_barrier(PAR_COMM_WORLD);

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
		assert_rc_equal(rc, 0);
	}

	/** connect to pool */
	print_message("rank 0 connecting to pool %ssynchronously... ",
		      arg->async ? "a" : "");
	rc = daos_pool_connect(arg->pool.pool_str, arg->group,
			       DAOS_PC_RW, &poh, &info,
			       arg->async ? &ev : NULL /* ev */);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("success\n");

	/** exclude last non-svc rank */
	if (info.pi_nnodes - 1 /* rank 0 */ <= arg->pool.svc->rl_nr) {
		print_message("not enough non-svc targets; skipping\n");
		goto disconnect;
	}
	rank = info.pi_nnodes - 1;
	print_message("rank 0 excluding rank %u... ", rank);
	/* TODO: remove the loop, call daos_exclude_target passing in the rank just calculated? */
	for (idx = 0; idx < arg->pool.svc->rl_nr; idx++) {
		daos_exclude_target(arg->pool.pool_uuid, arg->group,
				    arg->dmg_config,
				    arg->pool.svc->rl_ranks[idx], tgt);
	}
	WAIT_ON_ASYNC(arg, ev);
	print_message("success\n");

	/* TODO: pass a d_rank_list_t ** into pool query for list of affected engines,
	 * verify rank is in the list.
	 */
	print_message("rank 0 querying pool info... ");
	memset(&info, 'D', sizeof(info));
	rc = daos_pool_query(poh, NULL /* ranks */, &info, NULL,
			     arg->async ? &ev : NULL /* ev */);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	/* TODO: is it expected pi_ndisabled will equal # of targets per engine (not 1)? */
	assert_int_equal(info.pi_ndisabled, 1);
	print_message("success\n");

disconnect:
	/** disconnect from pool */
	print_message("rank %d disconnecting from pool %ssynchronously ... ",
		      arg->myrank, arg->async ? "a" : "");
	rc = daos_pool_disconnect(poh, arg->async ? &ev : NULL /* ev */);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
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
	daos_handle_t	 poh;
	int		 rc;

	char const *const names[] = { "AVeryLongName", "Name" };
	char const *const names_get[] = { "AVeryLongName", "Wrong", "Name" };
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
	int			 m = (int) ARRAY_SIZE(names_get);
	char			 out_buf[10 * BUFSIZE] = { 0 };
	void			*out_values[] = {
						  &out_buf[0 * BUFSIZE],
						  &out_buf[1 * BUFSIZE],
						  &out_buf[2 * BUFSIZE]
						};
	size_t			 out_sizes[] =	{ BUFSIZE, BUFSIZE, BUFSIZE };
	size_t			 total_size;

	par_barrier(PAR_COMM_WORLD);

	if (arg->myrank != 0)
		return;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}

	print_message("connecting to pool\n");
	rc = daos_pool_connect(arg->pool.pool_str, arg->group, DAOS_PC_RW, &poh, NULL, NULL);
	assert_rc_equal(rc, 0);

	print_message("setting pool attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");
	rc = daos_pool_set_attr(poh, n, names, in_values, in_sizes, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("listing pool attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");

	total_size = 0;
	rc = daos_pool_list_attr(poh, NULL, &total_size, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying Total Name Length..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));

	total_size = BUFSIZE;
	rc = daos_pool_list_attr(poh, out_buf, &total_size, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying Small Name..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));
	assert_string_equal(out_buf, names[1]);

	total_size = 10*BUFSIZE;
	rc = daos_pool_list_attr(poh, out_buf, &total_size, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	print_message("Verifying All Names..\n");
	assert_int_equal(total_size, (name_sizes[0] + name_sizes[1]));
	assert_string_equal(out_buf, names[1]);
	assert_string_equal(out_buf + name_sizes[1], names[0]);

	print_message("getting pool attributes %ssynchronously ...\n",
		      arg->async ? "a" : "");

	rc = daos_pool_get_attr(poh, m, names_get, out_values, out_sizes, arg->async ? &ev : NULL);
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

	rc = daos_pool_get_attr(poh, m, names_get, NULL, out_sizes, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying with NULL buffer..\n");
	assert_int_equal(out_sizes[0], in_sizes[0]);
	assert_int_equal(out_sizes[1], 0);
	assert_int_equal(out_sizes[2], in_sizes[1]);

	print_message("Deleting all attributes\n");
	rc = daos_pool_del_attr(poh, m, names_get, arg->async ? &ev : NULL);
	/* should work even if "Wrong" do not exist */
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);

	print_message("Verifying all attributes deletion\n");
	total_size = 0;
	rc = daos_pool_list_attr(poh, NULL, &total_size, arg->async ? &ev : NULL);
	assert_rc_equal(rc, 0);
	WAIT_ON_ASYNC(arg, ev);
	assert_int_equal(total_size, 0);

	print_message("disconnecting from pool\n");
	rc = daos_pool_disconnect(poh, NULL);
	assert_rc_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_fini(&ev);
		assert_rc_equal(rc, 0);
	}
}

/** reconnect to pool after re-initializing DAOS lib */
static void
init_fini_conn(void **state)
{
	test_arg_t		*arg = *state;
	daos_handle_t		 poh;
	int			 rc;

	par_barrier(PAR_COMM_WORLD);

	rc = daos_pool_connect(arg->pool.pool_str, arg->group, DAOS_PC_RW, &poh, NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_pool_disconnect(poh, NULL /* ev */);
	assert_rc_equal(rc, 0);

	rc = daos_eq_destroy(arg->eq, 0);
	assert_rc_equal(rc, 0);

	rc = daos_fini();
	if (rc)
		print_message("daos_fini() failed with %d\n", rc);
	assert_rc_equal(rc, 0);

	rc = daos_init();
	if (rc)
		print_message("daos_init() failed with %d\n", rc);
	assert_rc_equal(rc, 0);

	/* the eq should re-create after daos reinit, as the hash-table
	 * re-inited.
	 */
	rc = daos_eq_create(&arg->eq);
	assert_rc_equal(rc, 0);

	rc = daos_pool_connect(arg->pool.pool_str, arg->group, DAOS_PC_RW, &poh,
			       &arg->pool.pool_info, NULL /* ev */);
	if (rc)
		print_message("daos_pool_connect failed, rc: %d\n", rc);
	else
		print_message("connected to pool, ntarget=%d\n",
			      arg->pool.pool_info.pi_ntargets);
	assert_rc_equal(rc, 0);

	rc = daos_pool_disconnect(poh, NULL /* ev */);
	assert_rc_equal(rc, 0);
}

static bool
ace_has_permissions(struct daos_ace *ace, uint64_t perms)
{
	if (ace->dae_access_types != DAOS_ACL_ACCESS_ALLOW) {
		print_message("Expected access type allow for ACE\n");
		daos_ace_dump(ace, 0);
		return false;
	}

	if (ace->dae_allow_perms != perms) {
		print_message("Expected allow perms %#lx for ACE\n", perms);
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

	if (!ace_has_permissions(ace, DAOS_ACL_PERM_READ |
				      DAOS_ACL_PERM_WRITE)) {
		print_message("Owner ACE was wrong\n");
		return false;
	}

	if (daos_acl_get_ace_for_principal(prop, DAOS_ACL_OWNER_GROUP,
					   NULL, &ace) != 0) {
		print_message("Owner Group ACE not found\n");
		return false;
	}

	acl_expected_len += daos_ace_get_size(ace);

	if (!ace_has_permissions(ace, DAOS_ACL_PERM_READ |
				      DAOS_ACL_PERM_WRITE)) {
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

/** create pool with properties and query */
static void
pool_properties(void **state)
{
	test_arg_t		*arg0 = *state;
	test_arg_t		*arg = NULL;
	char			 label[] = "test_pool_properties";
#if 0 /* DAOS-5456 space_rb props not supported with dmg pool create */
	uint64_t		 space_rb = 36;
#endif
	daos_prop_t		*prop = NULL;
	daos_prop_t		*prop_query;
	struct daos_prop_entry	*entry;
	daos_pool_info_t	 info = {0};
	int			 rc;
	char			*expected_owner;
	char			*expected_group;

	par_barrier(PAR_COMM_WORLD);

	print_message("create pool with properties, and query it to verify.\n");
	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_rc_equal(rc, 0);

	prop = daos_prop_alloc(2);
	/* label - set arg->pool_label to use daos_pool_connect() */
	prop->dpp_entries[0].dpe_type = DAOS_PROP_PO_LABEL;
	D_STRNDUP_S(prop->dpp_entries[0].dpe_str, label);
	assert_ptr_not_equal(prop->dpp_entries[0].dpe_str, NULL);
	D_STRNDUP_S(arg->pool_label, label);
	assert_ptr_not_equal(arg->pool_label, NULL);

	prop->dpp_entries[1].dpe_type = DAOS_PROP_PO_SCRUB_MODE;
	prop->dpp_entries[1].dpe_val = DAOS_SCRUB_MODE_TIMED;

#if 0 /* DAOS-5456 space_rb props not supported with dmg pool create */
	/* change daos_prop_alloc() above, specify 2 entries not 1 */
	prop->dpp_entries[1].dpe_type = DAOS_PROP_PO_SPACE_RB;
	prop->dpp_entries[1].dpe_val = space_rb;
#endif

	while (!rc && arg->setup_state != SETUP_POOL_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, prop, NULL);
	assert_rc_equal(rc, 0);

	if (arg->myrank == 0) {
		rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_debug_set_params(arg->group, info.pi_leader,
			DMG_KEY_FAIL_LOC, DAOS_FORCE_PROP_VERIFY, 0, NULL);
		assert_rc_equal(rc, 0);
	}
	par_barrier(PAR_COMM_WORLD);

	prop_query = daos_prop_alloc(0);
	rc = daos_pool_query(arg->pool.poh, NULL, NULL, prop_query, NULL);
	assert_rc_equal(rc, 0);

	assert_int_equal(prop_query->dpp_nr, DAOS_PROP_PO_NUM);
	/* set properties should get the value user set */
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_LABEL);
	if (entry == NULL || strcmp(entry->dpe_str, label) != 0) {
		print_message("label verification filed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
#if 0 /* DAOS-5456 space_rb props not supported with dmg pool create */
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_SPACE_RB);
	if (entry == NULL || entry->dpe_val != space_rb) {
		print_message("space_rb verification filed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
#endif
	/* not set properties should get default value */
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_SELF_HEAL);
	if (entry == NULL ||
	    entry->dpe_val != (DAOS_SELF_HEAL_AUTO_EXCLUDE |
			       DAOS_SELF_HEAL_AUTO_REBUILD)) {
		print_message("self-heal verification filed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_RECLAIM);
	if (entry == NULL || entry->dpe_val != DAOS_RECLAIM_LAZY) {
		print_message("reclaim verification filed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_ACL);
	if (entry == NULL || entry->dpe_val_ptr == NULL ||
	    !is_acl_prop_default((struct daos_acl *)entry->dpe_val_ptr)) {
		print_message("ACL prop verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	/* default owner should be effective uid */
	assert_int_equal(daos_acl_uid_to_principal(geteuid(), &expected_owner),
			 0);
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_OWNER);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, expected_owner,
		    DAOS_ACL_MAX_PRINCIPAL_LEN)) {
		print_message("Owner prop verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	/* default owner-group should be effective gid */
	assert_int_equal(daos_acl_gid_to_principal(getegid(), &expected_group),
			 0);
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_OWNER_GROUP);
	if (entry == NULL || entry->dpe_str == NULL ||
	    strncmp(entry->dpe_str, expected_group,
		    DAOS_ACL_MAX_PRINCIPAL_LEN)) {
		print_message("Owner-group prop verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_SCRUB_MODE);
	if (entry == NULL || entry->dpe_val != DAOS_SCRUB_MODE_OFF)
		fail_msg("scrubber sched verification failed.\n");

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_SCRUB_FREQ);
	if (entry == NULL) {
		print_message("scrubber frequency verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_SCRUB_THRESH);
	if (entry == NULL) {
		print_message("scrubber threshold verification failed.\n");
		assert_int_equal(rc, 1); /* fail the test */
	}

	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	par_barrier(PAR_COMM_WORLD);

	daos_prop_free(prop);
	daos_prop_free(prop_query);
	test_teardown((void **)&arg);
}

static void
pool_op_retry(void **state)
{
	test_arg_t	*arg = *state;
	daos_handle_t	 poh;
	daos_pool_info_t info = {0};
	d_rank_list_t	*engine_ranks = NULL;
	int		 rc;

	par_barrier(PAR_COMM_WORLD);

	if (arg->myrank != 0)
		return;

	print_message("setting on leader %u DAOS_POOL_CONNECT_FAIL_CORPC ... ",
		arg->pool.pool_info.pi_leader);
	rc = daos_debug_set_params(arg->group, arg->pool.pool_info.pi_leader, DMG_KEY_FAIL_LOC,
				   DAOS_POOL_CONNECT_FAIL_CORPC | DAOS_FAIL_ONCE, 0, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("connecting to pool ... ");
	rc = daos_pool_connect(arg->pool.pool_str, arg->group,
			       DAOS_PC_RW, &poh, &info,
			       NULL /* ev */);
	assert_rc_equal(rc, 0);
	assert_memory_equal(info.pi_uuid, arg->pool.pool_uuid,
			    sizeof(info.pi_uuid));
	assert_int_equal(info.pi_ndisabled, 0);
	print_message("success\n");

	print_message("setting on leader %u DAOS_POOL_QUERY_FAIL_CORPC ... ", info.pi_leader);
	rc = daos_debug_set_params(arg->group, info.pi_leader, DMG_KEY_FAIL_LOC,
				   DAOS_POOL_QUERY_FAIL_CORPC | DAOS_FAIL_ONCE, 0, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	print_message("querying pool info... ");
	memset(&info, 'D', sizeof(info));
	info.pi_bits = DPI_ALL;
	rc = daos_pool_query(poh, &engine_ranks, &info, NULL, NULL /* ev */);
	assert_rc_equal(rc, 0);
	assert_int_equal(info.pi_ndisabled, 0);
	assert_ptr_not_equal(engine_ranks, NULL);
	assert_int_not_equal(engine_ranks->rl_nr, 0);
	print_message("no disabled targets and %u pool storage engine ranks... success\n",
		      engine_ranks->rl_nr);

	print_message("setting on leader %u DAOS_POOL_DISCONNECT_FAIL_CORPC ... ", info.pi_leader);
	rc = daos_debug_set_params(arg->group, info.pi_leader, DMG_KEY_FAIL_LOC,
				  DAOS_POOL_DISCONNECT_FAIL_CORPC | DAOS_FAIL_ONCE, 0, NULL);
	assert_rc_equal(rc, 0);
	print_message("success\n");

	/** disconnect from pool */
	print_message("disconnecting from pool ... ");
	rc = daos_pool_disconnect(poh, NULL /* ev */);
	assert_rc_equal(rc, 0);
	print_message("success\n");
}

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CREATE, true, SMALL_POOL_SIZE,
			  0, NULL);
}

/* Private definition for void * typed test_arg_t.pool_lc_args */
struct test_list_cont {
	struct test_pool	 tpool;
	daos_size_t		 nconts;
	uuid_t			*conts;
};

static int
setup_containers(void **state, daos_size_t nconts)
{
	test_arg_t		*arg = *state;
	struct test_list_cont	*lcarg = NULL;
	int			 i;
	int			 rc = 0;
	d_rank_list_t		tmp_list;

	D_ALLOC_PTR(lcarg);
	if (lcarg == NULL)
		goto err;

	/***** First, create a pool in which containers will be created *****/

	/* Set some properties in the in/out tpool struct */
	lcarg->tpool.poh = DAOS_HDL_INVAL;
	tmp_list.rl_nr = svc_nreplicas;
	tmp_list.rl_ranks = lcarg->tpool.ranks;
	d_rank_list_dup(&lcarg->tpool.svc, &tmp_list);
	lcarg->tpool.pool_size = 1 << 28;	/* 256MB SCM */
	/* Create the pool */
	rc = test_setup_pool_create(state, NULL /* ipool */, &lcarg->tpool,
				    NULL /* prop */);
	if (rc != 0) {
		print_message("setup: pool creation failed: %d\n", rc);
		goto err_free_lcarg;
	}

	/* TODO: make test_setup_pool_connect() more generic, call here */
	if (arg->myrank == 0) {
		rc = daos_pool_connect(lcarg->tpool.pool_str, arg->group,
				       DAOS_PC_RW,
				       &lcarg->tpool.poh, NULL /* pool info */,
				       NULL /* ev */);
		if (rc != 0)
			print_message("setup: daos_pool_connect failed: %d\n",
				      rc);
	}

	if (arg->multi_rank) {
		par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);
		if (rc == 0) {
			handle_share(&lcarg->tpool.poh, HANDLE_POOL,
				     arg->myrank, lcarg->tpool.poh, 0);
		}
	}

	if (rc != 0)
		goto err_destroy_pool;
	print_message("setup: connected to pool: "DF_UUIDF"\n",
		      DP_UUID(lcarg->tpool.pool_uuid));

	/***** Create many containers in the pool *****/

	if (nconts) {
		D_ALLOC_ARRAY(lcarg->conts, nconts);
		assert_ptr_not_equal(lcarg->conts, NULL);
		print_message("setup: alloc lcarg->conts len %zu\n",
			      nconts);
	}

	for (i = 0; i < nconts; i++) {
		/* TODO: make test_setup_cont_create() generic, call here */
		if (arg->myrank == 0) {
			char	clabel[DAOS_PROP_LABEL_MAX_LEN+1];

			rc = snprintf(clabel, sizeof(clabel), "daos_pool_test_container_%d", i);
			assert_true(rc > 0);
			rc = daos_cont_create_with_label(lcarg->tpool.poh, clabel, NULL /* prop */,
							 &lcarg->conts[i], NULL /* ev */);
			if (rc != 0)
				print_message("setup: daos_cont_create_with_label failed: %d\n",
					      rc);
			else
				print_message("setup: container %s "DF_UUIDF" created\n", clabel,
					      DP_UUID(lcarg->conts[i]));
		}

		if (arg->multi_rank) {
			par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);
			/** broadcast container UUID */
			if (rc == 0)
				par_bcast(PAR_COMM_WORLD, lcarg->conts[i], sizeof(lcarg->conts[i]),
					  PAR_CHAR, 0);
		}

		if (rc != 0)
			goto err_destroy_conts;
	}

	lcarg->nconts = nconts;
	arg->pool_lc_args = lcarg;
	return 0;

err_destroy_conts:
	if (arg->myrank == 0) {
		for (i = 0; i < nconts; i++) {
			char	clabel[DAOS_PROP_LABEL_MAX_LEN+1];

			if (uuid_is_null(lcarg->conts[i]))
				break;
			rc = snprintf(clabel, sizeof(clabel), "daos_pool_test_container_%d", i);
			assert_true(rc > 0);
			daos_cont_destroy(lcarg->tpool.poh, clabel, 1 /* force */, NULL /* ev */);
		}
	}

err_destroy_pool:
	if (arg->myrank == 0)
		pool_destroy_safe(arg, &lcarg->tpool);

err_free_lcarg:
	if (lcarg->tpool.svc)
		d_rank_list_free(lcarg->tpool.svc);
	D_FREE(lcarg);

err:
	return 1;
}

static int
teardown_containers(void **state)
{
	test_arg_t		*arg = *state;
	struct test_list_cont	*lcarg = arg->pool_lc_args;
	int			 i;
	int			 rc = 0;

	if (lcarg == NULL)
		return 0;

	for (i = 0; i < lcarg->nconts; i++) {
		char str[37];

		if (uuid_is_null(lcarg->conts[i]))
			break;

		if (arg->myrank == 0) {
			print_message("teardown: destroy container: "
				      DF_UUIDF"\n", DP_UUID(lcarg->conts[i]));
			uuid_unparse(lcarg->conts[i], str);
			rc = daos_cont_destroy(lcarg->tpool.poh, str,
					       1, NULL);
		}

		if (arg->multi_rank)
			par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);

		if (rc != 0)
			return rc;
	}

	if (arg->myrank == 0)
		rc = pool_destroy_safe(arg, &lcarg->tpool);

	if (arg->multi_rank)
		par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);

	if (rc != 0)
		return rc;

	lcarg->nconts = 0;
	D_FREE(lcarg->conts);
	D_FREE(arg->pool_lc_args);
	arg->pool_lc_args = NULL;

	return test_case_teardown(state);
}

static int
setup_zerocontainers(void **state)
{
	async_disable(state);
	return setup_containers(state, 0 /* nconts */);
}

static int
setup_manycontainers(void **state)
{
	const daos_size_t nconts = 16;

	async_disable(state);
	return setup_containers(state, nconts);
}

static void
clean_cont_info(daos_size_t nconts, struct daos_pool_cont_info *conts) {
	int	i;

	if (conts) {
		for (i = 0; i < nconts; i++)
			uuid_clear(conts[i].pci_uuid);
	}
}

static void
clean_cont_info2(daos_size_t nconts, struct daos_pool_cont_info2 *conts) {
	if (conts)
		memset(conts, 0, (nconts * sizeof(struct daos_pool_cont_info2)));
}

/* Search for container information in pools created in setup (pool_lc_args)
 * Return matching index or -1 if no match.
 */
static int
find_cont(void **state, uuid_t cuuid)
{
	test_arg_t		*arg = *state;
	struct test_list_cont	*lcarg = arg->pool_lc_args;
	int			 i;
	int			 found_idx = -1;

	for (i = 0; i < lcarg->nconts; i++) {
		if (uuid_compare(cuuid, lcarg->conts[i]) == 0) {
			found_idx = i;
			break;
		}
	}

	print_message("container "DF_UUIDF" %sfound in list result\n",
		      DP_UUID(cuuid), ((found_idx == -1) ? "NOT " : ""));
	return found_idx;
}

/* Find the item in infos[] with UUID matching cuuid */
static struct daos_pool_cont_info2 *
find_cont2(uuid_t cuuid, daos_size_t n_infos, struct daos_pool_cont_info2 *infos)
{
	int	i;

	for (i = 0; i < n_infos; i++) {
		if (uuid_compare(cuuid, infos[i].pci_id.pci_uuid) == 0)
			return &infos[i];
	}
	return NULL;
}

/* Verify container info returned by DAOS API
 * rc_ret:	return code from daos_pool_list_cont()
 * nconts_in:	ncont input argument to daos_pool_list_cont()
 * nconts_out:	ncont output argument value after daos_pool_list_cont()
 */
static void
verify_cont_info(void **state, int rc_ret, daos_size_t nconts_in,
		 struct daos_pool_cont_info *conts, daos_size_t nconts_out)
{
	test_arg_t		*arg = *state;
	struct test_list_cont	*lcarg = arg->pool_lc_args;
	daos_size_t		 nfilled;
	int			 i;
	int			 rc;

	assert_int_equal(nconts_out, lcarg->nconts);

	if (conts == NULL)
		return;

	/* How many entries of conts[] expected to be populated?
	 * In successful calls, nconts_out.
	 */
	nfilled = (rc_ret == 0) ? nconts_out : 0;

	/* Walk through conts[] items daos_pool_list_cont() was told about */
	print_message("verifying conts[0..%zu], nfilled=%zu\n", nconts_in,
		      nfilled);
	for (i = 0; i < nconts_in; i++) {
		if (i < nfilled) {
			/* container is found in the setup state */
			rc = find_cont(state, conts[i].pci_uuid);
			assert_int_not_equal(rc, -1);
		} else {
			/* Expect no content in conts[>=nfilled] */
			rc = uuid_is_null(conts[i].pci_uuid);
			assert_int_not_equal(rc, 0);
		}
	}
}

/* Common function for testing list containers feature.
 * Some tests can only be run when multiple containers have been created,
 * Other tests may run when there are zero or more containers in the pool.
 */
static void
list_containers_test(void **state)
{
	test_arg_t			*arg = *state;
	struct test_list_cont		*lcarg = arg->pool_lc_args;
	int				 rc;
	daos_size_t			 nconts;
	daos_size_t			 nconts_alloc;
	daos_size_t			 nconts_orig;
	struct daos_pool_cont_info	*conts = NULL;
	int				 tnum = 0;

	par_barrier(PAR_COMM_WORLD);

	if (arg->myrank != 0)
		return;

	/***** Test: retrieve number of containers in pool *****/
	nconts = nconts_orig = 0xDEF0; /* Junk value (e.g., uninitialized) */
	assert_true(daos_handle_is_valid(lcarg->tpool.poh));
	rc = daos_pool_list_cont(lcarg->tpool.poh, &nconts, conts,
			NULL /* ev */);
	print_message("daos_pool_list_cont returned rc=%d\n", rc);
	assert_rc_equal(rc, 0);
	verify_cont_info(state, rc, nconts_orig, conts, nconts);

	print_message("success t%d: output nconts=%zu\n", tnum++,
		      lcarg->nconts);

	/* Setup for next 2 tests: alloc conts[] */
	nconts_alloc = lcarg->nconts + 10;
	D_ALLOC_ARRAY(conts, nconts_alloc);
	assert_ptr_not_equal(conts, NULL);

	/***** Test: provide nconts, conts. Expect nconts=lcarg->nconts
	 * and that many items in conts[] filled
	 *****/
	nconts = nconts_alloc;
	rc = daos_pool_list_cont(lcarg->tpool.poh, &nconts, conts,
				 NULL /* ev */);
	assert_rc_equal(rc, 0);
	assert_int_equal(nconts, lcarg->nconts);
	verify_cont_info(state, rc, nconts_alloc, conts, nconts);

	clean_cont_info(nconts_alloc, conts);
	print_message("success t%d: conts[] over-sized\n", tnum++);

	/***** Test: provide nconts=0, non-NULL conts ****/
	nconts = 0;
	rc = daos_pool_list_cont(lcarg->tpool.poh, &nconts, conts,
				 NULL /* ev */);
	assert_rc_equal(rc, 0);
	assert_int_equal(nconts, lcarg->nconts);
	print_message("success t%d: nconts=0, non-NULL conts[] rc=%d\n",
		      tnum++, rc);

	/* Teardown for above 2 tests */
	D_FREE(conts);
	conts = NULL;

	/***** Test: invalid nconts=NULL *****/
	rc = daos_pool_list_cont(lcarg->tpool.poh, NULL /* nconts */, conts, NULL /* ev */);
	assert_rc_equal(rc, -DER_INVAL);
	print_message("success t%d: in &nconts NULL, -DER_INVAL\n", tnum++);


	/*** Tests that can only run with multiple containers ***/
	if (lcarg->nconts > 1) {
		/***** Test: Exact size buffer *****/
		/* Setup */
		nconts_alloc = lcarg->nconts;
		D_ALLOC_ARRAY(conts, nconts_alloc);
		assert_ptr_not_equal(conts, NULL);

		/* Test: Exact size buffer */
		nconts = nconts_alloc;
		rc = daos_pool_list_cont(lcarg->tpool.poh, &nconts, conts,
					  NULL /* ev */);
		assert_rc_equal(rc, 0);
		assert_int_equal(nconts, lcarg->nconts);
		verify_cont_info(state, rc, nconts_alloc, conts, nconts);

		/* Teardown */
		D_FREE(conts);
		conts = NULL;
		print_message("success t%d: conts[] exact length\n", tnum++);

		/***** Test: Under-sized buffer (negative) -DER_TRUNC *****/
		/* Setup */
		nconts_alloc = lcarg->nconts - 1;
		D_ALLOC_ARRAY(conts, nconts_alloc);
		assert_ptr_not_equal(conts, NULL);

		/* Test: Under-sized buffer */
		nconts = nconts_alloc;
		rc = daos_pool_list_cont(lcarg->tpool.poh, &nconts, conts,
					  NULL /* ev */);
		assert_rc_equal(rc, -DER_TRUNC);
		verify_cont_info(state, rc, nconts_alloc, conts, nconts);

		print_message("success t%d: conts[] under-sized\n", tnum++);

		/* Teardown */
		D_FREE(conts);
		conts = NULL;
	} /* if (lcarg->nconts  > 1) */

	print_message("success\n");
}

/* Verify container info returned by DAOS API
 * rc_ret:	return code from daos_pool_filter_cont()
 * nconts_in:	ncont input argument to daos_pool_filter_cont()
 * nconts_out:	ncont output argument value after daos_pool_filter_cont()
 */
static void
verify_cont_info2(void **state, int rc_ret, daos_size_t nconts_in,
		  struct daos_pool_cont_info2 *conts, daos_size_t nconts_out,
		  daos_size_t exp_nconts, uuid_t *exp_cuuids)
{
	daos_size_t		 nfilled;
	int			 i;
	int			 rc;

	assert_int_equal(nconts_out, exp_nconts);

	if (conts == NULL)
		return;

	/* How many entries of conts[] expected to be populated? In successful calls, nconts_out. */
	nfilled = (rc_ret == 0) ? nconts_out : 0;

	if (nfilled > 0)
		assert_ptr_not_equal(exp_cuuids, NULL);

	/* Find all of the expected container uuids in the output */
	print_message("verifying conts[0..%zu], nfilled=%zu\n", nconts_in, nfilled);
	for (i = 0; i < nfilled; i++) {
		struct daos_pool_cont_info2	*found = find_cont2(exp_cuuids[i], nfilled, conts);
		size_t				 idx;

		assert_ptr_not_equal(found, NULL);
		idx = found - conts;
		print_message("expected container "DF_UUID" (%s) found in returned conts[%zu]\n",
			      DP_UUID(exp_cuuids[i]), found->pci_id.pci_label, idx);
	}

	/* Verify no content was filled in conts[>=nfilled] */
	for (i = nfilled; i < nconts_in; i++) {
		rc = uuid_is_null(conts[i].pci_id.pci_uuid);
		assert_int_not_equal(rc, 0);
	}
}

static inline void
init_one_part_filter(daos_pool_cont_filter_t *filt, uint32_t combine_func,
		     daos_pool_cont_filter_part_t *part,
		     uint32_t key, uint32_t compare_func, uint64_t val)
{
	int	rc;

	rc = daos_pool_cont_filter_init(filt, combine_func);
	assert_rc_equal(rc, 0);

	part->pcfp_key = key;
	part->pcfp_func = compare_func;
	part->pcfp_val64 = val;
	rc = daos_pool_cont_filter_add(filt, part);
	assert_rc_equal(rc, 0);
}

static inline void
init_two_part_filter(daos_pool_cont_filter_t *filt, uint32_t combine_func,
		     daos_pool_cont_filter_part_t *p0,
		     uint32_t p0_key, uint32_t p0_comp, uint64_t p0_val,
		     daos_pool_cont_filter_part_t *p1,
		     uint32_t p1_key, uint32_t p1_comp, uint64_t p1_val)
{
	int	rc;

	rc = daos_pool_cont_filter_init(filt, combine_func);
	assert_rc_equal(rc, 0);

	p0->pcfp_key = p0_key;
	p0->pcfp_func = p0_comp;
	p0->pcfp_val64 = p0_val;
	rc = daos_pool_cont_filter_add(filt, p0);
	assert_rc_equal(rc, 0);

	p1->pcfp_key = p1_key;
	p1->pcfp_func = p1_comp;
	p1->pcfp_val64 = p1_val;
	rc = daos_pool_cont_filter_add(filt, p1);
	assert_rc_equal(rc, 0);
}

static void
init_invalid_filter(daos_pool_cont_filter_t *filt) {
	daos_pool_cont_filter_part_t	dummy_part;
	int				i;
	int				rc;

	rc = daos_pool_cont_filter_init(filt, PCF_COMBINE_LOGICAL_AND);
	assert_rc_equal(rc, 0);

	dummy_part.pcfp_key = PCF_KEY_MD_OTIME;
	dummy_part.pcfp_func = PCF_FUNC_EQ;
	dummy_part.pcfp_val64 = 0;

	for (i = 0; i < DAOS_POOL_CONT_FILTER_MAX_NPARTS + 1; i++) {
		rc = daos_pool_cont_filter_add(filt, &dummy_part);
		assert_rc_equal(rc, 0);
	}
}

static inline void
run_filter_check(void **state, daos_pool_cont_filter_t *filt, int exp_rc, daos_size_t nconts,
		 struct daos_pool_cont_info2 *conts, daos_size_t exp_nconts, uuid_t *exp_cuuids,
		 bool cleanup, int tnum)
{
	test_arg_t		*arg = *state;
	struct test_list_cont	*lcarg = arg->pool_lc_args;
	daos_size_t		 nconts_orig;
	int			 rc;

	nconts_orig = nconts;

	switch (filt->pcf_nparts) {
	case 1:
		print_message("testing t%d: (%s %s "DF_U64") %s , expect nconts=%zu\n", tnum,
			      daos_pool_cont_filter_key_str(filt->pcf_parts[0]->pcfp_key),
			      daos_pool_cont_filter_func_str(filt->pcf_parts[0]->pcfp_func),
			      filt->pcf_parts[0]->pcfp_val64,
			      ((filt->pcf_combine_func == PCF_COMBINE_LOGICAL_AND) ? "&&" : "||"),
			      exp_nconts);
		break;
	case 2:
		print_message("testing t%d: ((%s %s "DF_U64") %s (%s %s "DF_U64"))"
			      "expect nconts=%zu\n", tnum,
			      daos_pool_cont_filter_key_str(filt->pcf_parts[0]->pcfp_key),
			      daos_pool_cont_filter_func_str(filt->pcf_parts[0]->pcfp_func),
			      filt->pcf_parts[0]->pcfp_val64,
			      ((filt->pcf_combine_func == PCF_COMBINE_LOGICAL_AND) ? "&&" : "||"),
			      daos_pool_cont_filter_key_str(filt->pcf_parts[1]->pcfp_key),
			      daos_pool_cont_filter_func_str(filt->pcf_parts[1]->pcfp_func),
			      filt->pcf_parts[1]->pcfp_val64,
			      exp_nconts);
		break;
	default:
		print_message("testing t%d: %u part filter, output nconts=%zu\n", tnum,
			      filt->pcf_nparts, nconts);
		break;
	}

	/* Always clean prior results before running */
	clean_cont_info2(lcarg->nconts, conts);

	rc = daos_pool_filter_cont(lcarg->tpool.poh, filt, &nconts, conts, NULL /* ev */);
	assert_rc_equal(rc, exp_rc);
	verify_cont_info2(state, rc, nconts_orig, conts, nconts, exp_nconts, exp_cuuids);

	/* Caller determines whether to clean up results from this execution */
	if (cleanup)
		clean_cont_info2(lcarg->nconts, conts);
	print_message("success t%d\n", tnum);
}

/* Common function for testing filter containers feature.
 * Some tests can only be run when multiple containers have been created,
 * Other tests may run when there are zero or more containers in the pool.
 */
static void
filter_containers_test(void **state)
{
	test_arg_t			*arg = *state;
	struct test_list_cont		*lcarg = arg->pool_lc_args;
	int				 rc;
	daos_size_t			 nconts;
	daos_size_t			 nconts_alloc;
	daos_size_t			 nconts_orig;
	daos_pool_cont_filter_t		 filt;
	daos_pool_cont_filter_part_t	 part0;
	daos_pool_cont_filter_part_t	 part1;
	struct daos_pool_cont_info2	*conts = NULL;
	daos_size_t			 exp_nconts;
	const bool			 CLEAN = true;
	const bool			 NOCLEAN = false;
	int				 tnum = 0;
	int				 exp_rc;

	par_barrier(PAR_COMM_WORLD);

	if (arg->myrank != 0)
		return;

	print_message("starting test\n");

	/***** Test: retrieve number of containers in pool (same as list containers interface) */
	nconts = nconts_orig = 0xDEF0; /* Junk value (e.g., uninitialized) */
	exp_nconts = lcarg->nconts;
	assert_true(daos_handle_is_valid(lcarg->tpool.poh));
	rc = daos_pool_filter_cont(lcarg->tpool.poh, NULL /* filter */, &nconts, conts,
				   NULL /* ev */);
	assert_rc_equal(rc, 0);
	verify_cont_info2(state, rc, nconts_orig, conts, nconts, exp_nconts, NULL);
	print_message("success t%d: output nconts=%zu\n", tnum++, lcarg->nconts);


	/*** First batch of tests match no containers, conts==NULL, and output nconts=0 ***/

	print_message("=== test batch from t%d: match none, input conts==NULL\n", tnum);
	exp_nconts = 0;
	nconts = 0;
	exp_rc = 0;

	/* Test: 1-part filter (AND, nhandles > 0) match none, NULL conts, check nconts=0 */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_NUM_HANDLES, PCF_FUNC_GT, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (OR, nhandles > 0) match none, NULL conts, check nconts=0 */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
			     &part0, PCF_KEY_NUM_HANDLES, PCF_FUNC_GT, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (AND, mtime == 0) match none, NULL conts, check nconts=0 */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_MD_MTIME, PCF_FUNC_EQ, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (OR, mtime == 0) match none, NULL conts, check nconts=0 */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
			     &part0, PCF_KEY_MD_MTIME, PCF_FUNC_EQ, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (AND, otime > 0) match none, NULL conts, check nconts=0 */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_MD_OTIME, PCF_FUNC_GT, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (OR, otime > 0) match none, NULL conts, check nconts=0 */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
			     &part0, PCF_KEY_MD_OTIME, PCF_FUNC_GT, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (AND, snaps >= 1) match none, NULL conts, check nconts=0  */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_GE, 1);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (OR, snaps >= 1) match none, NULL conts, check nconts=0 */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
			     &part0, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_GE, 1);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);


	/*** Second batch of tests match all, conts==NULL, output the number of containers ***/

	print_message("=== test batch from t%d: match all, input conts==NULL\n", tnum);

	exp_nconts = lcarg->nconts;

	/* Test: 1-part filter (AND, nhandles == 0) match all, NULL conts, check nconts */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_NUM_HANDLES, PCF_FUNC_EQ, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (OR, nhandles == 0) match all, NULL conts, check nconts */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
			     &part0, PCF_KEY_NUM_HANDLES, PCF_FUNC_EQ, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (AND, mtime > 0) match all, NULL conts, check nconts */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_MD_MTIME, PCF_FUNC_GT, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (OR, mtime > 0) match all, NULL conts, check nconts */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
			     &part0, PCF_KEY_MD_MTIME, PCF_FUNC_GT, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (AND, otime == 0) match all, NULL conts, check nconts */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_MD_OTIME, PCF_FUNC_EQ, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (OR, otime == 0) match all, NULL conts, check nconts */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
			     &part0, PCF_KEY_MD_OTIME, PCF_FUNC_EQ, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (AND, snaps >= 0) match all, NULL conts, check nconts  */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_GE, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (OR, snaps >= 0) match all, NULL conts, check nconts */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
			     &part0, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_GE, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Setup for next tests: conts[] */
	nconts_alloc = lcarg->nconts + 10;
	D_ALLOC_ARRAY(conts, nconts_alloc);
	assert_ptr_not_equal(conts, NULL);
	nconts = nconts_alloc;


	/*** Third batch repeats batch 1 tests (match none), but with conts != NULL here ***/

	print_message("=== test batch from t%d: match none, input conts!=NULL\n", tnum);
	exp_nconts = 0;

	/* Test: 1-part filter (AND, nhandles > 0) match none, check nconts=0 */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_NUM_HANDLES, PCF_FUNC_GT, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (OR, nhandles > 0) match none, check nconts=0 */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
			     &part0, PCF_KEY_NUM_HANDLES, PCF_FUNC_GT, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (AND, mtime == 0) match none, check nconts=0 and conts */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_MD_MTIME, PCF_FUNC_EQ, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (AND, otime > 0) match none, check nconts=0 and conts */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_MD_OTIME, PCF_FUNC_GT, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (AND, snaps >= 1) match none, check nconts=0 and and conts */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_GE, 1);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, NULL, CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/*** Fourth batch repeats batch 2 tests (match all), but with conts != NULL here ***/

	print_message("=== test batch from t%d: match all, input conts!=NULL\n", tnum);
	exp_nconts = lcarg->nconts;

	/* Test: 1-part filter (AND, nhandles == 0) match all, check nconts and conts */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_NUM_HANDLES, PCF_FUNC_EQ, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, lcarg->conts,
			 CLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (AND, mtime > 0) match all, check nconts and conts */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_MD_MTIME, PCF_FUNC_GT, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, lcarg->conts,
			 CLEAN, tnum++);
	clean_cont_info2(nconts_alloc, conts);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (AND, otime == 0) match all, check nconts and conts */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_MD_OTIME, PCF_FUNC_EQ, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, lcarg->conts,
			 CLEAN, tnum++);
	clean_cont_info2(nconts_alloc, conts);
	daos_pool_cont_filter_fini(&filt);

	/* Test: 1-part filter (AND, snaps >= 0) match all, check nconts and conts */
	/* Do not clean conts contents - use info to set up for next batch of tests */
	init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
			     &part0, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_GE, 0);
	run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, lcarg->conts,
			 NOCLEAN, tnum++);
	daos_pool_cont_filter_fini(&filt);

	if (lcarg-> nconts > 3) {
		struct daos_pool_cont_info2    *info;
		struct daos_pool_cont_info2	c0, c1, c2;	/* first three containers created */
		struct daos_pool_cont_info2	clast;		/* last container created */
		daos_handle_t			c0h, c1h, c2h;	/* container open handles */
		daos_handle_t			clasth;
		daos_epoch_t			clast_epc;
		daos_epoch_t			c0_epc;
		daos_epoch_range_t		epr;
		uint64_t			c1_mtime;	/* create time */
		daos_cont_info_t		clast_ci;
		uuid_t			       *exp_cuuids;
		int				i;

		/*** Set up for fifth and sixth batches ***/

		info = find_cont2(lcarg->conts[0], nconts, conts);
		assert_ptr_not_equal(info, NULL);
		c0 = *info;
		info = find_cont2(lcarg->conts[1], nconts, conts);
		assert_ptr_not_equal(info, NULL);
		c1 = *info;
		info = find_cont2(lcarg->conts[2], nconts, conts);
		assert_ptr_not_equal(info, NULL);
		c2 = *info;
		info = find_cont2(lcarg->conts[lcarg->nconts - 1], nconts, conts);
		assert_ptr_not_equal(info, NULL);
		clast = *info;

		/* Get container create time (metadata modify time) of second container created */
		c1_mtime = c1.pci_cinfo.ci_md_mtime;

		/* open the first three containers created */
		print_message("open container %s\n", c0.pci_id.pci_label);
		rc = daos_cont_open(lcarg->tpool.poh, c0.pci_id.pci_label, DAOS_COO_RW, &c0h,
				    NULL /* info */, NULL /* ev */);
		assert_rc_equal(rc, 0);
		print_message("open container %s\n", c1.pci_id.pci_label);
		rc = daos_cont_open(lcarg->tpool.poh, c1.pci_id.pci_label, DAOS_COO_RO, &c1h,
				    NULL /* info */, NULL /* ev */);
		assert_rc_equal(rc, 0);
		print_message("open container %s\n", c2.pci_id.pci_label);
		rc = daos_cont_open(lcarg->tpool.poh, c2.pci_id.pci_label, DAOS_COO_RO, &c2h,
				    NULL /* info */, NULL /* ev */);
		assert_rc_equal(rc, 0);

		D_ALLOC_ARRAY(exp_cuuids, lcarg->nconts);
		assert_ptr_not_equal(exp_cuuids, NULL);
		for (i = 0; i < lcarg->nconts; i++)
			uuid_clear(exp_cuuids[i]);

		/*** Fifth batch of tests, match some containers, specify NULL for conts arg ***/

		print_message("=== test batch from t%d: match some, input conts==NULL\n", tnum);

		/* Test: 1-part filter (AND, nhandles > 0), match some, NULL conts arg */
		exp_nconts = 3;
		init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
				     &part0, PCF_KEY_NUM_HANDLES, PCF_FUNC_GT, 0);
		run_filter_check(state, &filt, exp_rc, nconts, NULL, exp_nconts, lcarg->conts,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* Test: 1-part filter (OR, nhandles > 0), match some, NULL conts arg */
		init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
				     &part0, PCF_KEY_NUM_HANDLES, PCF_FUNC_GT, 0);
		run_filter_check(state, &filt, exp_rc, nconts, NULL, exp_nconts, lcarg->conts,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* Test: 1-part filter (AND, mtime <= second container create time), match some,
		 * NULL conts arg
		 */
		exp_nconts = 2;
		init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
				     &part0, PCF_KEY_MD_MTIME, PCF_FUNC_LE, c1_mtime);
		run_filter_check(state, &filt, exp_rc, nconts, NULL, exp_nconts, lcarg->conts,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* Test: 1-part filter (OR, mtime <= second container create time), match some,
		 * NULL conts arg
		 */
		init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
				     &part0, PCF_KEY_MD_MTIME, PCF_FUNC_LE, c1_mtime);
		run_filter_check(state, &filt, exp_rc, nconts, NULL, exp_nconts, lcarg->conts,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* Test: 1-part filter (AND, otime != 0) match some open, NULL conts arg */
		exp_nconts = 3;
		init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
				     &part0, PCF_KEY_MD_OTIME, PCF_FUNC_NE, 0);
		run_filter_check(state, &filt, exp_rc, nconts, NULL, exp_nconts, lcarg->conts,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* Test: 1-part filter (OR, otime != 0) match some open, NULL conts arg */
		init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
				     &part0, PCF_KEY_MD_OTIME, PCF_FUNC_NE, 0);
		run_filter_check(state, &filt, exp_rc, nconts, NULL, exp_nconts, lcarg->conts,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* create snapshot on last container created, used by the below 2 tests */
		print_message("open container %s\n", clast.pci_id.pci_label);
		rc = daos_cont_open(lcarg->tpool.poh, clast.pci_id.pci_label, DAOS_COO_RW, &clasth,
				    &clast_ci, NULL /* ev */);
		assert_rc_equal(rc, 0);
		rc = daos_cont_create_snap(clasth, &clast_epc, NULL /* name */, NULL /* ev */);
		assert_rc_equal(rc, 0);
		print_message("created snapshot on container %s, epoch "DF_X64"(%zu)\n",
			      clast.pci_id.pci_label, clast_epc, clast_epc);

		/* Test: 1-part filter (AND, snaps < 1) match some, NULL conts arg */
		exp_nconts = lcarg->nconts - 1;
		init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
				     &part0, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_LT, 1);
		run_filter_check(state, &filt, exp_rc, nconts, NULL, exp_nconts, lcarg->conts,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* Test: 1-part filter (OR, snaps < 1) match some, NULL conts arg, */
		init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
				     &part0, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_LT, 1);
		run_filter_check(state, &filt, exp_rc, nconts, NULL, exp_nconts, lcarg->conts,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* close the container and destroy the snapshot used by the above 2 tests */
		epr.epr_lo = epr.epr_hi = clast_epc;
		print_message("destroy snapshot on container %s, epoch %zu\n",
			      clast.pci_id.pci_label, clast_epc);
		rc = daos_cont_destroy_snap(clasth, epr, NULL /* ev */);
		assert_rc_equal(rc, 0);
		print_message("close container %s\n", clast.pci_id.pci_label);
		rc = daos_cont_close(clasth, NULL /* ev */);
		assert_rc_equal(rc, 0);

		/* Test: 2-part filter (AND, otime > 0, snaps >= 1) match some, NULL conts */
		/* create snapshot on first container (already open) */
		rc = daos_cont_create_snap(c0h, &c0_epc, NULL /* name */, NULL /* ev */);
		assert_rc_equal(rc, 0);
		print_message("created snapshot on container %s, epoch %zu\n",
			      c0.pci_id.pci_label, c0_epc);
		exp_nconts = 1;
		init_two_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
				     &part0, PCF_KEY_MD_OTIME, PCF_FUNC_GT, 0,
				     &part1, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_GE, 1);
		run_filter_check(state, &filt, exp_rc, nconts, NULL, exp_nconts, lcarg->conts,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* Test: 2-part filter (OR, otime > 0, snaps >= 1) match some, NULL conts arg.
		 * Expect 4 matching containers:
		 * First 3 containers currently open (one with a snapshot created on it);
		 * And last container opened previously and closed.
		 */
		exp_nconts = 4;
		uuid_copy(exp_cuuids[0], lcarg->conts[0]);
		uuid_copy(exp_cuuids[1], lcarg->conts[1]);
		uuid_copy(exp_cuuids[2], lcarg->conts[2]);
		uuid_copy(exp_cuuids[3], lcarg->conts[lcarg->nconts - 1]);

		init_two_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
				     &part0, PCF_KEY_MD_OTIME, PCF_FUNC_GT, 0,
				     &part1, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_GE, 1);
		run_filter_check(state, &filt, exp_rc, nconts, NULL, exp_nconts, exp_cuuids,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* Test: 2-part filter (OR, nhandles > 0, snaps >= 1) match some, NULL conts arg.
		 * First 3 containers currently open (one with a snapshot created on it);
		 * Do not match last container opened previously and closed and no snapshots.
		 */
		exp_nconts = 3;		/* exp_cuuids[0..2] == lcarg->conts[0..2] */
		init_two_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
				     &part0, PCF_KEY_NUM_HANDLES, PCF_FUNC_GT, 0,
				     &part1, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_GE, 1);
		run_filter_check(state, &filt, exp_rc, nconts, NULL, exp_nconts, exp_cuuids,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		epr.epr_lo = epr.epr_hi = c0_epc;
		print_message("destroy snapshot on container %s, epoch %zu\n",
			      c0.pci_id.pci_label, c0_epc);
		rc = daos_cont_destroy_snap(c0h, epr, NULL /* ev */);
		assert_rc_equal(rc, 0);


		/*** Sixth batch repeats batch 5 with conts != NULL ***/

		print_message("=== test batch from t%d: match some, input conts!=NULL\n", tnum);

		/* Test: 1-part filter (AND, mtime <= second container create time) match some,
		 * check nconts and conts. Expect 1 (not 2) matches - first container was modified.
		 */
		exp_nconts = 1;
		uuid_copy(exp_cuuids[0], lcarg->conts[1]);
		init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
				     &part0, PCF_KEY_MD_MTIME, PCF_FUNC_LE, c1_mtime);
		run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, exp_cuuids,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* Test: 1-part filter (AND, otime != 0) match some open, check nconts and conts */
		exp_nconts = 4;
		uuid_copy(exp_cuuids[0], lcarg->conts[0]);
		uuid_copy(exp_cuuids[1], lcarg->conts[1]);
		uuid_copy(exp_cuuids[2], lcarg->conts[2]);
		uuid_copy(exp_cuuids[3], lcarg->conts[lcarg->nconts - 1]);

		init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
				     &part0, PCF_KEY_MD_OTIME, PCF_FUNC_NE, 0);
		run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, exp_cuuids,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* create snapshot on last container created, used by the following test */
		print_message("open container %s\n", clast.pci_id.pci_label);
		rc = daos_cont_open(lcarg->tpool.poh, clast.pci_id.pci_label, DAOS_COO_RW, &clasth,
				    &clast_ci, NULL /* ev */);
		assert_rc_equal(rc, 0);
		rc = daos_cont_create_snap(clasth, &clast_epc, NULL /* name */, NULL /* ev */);
		assert_rc_equal(rc, 0);
		print_message("created snapshot on container %s, epoch %zu\n",
			      clast.pci_id.pci_label, clast_epc);

		/* Test: 1-part filter (AND, snaps < 1) match some, check nconts and conts */
		exp_nconts = lcarg->nconts - 1;
		init_one_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
				     &part0, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_LT, 1);
		run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, lcarg->conts,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* close the container and destroy the snapshot used by the above 2 tests */
		epr.epr_lo = epr.epr_hi = clast_epc;
		print_message("destroy snapshot on container %s, epoch %zu\n",
			      clast.pci_id.pci_label, clast_epc);
		rc = daos_cont_destroy_snap(clasth, epr, NULL /* ev */);
		assert_rc_equal(rc, 0);
		print_message("close container %s\n", clast.pci_id.pci_label);
		rc = daos_cont_close(clasth, NULL /* ev */);
		assert_rc_equal(rc, 0);

		/* Test: 2-part filter (AND, otime > 0, snaps >= 1) match some,
		 * check nconts and conts
		 */
		/* create snapshot on first container (already open) */
		rc = daos_cont_create_snap(c0h, &c0_epc, NULL /* name */, NULL /* ev */);
		assert_rc_equal(rc, 0);
		print_message("created snapshot on container %s, epoch %zu\n",
			      c0.pci_id.pci_label, c0_epc);
		exp_nconts = 1;
		init_two_part_filter(&filt, PCF_COMBINE_LOGICAL_AND,
				     &part0, PCF_KEY_MD_OTIME, PCF_FUNC_GT, 0,
				     &part1, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_GE, 1);
		run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, lcarg->conts,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* Test: 2-part filter (OR, otime > 0, snaps >= 1) match some,
		 * check nconts and conts.
		 * Expect 4 matching containers:
		 * First 3 containers currently open (one with a snapshot created on it);
		 * And last container opened previously and closed.
		 */
		exp_nconts = 4;
		uuid_copy(exp_cuuids[0], lcarg->conts[0]);
		uuid_copy(exp_cuuids[1], lcarg->conts[1]);
		uuid_copy(exp_cuuids[2], lcarg->conts[2]);
		uuid_copy(exp_cuuids[3], lcarg->conts[lcarg->nconts - 1]);
		init_two_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
				     &part0, PCF_KEY_MD_OTIME, PCF_FUNC_GT, 0,
				     &part1, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_GE, 1);
		run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, exp_cuuids,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);

		/* Test: 2-part filter (OR, nhandles > 0, snaps >= 1) match some,
		 * check nconts and conts.
		 * First 3 containers currently open (one with a snapshot created on it);
		 * Do not match last container opened previously and closed and no snapshots.
		 */
		exp_nconts = 3;		/* exp_cuuids[0..2] == lcarg->conts[0..2] */
		init_two_part_filter(&filt, PCF_COMBINE_LOGICAL_OR,
				     &part0, PCF_KEY_NUM_HANDLES, PCF_FUNC_GT, 0,
				     &part1, PCF_KEY_NUM_SNAPSHOTS, PCF_FUNC_GE, 1);
		run_filter_check(state, &filt, exp_rc, nconts, conts, exp_nconts, exp_cuuids,
				 CLEAN, tnum++);
		daos_pool_cont_filter_fini(&filt);


		epr.epr_lo = epr.epr_hi = c0_epc;
		print_message("destroy snapshot on container %s, epoch %zu\n",
			      c0.pci_id.pci_label, c0_epc);
		rc = daos_cont_destroy_snap(c0h, epr, NULL /* ev */);
		assert_rc_equal(rc, 0);

		D_FREE(exp_cuuids);
	}	/* Fifth and sixth batches if (lcarg->nconts > 3) */

	/* Test: NULL filter to get list of all containers */
	print_message("testing t%d: NULL filter to get list of all containers\n", tnum);
	nconts = nconts_orig = nconts_alloc;
	exp_nconts = lcarg->nconts;
	rc = daos_pool_filter_cont(lcarg->tpool.poh, NULL /* filt */, &nconts, conts,
				   NULL /* ev */);
	assert_rc_equal(rc, 0);
	verify_cont_info2(state, rc, nconts_orig, conts, nconts, exp_nconts, lcarg->conts);
	print_message("success t%d\n", tnum++);

	/* Test: 10-part filter, invalid input (exceeds limit on pcf_nparts) */
	init_invalid_filter(&filt);
	rc = daos_pool_filter_cont(lcarg->tpool.poh, &filt /* filt */, &nconts, conts,
				   NULL /* ev */);
	assert_rc_equal(rc, -DER_INVAL);
	daos_pool_cont_filter_fini(&filt);
	print_message("success t%d (filter pcf_nparts too large/invalid)\n", tnum++);

	/***** Test: provide nconts=0, non-NULL conts ****/
	nconts = 0;
	rc = daos_pool_filter_cont(lcarg->tpool.poh, NULL /* filter */, &nconts, conts,
				   NULL /* ev */);
	assert_rc_equal(rc, 0);
	assert_int_equal(nconts, lcarg->nconts);
	print_message("success t%d: nconts=0, non-NULL conts[] rc=%d\n", tnum++, rc);

	/* Teardown for above tests */
	D_FREE(conts);
	conts = NULL;

	/***** Test: invalid nconts=NULL *****/
	rc = daos_pool_filter_cont(lcarg->tpool.poh, NULL /* filter */, NULL /* nconts */,
				   conts, NULL /* ev */);
	assert_rc_equal(rc, -DER_INVAL);
	print_message("success t%d: in &nconts NULL, -DER_INVAL\n", tnum++);


	/*** Tests that can only run with multiple containers ***/
	if (lcarg->nconts > 1) {
		/***** Test: Exact size buffer *****/
		/* Setup */
		nconts_alloc = lcarg->nconts;
		D_ALLOC_ARRAY(conts, nconts_alloc);
		assert_ptr_not_equal(conts, NULL);

		/* Test: Exact size buffer */
		nconts = nconts_alloc;
		rc = daos_pool_filter_cont(lcarg->tpool.poh, NULL /* filter */, &nconts, conts,
					   NULL /* ev */);
		assert_rc_equal(rc, 0);
		verify_cont_info2(state, rc, nconts_alloc, conts, nconts, lcarg->nconts,
				  lcarg->conts);

		/* Teardown */
		D_FREE(conts);
		conts = NULL;
		print_message("success t%d: conts[] exact length\n", tnum++);

		/***** Test: Under-sized buffer (negative) -DER_TRUNC *****/
		/* Setup */
		nconts_alloc = lcarg->nconts - 1;
		D_ALLOC_ARRAY(conts, nconts_alloc);
		assert_ptr_not_equal(conts, NULL);

		/* Test: Under-sized buffer */
		nconts = nconts_alloc;
		rc = daos_pool_filter_cont(lcarg->tpool.poh, NULL /* filter */, &nconts, conts,
					   NULL /* ev */);
		assert_rc_equal(rc, -DER_TRUNC);
		verify_cont_info2(state, rc, nconts_alloc, conts, nconts, lcarg->nconts, NULL);

		print_message("success t%d: conts[] under-sized\n", tnum++);

		/* Teardown */
		D_FREE(conts);
		conts = NULL;
	} /* if (lcarg->nconts  > 1) */

	print_message("success\n");
}

static void
expect_pool_connect_access(test_arg_t *arg0, uint64_t perms,
			   uint64_t flags, int exp_result)
{
	test_arg_t	*arg = NULL;
	daos_prop_t	*prop;
	int		 rc;

	rc = test_setup((void **)&arg, SETUP_EQ, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_rc_equal(rc, 0);

	arg->pool.pool_connect_flags = flags;
	prop = get_daos_prop_with_owner_acl_perms(perms,
						  DAOS_PROP_PO_ACL);

	while (!rc && arg->setup_state != SETUP_POOL_CONNECT)
		rc = test_setup_next_step((void **)&arg, NULL, prop, NULL);

	/* Make sure we actually got to pool connect */
	assert_int_equal(arg->setup_state, SETUP_POOL_CONNECT);
	assert_rc_equal(rc, exp_result);

	daos_prop_free(prop);
	test_teardown((void **)&arg);
}

static void
pool_connect_access(void **state)
{
	test_arg_t	*arg0 = *state;

	par_barrier(PAR_COMM_WORLD);

	print_message("pool ACL gives the owner no permissions\n");
	expect_pool_connect_access(arg0, 0, DAOS_PC_RO, -DER_NO_PERM);

	print_message("pool ACL gives the owner RO, they want RW\n");
	expect_pool_connect_access(arg0, DAOS_ACL_PERM_READ, DAOS_PC_RW,
				   -DER_NO_PERM);

	print_message("pool ACL gives the owner RO, they want RO\n");
	expect_pool_connect_access(arg0, DAOS_ACL_PERM_READ, DAOS_PC_RO,
				   0);

	print_message("pool ACL gives the owner RW, they want RO\n");
	expect_pool_connect_access(arg0,
				   DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				   DAOS_PC_RO,
				   0);

	print_message("pool ACL gives the owner RW, they want RW\n");
	expect_pool_connect_access(arg0,
				   DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE,
				   DAOS_PC_RW,
				   0);
}

static void
label_strings_test(void **state)
{
	int		 i;
	test_arg_t	*arg = *state;
	const char	*valid_labels[] = {
					   "mypool",
					   "my_pool",
					   "MyPool",
					   "MyPool_2",
					   "cae61c0752f54874ad213c0ec43005cb",
					   "bash",
					   "Pool_ProjectA:Team42",
					   "ProjectA.TeamOne",
					   "server42.fictionaldomaincae61c07.org",
					     "0ABC",
					     "0xDA0S1234",
					     "0b101010",
					     /* len=DAOS_PROP_LABEL_MAX_LEN */
					     "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDE",
					     "a-b-cde",
					     "thiswoul-dntp-arse-asau-uidsoitsfine",
					     "g006b637-c63a-4734-99bc-a71298597de1",
	};
	const char	*invalid_labels[] = {
					     "",
					     "no/slashes\\at\\all",
					     "no spaces",
					     "is%20this%20label%20ok",
					     "No{brackets}",
					     "Whatsup?",
					     "'MyLabel'",
					     "MyPool!",
					     "0006b637-c63a-4734-99bc-a71298597de1",
					     "cae61c0]7-52f5-4874-ad21-3c0ec43005cb",
					     /* len=DAOS_PROP_LABEL_MAX_LEN+1 */
					     "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
					     "A0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789",
					     "/bin/bash;/bin/ls",
					     "bash&",
					     "$VAR",
					     "@daos_dev",
					     "#objectstorage",
					     "container7%BigKVS",
					     "onethousand^6",
					     "*myptr",
					     "Project(small_pool)",
					     "pool+containers+objects",
					     "`cmd`",
					     "~daosuser",
					     "don't\"quote\"me",
					     "ooo\bps",
					     "end of the line\n",
					     "end of the line\r\n",	};
	size_t	n_valid = ARRAY_SIZE(valid_labels);
	size_t	n_invalid = ARRAY_SIZE(invalid_labels);

	par_barrier(PAR_COMM_WORLD);

	if (arg->myrank == 0) {
		print_message("Verify %zu valid labels\n", n_valid);
		for (i = 0; i < n_valid; i++) {
			const char *lbl = valid_labels[i];

			print_message("%s should be valid\n", lbl);
			assert_true(daos_label_is_valid(lbl));
		}

		print_message("Verify %zu INvalid labels\n", n_invalid);
		for (i = 0; i < n_invalid; i++) {
			const char *lbl = invalid_labels[i];

			print_message("%s should not be valid\n", lbl);
			assert_false(daos_label_is_valid(lbl));
		}
	}
}

static void
pool_map_refreshes_common(void **state, bool fall_back)
{
	test_arg_t	*arg = *state;
	d_rank_t	 rank = ranks_to_kill[0];
	int		 tgt = 0;

	par_barrier(PAR_COMM_WORLD);

	/*
	 * Since the rebuild_single_pool_target call below refreshes the pool
	 * map of arg->pool.poh, we must use a separate connection, which is
	 * mostly clearly done with another client rank.
	 */
	if (arg->rank_size < 2) {
		print_message("need at least two client ranks\n");
		skip();
	}

	if (!test_runable(arg, 2))
		skip();

	rebuild_single_pool_target(arg, rank, tgt, false);

	if (arg->myrank == 1) {
		uint64_t	 fail_loc;
		int		 n = 4;
		daos_obj_id_t	 oids[n];
		struct ioreq	 reqs[n];
		const char	*akey = "pmr_akey";
		const char	*value = "d";
		daos_size_t	 iod_size = 1;
		int		 rx_nr = 1;
		uint64_t	 idx = 0;
		int		 i;

		for (i = 0; i < n; i++) {
			oids[i] = daos_test_oid_gen(arg->coh, OC_RP_2G1, 0, 0, i);
			ioreq_init(&reqs[i], arg->coh, oids[i], DAOS_IOD_SINGLE, arg);
		}

		print_message("rank 1: setting fail_loc DAOS_POOL_FAIL_MAP_REFRESH\n");
		if (fall_back)
			fail_loc = DAOS_POOL_FAIL_MAP_REFRESH_SERIOUSLY | DAOS_FAIL_ALWAYS;
		else
			fail_loc = DAOS_POOL_FAIL_MAP_REFRESH | DAOS_FAIL_ONCE;
		daos_fail_loc_set(fail_loc);

		print_message("rank 1: invoking concurrent updates to trigger concurrent pool map "
			      "refreshes\n");
		for (i = 0; i < n; i++)
			insert_nowait("pmr_dkey", 1, &akey, &iod_size, &rx_nr, &idx,
				      (void **)&value, DAOS_TX_NONE, &reqs[i], 0);

		print_message("rank 1: waiting for the updates to complete\n");
		for (i = 0; i < n; i++)
			insert_wait(&reqs[i]);

		print_message("rank 1: clearing fail_loc\n");
		daos_fail_loc_set(0);
	}

	par_barrier(PAR_COMM_WORLD);

	print_message("reintegrating the excluded targets\n");
	reintegrate_single_pool_target(arg, rank, tgt);
}

static int
pool_map_refreshes_setup(void **state)
{
	async_enable(state);
	return test_setup(state, SETUP_CONT_CONNECT, true, SMALL_POOL_SIZE, 0, NULL);
}

static void
pool_map_refreshes(void **state)
{
	pool_map_refreshes_common(state, false /* fall_back */);
}

static void
pool_map_refreshes_fallback(void **state)
{
	pool_map_refreshes_common(state, true /* fall_back */);
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
	{ "POOL6: exclude targets and query pool info",
	  pool_exclude, async_disable, NULL},
	{ "POOL7: set/get/list user-defined pool attributes (sync)",
	  pool_attribute, NULL, test_case_teardown},
	{ "POOL8: set/get/list user-defined pool attributes (async)",
	  pool_attribute, NULL, test_case_teardown},
	{ "POOL9: pool reconnect after daos re-init",
	  init_fini_conn, NULL, test_case_teardown},
	{ "POOL10: pool create with properties and query",
	  pool_properties, NULL, test_case_teardown},
	{ "POOL11: pool list containers (zero)",
	  list_containers_test, setup_zerocontainers, teardown_containers},
	{ "POOL12: pool list containers (many)",
	  list_containers_test, setup_manycontainers, teardown_containers},
	{ "POOL13: retry POOL_{CONNECT,DISCONNECT,QUERY}",
	  pool_op_retry, NULL, test_case_teardown},
	{ "POOL14: pool connect access based on ACL",
	  pool_connect_access, NULL, test_case_teardown},
	{ "POOL15: label property string validation",
	  label_strings_test, NULL, test_case_teardown},
	{ "POOL16: pool map refreshes",
	  pool_map_refreshes, pool_map_refreshes_setup, test_case_teardown},
	{ "POOL17: pool map refreshes (fallback)",
	  pool_map_refreshes_fallback, pool_map_refreshes_setup, test_case_teardown},
	{ "POOL18: pool filter containers (zero)",
	  filter_containers_test, setup_zerocontainers, teardown_containers},
	{ "POOL19: pool filter containers (many)",
	  filter_containers_test, setup_manycontainers, teardown_containers},
};

int
run_daos_pool_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc;

	par_barrier(PAR_COMM_WORLD);

	rc = run_daos_sub_tests("DAOS_Pool", pool_tests, ARRAY_SIZE(pool_tests), sub_tests,
				sub_tests_size, setup, test_teardown);

	par_barrier(PAR_COMM_WORLD);
	return rc;
}
