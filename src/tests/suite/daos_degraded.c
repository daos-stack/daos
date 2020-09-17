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
 * tests/suite/daos_replicated.c
 *
 * Replication tests need external interaction, to
 * kill servers and update pool map.
*/
#define D_LOGFAC	DD_FAC(tests)
#include "daos_iotest.h"

int		g_dkeys	  = 1000;

/**
 * Enumerator for Kill op for degraded tests
 */
enum {
	UPDATE,
	LOOKUP,
	ENUMERATE
};

/**
 * Performs insert, lookup, enum of g_dkeys and allow
 * custom operations to be introduced in-between updates/lookups/enum
 *
 * An intermediate op can be a pause, or querying of pool info or
 * sending an dmg rpc kill signal
 */
static void
insert_lookup_enum_with_ops(test_arg_t *arg, int op_kill)
{
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	int			g_dkeys_strlen = 8; /* "999999" */
	const char		*dkey_fmt = "degraded dkey%d";
	const char		akey[] = "degraded akey";
	char			*dkey[g_dkeys], *buf, *ptr;
	char			*dkey_enum;
	char			*rec[g_dkeys];
	char			*val[g_dkeys];
	daos_key_desc_t		kds[g_dkeys];
	daos_anchor_t		anchor_out;
	daos_size_t		rec_size[g_dkeys];
	daos_off_t		offset[g_dkeys];
	const char		*val_fmt = "degraded val%d";
	daos_size_t		val_size[g_dkeys];
	char			*rec_verify;
	uint32_t		number;
	int			rank, key_nr;
	int			enum_op = 1;
	int			size;
	int			rc;
	daos_pool_info_t	info = {0};
	int			enumed = 1;

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	oid = dts_oid_gen(OC_RP_XSF, 0, rank);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	if (!rank) {
		print_message("Using pool: %s\n", DP_UUID(arg->pool.pool_uuid));
		print_message("Inserting %d keys ...\n", g_dkeys * size);
	}

	MPI_Barrier(MPI_COMM_WORLD);
	rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL, NULL);
	assert_int_equal(rc, 0);
	if (info.pi_ntargets - info.pi_ndisabled < 2) {
		if (rank == 0)
			print_message("Not enough active targets, skipping "
				      "(%d/%d)\n", info.pi_ntargets,
				      info.pi_ndisabled);
		skip();
	}
	MPI_Barrier(MPI_COMM_WORLD);

	for (i = 0; i < g_dkeys; i++) {
		D_ALLOC(dkey[i], strlen(dkey_fmt) + g_dkeys_strlen + 1);
		assert_non_null(dkey[i]);
		sprintf(dkey[i], dkey_fmt, i);
		D_ALLOC(rec[i], strlen(val_fmt) + g_dkeys_strlen + 1);
		assert_non_null(rec[i]);
		offset[i] = i * 20;
		D_ALLOC(val[i], 64);
		assert_non_null(val[i]);
		val_size[i] = 64;
	}

	for (i = 0; i < g_dkeys; i++) {
		sprintf(rec[i], val_fmt, i);
		rec_size[i] = strlen(rec[i]);
		D_DEBUG(DF_MISC, "  d-key[%d] '%s' val '%d %s'\n", i,
			dkey[i], (int)rec_size[i], rec[i]);
		insert_single(dkey[i], akey, offset[i], rec[i],
			      rec_size[i], DAOS_TX_NONE, &req);

		if ((i + 1) % (g_dkeys/10) == 0) {
			MPI_Barrier(MPI_COMM_WORLD);
			if (rank == 0)
				print_message("\t%d keys inserted\n",
					      (i + 1) * size);
		}

		/** If the number of updates is half-way inject fault */
		if (op_kill == UPDATE && rank == 0 &&
		    g_dkeys > 1 && (i == g_dkeys/2))
			daos_kill_server(arg, arg->pool.pool_uuid,
					 arg->group, arg->pool.svc, -1);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank == 0)
		print_message("insertion done\nNow looking up %d keys ...\n",
			      g_dkeys * size);

	D_ALLOC(rec_verify, strlen(val_fmt) + g_dkeys_strlen + 1);

	for (i = 0; i < g_dkeys; i++) {
		sprintf(rec_verify, val_fmt, i);
		lookup_single(dkey[i], akey, offset[i], val[i],
			      val_size[i], DAOS_TX_NONE, &req);
		assert_int_equal(req.iod[0].iod_size, strlen(rec_verify));
		assert_memory_equal(val[i], rec_verify, req.iod[0].iod_size);

		if ((i + 1) % (g_dkeys/10) == 0) {
			MPI_Barrier(MPI_COMM_WORLD);
			if (rank == 0)
				print_message("\t%d keys looked up\n",
					      (i + 1) * size);
		}

		/** If the number of lookup is half-way inject fault */
		if (op_kill == LOOKUP && rank == 0 &&
		    g_dkeys > 1 && (i == g_dkeys/2))
			daos_kill_server(arg, arg->pool.pool_uuid,
					 arg->group, arg->pool.svc, -1);
	}
	D_FREE(rec_verify);

	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank == 0)
		print_message("lookup done\nNow enumerating %d keys ...\n",
			      g_dkeys * size);

	memset(&anchor_out, 0, sizeof(anchor_out));
	D_ALLOC(buf, 512);
	if (buf == NULL)
		print_message("[  ERROR   ] Memory allocation for buf"
			      " returned NULL\n");
	assert_non_null(buf);
	D_ALLOC(dkey_enum, strlen(dkey_fmt) + g_dkeys_strlen + 1);

	/** enumerate records */
	for (number = 5, key_nr = 0; !daos_anchor_is_eof(&anchor_out);
	     number = 5) {
		memset(buf, 0, 512);
		enumerate_dkey(DAOS_TX_NONE, &number, kds,
			       &anchor_out, buf, 512, &req);
		if (number == 0)
			continue;

		for (ptr = buf, i = 0; i < number; i++) {
			snprintf(dkey_enum, kds[i].kd_key_len + 1, "%s", ptr);
			D_DEBUG(DF_MISC, "i %d key %s len %d\n", i, dkey_enum,
				(int)kds[i].kd_key_len);
			ptr += kds[i].kd_key_len;
		}
		key_nr += number;

		if (key_nr >= enumed * (g_dkeys/10)) {
			MPI_Barrier(MPI_COMM_WORLD);
			if (rank == 0)
				print_message("\t%d keys enumerated\n",
					      key_nr * size);
			enumed++;
		}

		/** If the number of keys enumerated is half-way inject fault */
		if (op_kill == ENUMERATE && rank == 0 && enum_op &&
		    g_dkeys > 1 && (key_nr  >= g_dkeys/2)) {
			daos_kill_server(arg, arg->pool.pool_uuid,
					 arg->group, arg->pool.svc, -1);
			enum_op = 0;
		}

	}
	assert_int_equal(key_nr, g_dkeys);

	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank == 0)
		print_message("enumeration done\n");
	ioreq_fini(&req);

	for (i = 0; i < g_dkeys; i++) {
		D_FREE(val[i]);
		D_FREE(dkey[i]);
		D_FREE(rec[i]);
	}
	D_FREE(buf);
	D_FREE(dkey_enum);
}

static void
io_degraded_update_demo(void **state)
{
	test_arg_t		*arg = *state;

	insert_lookup_enum_with_ops(arg, UPDATE);
}

static void
io_degraded_lookup_demo(void **state)
{
	test_arg_t		*arg = *state;

	insert_lookup_enum_with_ops(arg, LOOKUP);
}

static void
io_degraded_enum_demo(void **state)
{
	test_arg_t		*arg = *state;

	insert_lookup_enum_with_ops(arg, ENUMERATE);
}

/** create a new pool/container for each test */
static const struct CMUnitTest degraded_tests[] = {
	{"DEGRADED1: Degraded mode during updates",
	 io_degraded_update_demo, NULL, test_case_teardown},
	{"DEGRADED2: Degraded mode during lookup",
	 io_degraded_lookup_demo, NULL, test_case_teardown},
	{"DEGRADED3: Degraded mode during enumerate",
	 io_degraded_enum_demo, NULL, test_case_teardown},
};

static int
degraded_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			  NULL);
}

static int
degraded_teardown(void **state)
{
#if 0
	print_message("degraded_teardown(): At the moment, the daos_pool_destroy() call may time out, since the client MGMT_POOL_DESTROY RPC has the same timeout as the server RDB_STOP RPC(s) to the killed server(s). (Previously, the same issue affects the daos_pool_disconnect() call.)\n");
#endif
	return test_teardown(state);
}

int
run_daos_degraded_test(int rank, int size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	rc = cmocka_run_group_tests_name("DAOS degraded-mode tests",
					 degraded_tests, degraded_setup,
					 degraded_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
