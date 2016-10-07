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
	assert_int_equal(a->es_ghce, b->es_ghce);
	assert_int_equal(a->es_glre, b->es_glre);
	assert_int_equal(a->es_ghpce, b->es_ghpce);
}

static void
epoch_query(void **state)
{
	test_arg_t		*arg = *state;
	daos_epoch_state_t	 epoch_state;
	daos_event_t		 ev;
	daos_event_t		*evp;
	int			 rc;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	print_message("querying epoch state %ssynchronously ...\n",
		      arg->async ? "a" : "");

	rc = daos_epoch_query(arg->coh, &epoch_state, arg->async ? &ev : NULL);
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

	rc = daos_epoch_hold(arg->coh, epoch, state, arg->async ? &ev : NULL);

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

	rc = daos_epoch_commit(arg->coh, epoch, state, arg->async ? &ev : NULL);

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

	assert_int_equal(arg->co_info.ci_epoch_state.es_lhe, DAOS_EPOCH_MAX);

	print_message("SUBTEST 1: commit to an unheld epoch: shall get %d\n",
		      -DER_EP_RO);
	epoch = arg->co_info.ci_epoch_state.es_hce + 22;
	rc = do_epoch_commit(arg, epoch, &epoch_state);
	assert_int_equal(rc, -DER_EP_RO);

	print_message("SUBTEST 2: hold that epoch: shall succeed.\n");
	epoch_expected = epoch;
	rc = do_epoch_hold(arg, &epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch, epoch_state.es_lhe);
	assert_int_equal(epoch, epoch_expected);
	arg->co_info.ci_epoch_state.es_lhe = epoch;
	assert_epoch_state_equal(&epoch_state, &arg->co_info.ci_epoch_state);

	print_message("SUBTEST 3: retry committing to a higher epoch, which is "
		      "held already by subtest 2: shall succeed.\n");
	epoch += 22;
	rc = do_epoch_commit(arg, epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch_state.es_hce, epoch);
	assert_int_equal(epoch_state.es_lhe, epoch + 1);
	assert_int_equal(epoch_state.es_ghce, epoch);
	assert_int_equal(epoch_state.es_ghpce, epoch);
	arg->co_info.ci_epoch_state.es_hce = epoch;
	arg->co_info.ci_epoch_state.es_lhe = epoch + 1;
	arg->co_info.ci_epoch_state.es_ghce = epoch;
	arg->co_info.ci_epoch_state.es_ghpce = epoch;
	assert_epoch_state_equal(&epoch_state, &arg->co_info.ci_epoch_state);

	print_message("SUBTEST 4: hold an epoch <= GHPCE: shall succeed and "
		      "end up holding GHPCE + 1.\n");
	epoch = arg->co_info.ci_epoch_state.es_hce;
	epoch_expected = arg->co_info.ci_epoch_state.es_ghpce + 1;
	rc = do_epoch_hold(arg, &epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch, epoch_state.es_lhe);
	assert_int_equal(epoch, epoch_expected);
	arg->co_info.ci_epoch_state.es_lhe = epoch;
	assert_epoch_state_equal(&epoch_state, &arg->co_info.ci_epoch_state);

	print_message("SUBTEST 5: release the hold: shall succeed.\n");
	epoch = DAOS_EPOCH_MAX;
	rc = do_epoch_hold(arg, &epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch, epoch_state.es_lhe);
	assert_int_equal(epoch, DAOS_EPOCH_MAX);
	arg->co_info.ci_epoch_state.es_lhe = epoch;
	assert_epoch_state_equal(&epoch_state, &arg->co_info.ci_epoch_state);

	print_message("SUBTEST 6: close and open the container again: shall "
		      "succeed and report correct GLRE.\n");
	rc = daos_cont_close(arg->coh, NULL);
	assert_int_equal(rc, 0);
	rc = daos_cont_open(arg->poh, arg->co_uuid, DAOS_COO_RW, &arg->coh,
			    &arg->co_info, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(arg->co_info.ci_epoch_state.es_hce,
			 epoch_state.es_hce);
	assert_int_equal(arg->co_info.ci_epoch_state.es_lre,
			 epoch_state.es_hce);
	assert_int_equal(arg->co_info.ci_epoch_state.es_lhe, DAOS_EPOCH_MAX);
	assert_int_equal(arg->co_info.ci_epoch_state.es_ghce,
			 epoch_state.es_ghce);
	assert_int_equal(arg->co_info.ci_epoch_state.es_glre,
			 arg->co_info.ci_epoch_state.es_lre);
	assert_int_equal(arg->co_info.ci_epoch_state.es_ghpce,
			 arg->co_info.ci_epoch_state.es_hce);
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

	/** create pool with minimal size */
	rc = dmg_pool_create(0731, geteuid(), getegid(), "srv_grp", NULL,
			     "pmem", 256*1024*1024, &arg->svc,
			     arg->pool_uuid, NULL);
	if (rc)
		return rc;

	/** connect to pool */
	rc = daos_pool_connect(arg->pool_uuid, NULL /* grp */, &arg->svc,
			       DAOS_PC_RW, &arg->poh, &arg->pool_info,
			       NULL /* ev */);
	if (rc)
		return rc;

	/** create container */
	uuid_generate(arg->co_uuid);
	rc = daos_cont_create(arg->poh, arg->co_uuid, NULL);
	if (rc)
		return rc;

	/** open container */
	rc = daos_cont_open(arg->poh, arg->co_uuid, DAOS_COO_RW, &arg->coh,
			    &arg->co_info, NULL);
	if (rc)
		return rc;

	*state = arg;
	return 0;
}

static int
teardown(void **state) {
	test_arg_t	*arg = *state;
	int		 rc;

	rc = daos_cont_close(arg->coh, NULL);
	if (rc)
		return rc;

	rc = daos_cont_destroy(arg->poh, arg->co_uuid, 1, NULL);
	if (rc)
		return rc;

	rc = daos_pool_disconnect(arg->poh, NULL /* ev */);
	if (rc)
		return rc;

	rc = dmg_pool_destroy(arg->pool_uuid, "srv_grp", 1, NULL);
	if (rc)
		return rc;

	rc = daos_eq_destroy(arg->eq, 0);
	if (rc)
		return rc;

	free(arg);
	return 0;
}

int
run_daos_epoch_test(int rank, int size)
{
	int	rc;

	if (rank == 0)
		rc = cmocka_run_group_tests_name("DSM epoch tests", epoch_tests,
						 setup, teardown);
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	return rc;
}
