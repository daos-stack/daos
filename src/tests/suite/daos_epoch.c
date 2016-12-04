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
do_epoch_hold(test_arg_t *arg, daos_handle_t coh, daos_epoch_t *epoch,
	      daos_epoch_state_t *state)
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

	rc = daos_epoch_hold(coh, epoch, state, arg->async ? &ev : NULL);

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
do_epoch_slip(test_arg_t *arg, daos_handle_t coh, daos_epoch_t epoch,
	      daos_epoch_state_t *state)
{
	daos_event_t	ev;
	daos_event_t   *evp;
	int		rc;

	if (arg->async) {
		rc = daos_event_init(&ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	print_message("sliping to epoch "DF_U64" %ssynchronously ...\n", epoch,
		      arg->async ? "a" : "");

	rc = daos_epoch_slip(coh, epoch, state, arg->async ? &ev : NULL);

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
do_epoch_commit(test_arg_t *arg, daos_handle_t coh, daos_epoch_t epoch,
		daos_epoch_state_t *state)
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

	rc = daos_epoch_commit(coh, epoch, state, arg->async ? &ev : NULL);

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
#if 0
	daos_handle_t		 second_coh;
	daos_epoch_t		 second_epoch;
#endif
	int			 rc;

	assert_int_equal(arg->co_info.ci_epoch_state.es_lhe, DAOS_EPOCH_MAX);

	print_message("SUBTEST 1: commit to an unheld epoch: shall get %d\n",
		      -DER_EP_RO);
	epoch = arg->co_info.ci_epoch_state.es_hce + 22;
	rc = do_epoch_commit(arg, arg->coh, epoch, &epoch_state);
	assert_int_equal(rc, -DER_EP_RO);

	print_message("SUBTEST 2: hold that epoch: shall succeed.\n");
	epoch_expected = epoch;
	rc = do_epoch_hold(arg, arg->coh, &epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch, epoch_state.es_lhe);
	assert_int_equal(epoch, epoch_expected);
	arg->co_info.ci_epoch_state.es_lhe = epoch;
	assert_epoch_state_equal(&epoch_state, &arg->co_info.ci_epoch_state);

	print_message("SUBTEST 3: retry committing to a higher epoch, which is "
		      "held already by subtest 2: shall succeed.\n");
	epoch += 22;
	rc = do_epoch_commit(arg, arg->coh, epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch_state.es_hce, epoch);
	assert_int_equal(epoch_state.es_lre, epoch);
	assert_int_equal(epoch_state.es_lhe, epoch + 1);
	assert_int_equal(epoch_state.es_ghce, epoch);
	assert_int_equal(epoch_state.es_glre, epoch);
	assert_int_equal(epoch_state.es_ghpce, epoch);
	arg->co_info.ci_epoch_state = epoch_state;

#if 0 /* Disable until multiple handles are supported. */
	print_message("SUBTEST 4: hold an epoch <= GHPCE: shall succeed and "
		      "end up holding GHPCE + 1.\n");
	/*
	 * Open a second handle and commit to an epoch higher than the previous
	 * handle's HCE.
	 */
	second_epoch = arg->co_info.ci_epoch_state.es_hce + 1000;
	rc = daos_cont_open(arg->poh, arg->co_uuid, DAOS_COO_RW, &second_coh,
			    NULL /* info */, NULL /* ev */);
	assert_int_equal(rc, 0);
	rc = daos_epoch_hold(second_coh, &second_epoch, NULL /* state */,
			     NULL /* ev */);
	assert_int_equal(rc, 0);
	assert_int_equal(second_epoch,
			 arg->co_info.ci_epoch_state.es_hce + 1000);
	rc = daos_epoch_commit(second_coh, second_epoch, NULL /* state */,
			       NULL /* ev */);
	assert_int_equal(rc, 0);
	/* Attempt to hold an epoch <= GHPCE with the original handle. */
	epoch = arg->co_info.ci_epoch_state.es_hce + 1;
	epoch_expected = second_epoch + 1;
	rc = do_epoch_hold(arg, arg->coh, &epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch, epoch_state.es_lhe);
	assert_int_equal(epoch, epoch_expected);
	arg->co_info.ci_epoch_state.es_lhe = epoch;
	assert_epoch_state_equal(&epoch_state, &arg->co_info.ci_epoch_state);
	rc = daos_cont_close(second_coh, NULL /* ev */);
	assert_int_equal(rc, 0);
#endif

	print_message("SUBTEST 5: release the hold: shall succeed.\n");
	epoch = DAOS_EPOCH_MAX;
	rc = do_epoch_hold(arg, arg->coh, &epoch, &epoch_state);
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

static void
epoch_slip(void **argp)
{
	test_arg_t		*arg = *argp;
	uuid_t			 uuid;
	daos_handle_t		 coh;
	daos_cont_info_t	 info;
	daos_epoch_state_t	 epoch_state;
	daos_epoch_t		 epoch;
	int			 rc;

	/*
	 * Without DAOS_COO_NOSLIP, very little can we test about
	 * daos_epoch_slip().
	 */
	uuid_generate(uuid);
	print_message("creating container "DF_UUIDF" and opening with "
		      "DAOS_COO_NOSLIP\n", DP_UUID(uuid));
	rc = daos_cont_create(arg->poh, uuid, NULL);
	assert_int_equal(rc, 0);
	rc = daos_cont_open(arg->poh, uuid, DAOS_COO_RW | DAOS_COO_NOSLIP,
			    &coh, &info, NULL);
	assert_int_equal(rc, 0);

	epoch = 10;
	print_message("holding and committing to epoch "DF_U64"\n", epoch);
	rc = do_epoch_hold(arg, coh, &epoch, &info.ci_epoch_state);
	assert_int_equal(rc, 0);
	rc = do_epoch_commit(arg, coh, epoch, &info.ci_epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(info.ci_epoch_state.es_hce, epoch);
	assert_int_equal(info.ci_epoch_state.es_lre, 0);
	assert_int_equal(info.ci_epoch_state.es_glre, 0);

	print_message("SUBTEST 1: slip to 0 shall be no-op\n");
	epoch = 0;
	rc = do_epoch_slip(arg, coh, epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_epoch_state_equal(&epoch_state, &info.ci_epoch_state);

	print_message("SUBTEST 2: slip to (LRE, HCE)\n");
	epoch = info.ci_epoch_state.es_lre + 1;
	assert_true(epoch < info.ci_epoch_state.es_hce);
	rc = do_epoch_slip(arg, coh, epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch_state.es_lre, epoch);
	assert_int_equal(epoch_state.es_glre, epoch);
	info.ci_epoch_state.es_lre = epoch_state.es_lre;
	info.ci_epoch_state.es_glre = epoch_state.es_glre;
	assert_epoch_state_equal(&epoch_state, &info.ci_epoch_state);

	print_message("SUBTEST 1: slip to DAOS_EPOCH_MAX - 1 shall get HCE\n");
	epoch = DAOS_EPOCH_MAX - 1;
	assert_true(epoch > info.ci_epoch_state.es_hce);
	rc = do_epoch_slip(arg, coh, epoch, &epoch_state);
	assert_int_equal(rc, 0);
	assert_int_equal(epoch_state.es_lre, info.ci_epoch_state.es_hce);
	assert_int_equal(epoch_state.es_glre, info.ci_epoch_state.es_hce);
	info.ci_epoch_state.es_lre = epoch_state.es_lre;
	info.ci_epoch_state.es_glre = epoch_state.es_glre;
	assert_epoch_state_equal(&epoch_state, &info.ci_epoch_state);

	print_message("closing and destroying container "DF_UUIDF"\n",
		      DP_UUID(uuid));
	rc = daos_cont_close(coh, NULL);
	assert_int_equal(rc, 0);
	rc = daos_cont_destroy(arg->poh, uuid, 1, NULL);
	assert_int_equal(rc, 0);
}

static const struct CMUnitTest epoch_tests[] = {
	{ "EPOCH1: epoch_query",		/* must be first */
	  epoch_query, async_disable, NULL},
	{ "EPOCH2: epoch_query (async)",	/* must be second */
	  epoch_query, async_enable, NULL},
	{ "EPOCH3: epoch_hold_commit",
	  epoch_hold_commit, async_disable, NULL},
	{ "EPOCH4: epoch_hold_commit (async)",
	  epoch_hold_commit, async_enable, NULL},
	{ "EPOCH5: epoch_slip",
	  epoch_slip, async_disable, NULL},
	{ "EPOCH6: epoch_slip (async)",
	  epoch_slip, async_enable, NULL}
};

static int
setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, false);
}

int
run_daos_epoch_test(int rank, int size)
{
	int	rc;

	if (rank == 0)
		rc = cmocka_run_group_tests_name("DAOS epoch tests",
						 epoch_tests, setup,
						 test_teardown);
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	return rc;
}
