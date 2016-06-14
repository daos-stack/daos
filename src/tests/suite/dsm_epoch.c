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
/*
 * Epoch Tests
 */

#include "daos_test.h"

static void
assert_epoch_state_equal(daos_epoch_state_t *a, daos_epoch_state_t *b)
{
	assert_int_equal(a->es_hce, b->es_hce);
	assert_int_equal(a->es_lre, b->es_lre);
	assert_int_equal(a->es_lhe, b->es_lhe);
	assert_int_equal(a->es_glb_hce, b->es_glb_hce);
	assert_int_equal(a->es_glb_lre, b->es_glb_lre);
	assert_int_equal(a->es_glb_hpce, b->es_glb_hpce);
}

static void
epoch_query(void **state)
{
	test_arg_t		*arg = *state;
	daos_epoch_state_t	 epoch_state;
	daos_event_t		 ev;
	daos_event_t		*evp;
	int			 rc;

	if (arg->myrank != 0)
		return;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	print_message("querying epoch state %ssynchronously ...\n",
		      arg->async ? "a" : "");

	rc = dsm_epoch_query(arg->coh, &epoch_state, arg->async ? &ev : NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		assert_int_equal(ev.ev_error, 0);

		rc = daos_event_fini(&ev);
		assert_int_equal(rc, 0);
	}

	assert_epoch_state_equal(&epoch_state, &arg->co_info.ci_epoch_state);
}

static int
do_epoch_hold(test_arg_t *arg, daos_epoch_t *epoch, daos_epoch_state_t *state)
{
	daos_event_t	ev;
	daos_event_t   *evp;
	int		rc;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	print_message("holding epoch "DF_U64" %ssynchronously ...\n", *epoch,
		      arg->async ? "a" : "");

	rc = dsm_epoch_hold(arg->coh, epoch, state, arg->async ? &ev : NULL);

	if (arg->async) {
		assert_int_equal(rc, 0);

		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);

		rc = ev.ev_error;

		assert_int_equal(daos_event_fini(&ev), 0);
	}

	return rc;
}

static int
do_epoch_commit(test_arg_t *arg, daos_epoch_t epoch, daos_epoch_state_t *state)
{
	daos_event_t	ev;
	daos_event_t   *evp;
	int		rc;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	print_message("committing epoch "DF_U64" %ssynchronously ...\n",
		      epoch, arg->async ? "a" : "");

	rc = dsm_epoch_commit(arg->coh, epoch, state, arg->async ? &ev : NULL);

	if (arg->async) {
		assert_int_equal(rc, 0);

		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);

		rc = ev.ev_error;

		assert_int_equal(daos_event_fini(&ev), 0);
	}

	return rc;
}

static void
epoch_hold_commit(void **state)
{
	test_arg_t		*arg = *state;
	daos_epoch_state_t	 epoch_state;
	daos_epoch_t		 epoch;
	daos_epoch_t		 epoch_expected;
	int			 rc;

	if (arg->myrank != 0)
		return;

	assert_int_equal(arg->co_info.ci_epoch_state.es_lhe, DAOS_EPOCH_MAX);

	/* Commit to an unheld epoch. */
	epoch = arg->co_info.ci_epoch_state.es_hce + 22;
	rc = do_epoch_commit(arg, epoch, &epoch_state);
	assert_int_equal(rc, -DER_EP_RO);

	/* Hold that epoch. */
	epoch_expected = epoch;
	rc = do_epoch_hold(arg, &epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch, epoch_state.es_lhe);
	assert_int_equal(epoch, epoch_expected);
	arg->co_info.ci_epoch_state.es_lhe = epoch;
	assert_epoch_state_equal(&epoch_state, &arg->co_info.ci_epoch_state);

	/* Retry the commit to a higher epoch, which is held already. */
	epoch += 22;
	rc = do_epoch_commit(arg, epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch_state.es_hce, epoch);
	assert_int_equal(epoch_state.es_lhe, epoch + 1);
	assert_int_equal(epoch_state.es_glb_hce, epoch);
	assert_int_equal(epoch_state.es_glb_hpce, epoch);
	arg->co_info.ci_epoch_state.es_hce = epoch;
	arg->co_info.ci_epoch_state.es_lhe = epoch + 1;
	arg->co_info.ci_epoch_state.es_glb_hce = epoch;
	arg->co_info.ci_epoch_state.es_glb_hpce = epoch;
	assert_epoch_state_equal(&epoch_state, &arg->co_info.ci_epoch_state);

	/* Hold an epoch <= GHPCE. */
	epoch = arg->co_info.ci_epoch_state.es_hce;
	epoch_expected = arg->co_info.ci_epoch_state.es_glb_hpce + 1;
	rc = do_epoch_hold(arg, &epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch, epoch_state.es_lhe);
	assert_int_equal(epoch, epoch_expected);
	arg->co_info.ci_epoch_state.es_lhe = epoch;
	assert_epoch_state_equal(&epoch_state, &arg->co_info.ci_epoch_state);

	/* Release the hold. */
	epoch = DAOS_EPOCH_MAX;
	rc = do_epoch_hold(arg, &epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch, epoch_state.es_lhe);
	assert_int_equal(epoch, DAOS_EPOCH_MAX);
	arg->co_info.ci_epoch_state.es_lhe = epoch;
	assert_epoch_state_equal(&epoch_state, &arg->co_info.ci_epoch_state);
}

static const struct CMUnitTest epoch_tests[] = {
	{ "DSM300: epoch_query",		/* must be first */
	  epoch_query, async_disable, NULL},
	{ "DSM301: epoch_query (async)",	/* must be second */
	  epoch_query, async_enable, NULL},
	{ "DSM302: epoch_hold_commit",
	  epoch_hold_commit, async_disable, NULL},
	{ "DSM303: epoch_hold_commit (async)",
	  epoch_hold_commit, async_enable, NULL},
};

static int
setup(void **state)
{
	test_arg_t	*arg;
	int		 rc;

	arg = malloc(sizeof(test_arg_t));
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
		rc = dmg_pool_create(0, geteuid(), getegid(), "srv_grp", NULL,
				     "pmem", 256*1024*1024, &arg->svc,
				     arg->pool_uuid, NULL);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	if (arg->myrank == 0) {
		/** connect to pool */
		rc = dsm_pool_connect(arg->pool_uuid, NULL /* grp */, &arg->svc,
				      DAOS_PC_RW, NULL /* failed */, &arg->poh,
				      &arg->pool_info, NULL /* ev */);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;
	MPI_Bcast(&arg->pool_info, sizeof(arg->pool_info), MPI_CHAR, 0,
		  MPI_COMM_WORLD);

	/** l2g and g2l the pool handle */
	handle_share(&arg->poh, HANDLE_POOL, arg->myrank, arg->poh);

	if (arg->myrank == 0) {
		/** create container */
		uuid_generate(arg->co_uuid);
		rc = dsm_co_create(arg->poh, arg->co_uuid, NULL);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	if (arg->myrank == 0) {
		/** open container */
		rc = dsm_co_open(arg->poh, arg->co_uuid, DAOS_COO_RW, NULL,
				 &arg->coh, &arg->co_info, NULL);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** l2g and g2l the container handle */
	handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->poh);

	*state = arg;
	return 0;
}

static int
teardown(void **state) {
	test_arg_t	*arg = *state;
	int		 rc;

	rc = dsm_co_close(arg->coh, NULL);
	if (rc)
		return rc;

	if (arg->myrank == 0)
		rc = dsm_co_destroy(arg->poh, arg->co_uuid, 1, NULL);
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	rc = dsm_pool_disconnect(arg->poh, NULL /* ev */);
	if (rc)
		return rc;

	if (arg->myrank == 0)
		rc = dmg_pool_destroy(arg->pool_uuid, "srv_grp", 1, NULL);
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	rc = daos_eq_destroy(arg->eq, 0);
	if (rc)
		return rc;

	free(arg);
	return 0;
}

int
run_dsm_epoch_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("DSM epoch tests", epoch_tests, setup,
					 teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
