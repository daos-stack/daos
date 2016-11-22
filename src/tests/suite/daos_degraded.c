/**
 *
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
 * tests/suite/daos_replicated.c
 *
 * Replication tests need external interaction, to
 * kill servers and update pool map. So would not be started
 * with the regression tests.
*/
#include "daos_iotest.h"

/**
 *  Introduce arbitrary failures in the test
 *  expand this ops structure to support more failures
 */
struct rtp_test_ops {
	/**
	 * Induce failure op after updates,
	 * takes arbitrary args
	 */
	void	(*rto_after_update)(void *params);
	/**
	 * Indude failure op after lookup,
	 * takes arbitrary args
	 */
	void	(*rto_after_lookup)(void *params);
};

int g_dkeys = 1000;
int sleep_seconds = 1;

static void
insert_lookup_with_wait(test_arg_t *arg, void *params1,
			void *params2, struct rtp_test_ops *test_ops)
{
	daos_obj_id_t		oid;
	struct ioreq		req;
	int			i;
	int			g_dkeys_strlen = 6; /* "999999" */
	const char		*dkey_fmt = "test_update dkey%d";
	const char		akey[] = "test_update akey";
	char			*dkey[g_dkeys], *buf, *ptr;
	char			*dkey_enum;
	char			*rec[g_dkeys];
	char			*val[g_dkeys];
	daos_key_desc_t		kds[g_dkeys];
	daos_hash_out_t		hash_out;
	daos_size_t		rec_size[g_dkeys];
	daos_off_t		offset[g_dkeys];
	const char		*val_fmt = "epoch_discard val%d";
	daos_size_t		val_size[g_dkeys];
	char			*rec_verify;
	daos_epoch_t		epoch;
	uint32_t		number;
	int			rank, key_nr;

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	obj_random(arg, &oid);
	ioreq_init(&req, oid, arg);
	if (!rank) {
		print_message("\n\n=============================\n");
		print_message("Insert %d keys\n", g_dkeys);
		print_message("=============================\n");
	}

	for (i = 0; i < g_dkeys; i++) {
		D_ALLOC(dkey[i],
			strlen(dkey_fmt) + g_dkeys_strlen + 1);
		assert_non_null(dkey[i]);
		sprintf(dkey[i], dkey_fmt, i);
		D_ALLOC(rec[i], strlen(val_fmt) + g_dkeys_strlen + 1);
		assert_non_null(rec[i]);
		offset[i] = i * 20;
		D_ALLOC(val[i], 64);
		assert_non_null(val[i]);
		val_size[i] = 64;
	}

	epoch = 100;
	for (i = 0; i < g_dkeys; i++) {
		sprintf(rec[i], val_fmt, i);
		rec_size[i] = strlen(rec[i]);
		if (!rank)
			D_DEBUG(DF_MISC, "  d-key[%d] '%s' val '%.*s'\n", i,
				dkey[i], (int)rec_size[i], rec[i]);
		insert_single(dkey[i], akey, offset[i], rec[i],
			      rec_size[i], epoch, &req);
	}

	if (arg->myrank == 0) {
		print_message("\n\n=====================================\n");
		print_message("Done %d Updates\nSleeping %u seconds\n",
			      g_dkeys, sleep_seconds);
		print_message("Please kill server and exclude targets\n");
		test_ops->rto_after_update(params1);
		print_message("=====================================\n");

	}

	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank == 0)
		print_message("Now looking up %d keys\n", g_dkeys);

	D_ALLOC(rec_verify, strlen(val_fmt) + g_dkeys_strlen + 1);
	for (i = 0; i < g_dkeys; i++) {
		sprintf(rec_verify, val_fmt, i);
		lookup_single(dkey[i], akey, offset[i], val[i],
			      val_size[i], epoch, &req);
		assert_int_equal(req.rex[0][0].rx_rsize, strlen(rec_verify));
		assert_memory_equal(val[i], rec_verify, req.rex[0][0].rx_rsize);
	}
	free(rec_verify);


	if (arg->myrank == 0) {
		print_message("\n\n=====================================\n");
		test_ops->rto_after_lookup(params2);
		print_message("=====================================\n");
	}

	MPI_Barrier(MPI_COMM_WORLD);

	if (arg->myrank == 0)
		print_message("Now enumerating %d keys\n", g_dkeys);

	memset(&hash_out, 0, sizeof(hash_out));
	D_ALLOC(buf, 512);
	D_ALLOC(dkey_enum, strlen(dkey_fmt) + g_dkeys_strlen + 1);

	/** enumerate records */
	for (number = 5, key_nr = 0; !daos_hash_is_eof(&hash_out);
	     number = 5) {
		memset(buf, 0, 512);
		enumerate_dkey(0, &number, kds, &hash_out, buf, 512, &req);
		if (number == 0)
			continue;

		key_nr += number;
		for (ptr = buf, i = 0; i < number; i++) {
			snprintf(dkey_enum, kds[i].kd_key_len + 1, ptr);
			D_DEBUG(DF_MISC, "i %d key %s len %d\n", i, dkey_enum,
				(int)kds[i].kd_key_len);
			ptr += kds[i].kd_key_len;
		}
	}
	assert_int_equal(key_nr, g_dkeys);

	if (arg->myrank == 0) {
		print_message("\n\n================================\n");
		print_message("Done %d Enumerations\n\n", g_dkeys);
		print_message("Test Complete\n");
		print_message("================================\n");
	}

	ioreq_fini(&req);

	for (i = 0; i < g_dkeys; i++) {
		D_FREE(val[i], 64);
		D_FREE(dkey[i], strlen(dkey_fmt) + g_dkeys_strlen + 1);
		D_FREE(rec[i], strlen(val_fmt) + g_dkeys_strlen + 1);
	}
	D_FREE(buf, 512);
	D_FREE(dkey_enum,  strlen(dkey_fmt) + g_dkeys_strlen + 1);


}

static void
sleep_wait(void *params)
{
	int sleep_seconds = *(int *)params;

	print_message("Test Waiting for %d\n", sleep_seconds);
	sleep(sleep_seconds);
}

static void
dpool_query(void *params)
{
	int			rc;
	test_arg_t		*arg = (test_arg_t *)params;
	daos_pool_info_t	info;

	rc = daos_pool_query(arg->poh, NULL, &info, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(info.pi_ndisabled, 1);
}

static void
io_replicated_rw_demo(void **state)
{
	test_arg_t		*arg = *state;
	struct rtp_test_ops	ops;

	ops.rto_after_update	= sleep_wait;
	ops.rto_after_lookup	= dpool_query;
	insert_lookup_with_wait(arg, &sleep_seconds, arg, &ops);
}


static const struct CMUnitTest repl_tests[] = {
	{"DAOS300: simple update/fetch allowing replication - demo",
		io_replicated_rw_demo, NULL, NULL},
};

int
run_daos_repl_test(int rank, int size, int keys, int wsec)
{
	int rc = 0;

	sleep_seconds = wsec;
	g_dkeys = keys;
	rc = cmocka_run_group_tests_name("DAOS repl tests", repl_tests,
					 obj_setup, obj_teardown);

	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
