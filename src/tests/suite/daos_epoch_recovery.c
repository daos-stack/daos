/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
 * tests/suite/daos_epoch_recovery.c
 */
#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"
#include "daos_iotest.h"

enum io_op {
	UPDATE,
	VERIFY
};

static void
io(enum io_op op, test_arg_t *arg, daos_handle_t coh, daos_epoch_t epoch,
   daos_obj_id_t oid, const char *value)
{
	struct ioreq	req;
	const char	dkey[] = "epoch_recovery dkey";
	const int	nakeys = 4;
	const size_t	nakeys_strlen = 4 /* "9999" */;
	const char     *akey_fmt = "epoch_recovery akey%d";
	char	       *akey[nakeys];
	char	       *rec[nakeys];
	daos_size_t	rec_size[nakeys];
	daos_off_t	offset[nakeys];
	const char     *val_fmt = "epoch_recovery val%d '%s'";
	daos_size_t	rsize;
	int		i;

	ioreq_init(&req, coh, oid, DAOS_IOD_SINGLE, arg);

	rsize = strlen(val_fmt) + nakeys_strlen + strlen(value) + 1;

	for (i = 0; i < nakeys; i++) {
		akey[i] = malloc(strlen(akey_fmt) + nakeys_strlen + 1);
		assert_non_null(akey[i]);
		sprintf(akey[i], akey_fmt, i);
		rec_size[i] = rsize;
		rec[i] = calloc(rec_size[i], 1);
		assert_non_null(rec[i]);
		offset[i] = i * 20;
	}

	if (op == UPDATE) {
		print_message("preparing records\n");
		for (i = 0; i < nakeys; i++) {
			snprintf(rec[i], rec_size[i], val_fmt, i, value);
			print_message("  akey[%d] '%s' val '%d %s'\n", i,
				      akey[i], (int)rec_size[i], rec[i]);
		}

		print_message("writing records to epoch "DF_U64"\n", epoch);
		insert(dkey, nakeys, (const char **)akey, offset, (void **)rec,
		       rec_size, &epoch, &req);
	} else {	/* op == VERIFY */
		char *rec_verify;

		rec_verify = calloc(rsize, 1);
		assert_non_null(rec_verify);
		print_message("verifying epoch "DF_U64"\n", epoch);
		lookup(dkey, nakeys, (const char **)akey, offset, rec_size,
		       (void **)rec, rec_size, &epoch, &req, false);
		for (i = 0; i < nakeys; i++) {
			print_message("  akey[%d] '%s' val '%d %s'\n", i,
				      akey[i], (int)req.iod[i].iod_size,
				      rec[i]);
			snprintf(rec_verify, rsize, val_fmt, i, value);
			assert_int_equal(req.iod[i].iod_size,
					 rsize);
			assert_memory_equal(rec[i], rec_verify,
					    req.iod[i].iod_size);
		}
		free(rec_verify);
	}
}

enum epoch_recovery_op {
	CONT_CLOSE,
	POOL_DISCONNECT,
	POOL_EVICT
};

static void
epoch_recovery(test_arg_t *arg, enum epoch_recovery_op op)
{
	uuid_t		uuid;
	daos_handle_t	coh;
	daos_epoch_t	epoch;
	daos_obj_id_t	oid;
	int		rc;

	MPI_Barrier(MPI_COMM_WORLD);

	uuid_generate(uuid);
	print_message("creating and opening container "DF_UUIDF"\n",
		      DP_UUID(uuid));
	rc = daos_cont_create(arg->pool.poh, uuid, NULL /* ev */);
	assert_int_equal(rc, 0);
	rc = daos_cont_open(arg->pool.poh, uuid, DAOS_COO_RW, &coh,
			    NULL /* info */, NULL /* ev */);
	assert_int_equal(rc, 0);

	epoch = 1;
	rc = daos_epoch_hold(coh, &epoch, NULL /* state */,
			     NULL /* ev */);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch, 1);

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, 0, arg->myrank);

	/* Every rank updates epoch 1 and commit. */
	io(UPDATE, arg, coh, 1 /* epoch */, oid, "epoch 1");
	rc = daos_epoch_commit(coh, 1 /* epoch */, NULL /* state */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);

	/* Every rank updates epoch 2 but do not commit. */
	io(UPDATE, arg, coh, 2 /* epoch */, oid, "epoch 2");

	MPI_Barrier(MPI_COMM_WORLD);

	if (op == POOL_EVICT) {
		if (arg->myrank == 0) {
			print_message("evicting pool connections\n");
			rc = daos_pool_evict(arg->pool.pool_uuid, arg->group,
					     &arg->pool.svc, NULL /* ev */);
			assert_int_equal(rc, 0);
		}
		MPI_Barrier(MPI_COMM_WORLD);
	}
	print_message("closing container\n");
	rc = daos_cont_close(coh, NULL /* ev */);
	assert_int_equal(rc, 0);
	MPI_Barrier(MPI_COMM_WORLD);
	if (op == POOL_DISCONNECT || op == POOL_EVICT) {
		print_message("disconnecting from pool\n");
		rc = daos_pool_disconnect(arg->pool.poh, NULL /* ev */);
		assert_int_equal(rc, 0);
		print_message("reconnecting to pool\n");
		if (arg->myrank == 0) {
			rc = daos_pool_connect(arg->pool.pool_uuid, arg->group,
					       &arg->pool.svc, DAOS_PC_RW,
					       &arg->pool.poh, NULL /* info */,
					       NULL /* ev */);
			assert_int_equal(rc, 0);
		}
		handle_share(&arg->pool.poh, HANDLE_POOL, arg->myrank,
			     arg->pool.poh, 1);
	}
	print_message("reopening container\n");
	rc = daos_cont_open(arg->pool.poh, uuid, DAOS_COO_RO, &coh,
			    &arg->co_info, NULL /* ev */);
	assert_int_equal(rc, 0);
	assert_int_equal(arg->co_info.ci_epoch_state.es_hce, 1);

	/* Every rank verifies that epoch 2 is discarded and epoch 1 remains. */
	io(VERIFY, arg, coh, 2 /* epoch */, oid, "epoch 1");

	rc = daos_cont_close(coh, NULL /* ev */);
	assert_int_equal(rc, 0);
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
cont_close_discard(void **state)
{
	epoch_recovery(*state, CONT_CLOSE);
}

static void
pool_disconnect_discard(void **state)
{
	epoch_recovery(*state, POOL_DISCONNECT);
}

static void
pool_evict_discard(void **state)
{
	epoch_recovery(*state, POOL_EVICT);
}

static const struct CMUnitTest epoch_recovery_tests[] = {
	{"ERECOV1: container close discards uncommitted data",
	 cont_close_discard, NULL, test_case_teardown},
	{"ERECOV2: pool disconnect discards uncommitted data",
	 pool_disconnect_discard, NULL, test_case_teardown},
	{"ERECOV3: pool evict discards uncommitted data",
	 pool_evict_discard, NULL, test_case_teardown}
};

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CONNECT, true, DEFAULT_POOL_SIZE,
			  NULL);
}

int
run_daos_epoch_recovery_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("Epoch recovery tests",
					 epoch_recovery_tests, setup,
					 test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
