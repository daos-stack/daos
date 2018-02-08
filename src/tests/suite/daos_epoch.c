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

#define DDSUBSYS	DDFAC(tests)

#include "daos_test.h"
#include "daos_iotest.h"

#define MUST(rc) assert_int_equal(rc, 0)

static void
io_for_aggregation(test_arg_t *arg, daos_handle_t coh,
		   daos_epoch_t	start_epoch, int gs_dkeys,
		   daos_epoch_state_t *state, daos_obj_id_t oid,
		   bool update, bool verify_empty)
{
	struct ioreq		req;
	int			i, g_dkeys_strlen = 6;
	const char		akey[] = "slip akey";
	char			dkey[] = "slip dkey";
	char			*rec, *val, *rec_verify;
	const char		*val_fmt = "slip val%d";
	daos_size_t		val_size, rec_size;
	daos_epoch_t		epoch;

	if (verify_empty)
		print_message("Check empty records (epr: "DF_U64","DF_U64")\n",
			      start_epoch, start_epoch + gs_dkeys - 1);
	else
		print_message("Check valid records (epr: "DF_U64","DF_U64")\n",
			      start_epoch, start_epoch + gs_dkeys - 1);

	ioreq_init(&req, coh, oid, DAOS_IOD_SINGLE, arg);
	if (update && !arg->myrank)
		print_message("Inserting %d keys...\n",
			      gs_dkeys);

	D__ALLOC(rec, strlen(val_fmt) + g_dkeys_strlen + 1);
	assert_non_null(rec);
	D__ALLOC(val, 64);
	assert_non_null(val);
	val_size = 64;
	epoch = start_epoch;

	if (update) {
		for (i = 0; i < gs_dkeys; i++) {
			memset(rec, 0, (strlen(val_fmt) + g_dkeys_strlen + 1));
			sprintf(rec, val_fmt, epoch + i);
			rec_size = strlen(rec);
			D__DEBUG(DF_MISC, "  d-key[%d] '%s' val '%.*s'\n", i,
				dkey, (int)rec_size, rec);
			insert_single(dkey, akey, 1100, rec, rec_size,
				      (epoch + i), &req);
		}
	}

	D__ALLOC(rec_verify, strlen(val_fmt) + g_dkeys_strlen + 1);
	for (i = 0; i < gs_dkeys; i++) {
		memset(rec_verify, 0, (strlen(val_fmt) + g_dkeys_strlen + 1));
		memset(val, 0, 64);
		if (!verify_empty) {
			sprintf(rec_verify, val_fmt, (epoch + i));
			lookup_single(dkey, akey, 1100, val, val_size,
				      (epoch + i), &req);
		} else {
			lookup_empty_single(dkey, akey, 1100, val, val_size,
					    (epoch + i), &req);
		}
		assert_int_equal(req.iod[0].iod_size, strlen(rec_verify));
		assert_memory_equal(val, rec_verify, req.iod[0].iod_size);
	}

	D__FREE(val, 64);
	D__FREE(rec_verify, (strlen(val_fmt) + g_dkeys_strlen + 1));
	D__FREE(rec, strlen(val_fmt) + g_dkeys_strlen + 1);
}

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

static int
cont_create(test_arg_t *arg, uuid_t uuid)
{
	uuid_generate(uuid);
	print_message("creating container "DF_UUIDF"\n", DP_UUID(uuid));
	return daos_cont_create(arg->poh, uuid, NULL);
}

static int
cont_destroy(test_arg_t *arg, const uuid_t uuid)
{
	print_message("destroying container "DF_UUID"\n", DP_UUID(uuid));
	return daos_cont_destroy(arg->poh, uuid, 1, NULL);
}

static int
cont_open(test_arg_t *arg, const uuid_t uuid, unsigned int flags,
	  daos_handle_t *coh)
{
	print_message("opening container "DF_UUIDF" (flags=%X)\n",
		      DP_UUID(uuid), flags);
	return daos_cont_open(arg->poh, uuid, flags, coh, &arg->co_info, NULL);
}

static int
cont_close(test_arg_t *arg, daos_handle_t coh)
{
	print_message("closing container\n");
	return daos_cont_close(coh, NULL);
}

static int
cont_query(test_arg_t *arg, daos_handle_t coh, daos_cont_info_t *info)
{
	print_message(".");
	return daos_cont_query(coh, info, NULL);
}

static int
epoch_query(test_arg_t *arg, daos_handle_t coh, daos_epoch_state_t *state)
{
	daos_event_t	ev;
	daos_event_t   *evp;
	int		rc;

	print_message("querying epoch state %ssynchronously ...\n",
		      arg->async ? "a" : "");
	if (arg->async)
		MUST(daos_event_init(&ev, arg->eq, NULL));
	rc = daos_epoch_query(coh, state, arg->async ? &ev : NULL);
	if (arg->async) {
		assert_int_equal(rc, 0);
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		rc = ev.ev_error;
		MUST(daos_event_fini(&ev));
	}
	return rc;
}

static int
epoch_hold(test_arg_t *arg, daos_handle_t coh, daos_epoch_t *epoch,
	   daos_epoch_state_t *state)
{
	daos_event_t	ev;
	daos_event_t   *evp;
	int		rc;

	print_message("holding epoch "DF_U64" %ssynchronously ...\n", *epoch,
		      arg->async ? "a" : "");
	if (arg->async)
		MUST(daos_event_init(&ev, arg->eq, NULL));
	rc = daos_epoch_hold(coh, epoch, state, arg->async ? &ev : NULL);
	if (arg->async) {
		assert_int_equal(rc, 0);
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		rc = ev.ev_error;
		MUST(daos_event_fini(&ev));
	}
	return rc;
}

static int
epoch_slip(test_arg_t *arg, daos_handle_t coh, daos_epoch_t epoch,
	   daos_epoch_state_t *state)
{
	daos_event_t	ev;
	daos_event_t   *evp;
	int		rc;

	print_message("sliping to epoch "DF_U64" %ssynchronously ...\n", epoch,
		      arg->async ? "a" : "");
	if (arg->async)
		MUST(daos_event_init(&ev, arg->eq, NULL));
	rc = daos_epoch_slip(coh, epoch, state, arg->async ? &ev : NULL);
	if (arg->async) {
		assert_int_equal(rc, 0);
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		rc = ev.ev_error;
		MUST(daos_event_fini(&ev));
	}
	return rc;
}

static int
epoch_commit(test_arg_t *arg, daos_handle_t coh, daos_epoch_t epoch,
	     daos_epoch_state_t *state)
{
	daos_event_t	ev;
	daos_event_t   *evp;
	int		rc;

	print_message("committing epoch "DF_U64" %ssynchronously ...\n",
		      epoch, arg->async ? "a" : "");
	if (arg->async)
		MUST(daos_event_init(&ev, arg->eq, NULL));
	rc = daos_epoch_commit(coh, epoch, state, arg->async ? &ev : NULL);
	if (arg->async) {
		assert_int_equal(rc, 0);
		rc = daos_eq_poll(arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &ev);
		rc = ev.ev_error;
		MUST(daos_event_fini(&ev));
	}
	return rc;
}

static void
test_epoch_init(void **varg)
{
	test_arg_t	       *arg = *varg;
	uuid_t			cont_uuid;
	daos_handle_t		coh;
	daos_epoch_state_t     *state = &arg->co_info.ci_epoch_state;

	MUST(cont_create(arg, cont_uuid));
	MUST(cont_open(arg, cont_uuid, DAOS_COO_RW, &coh));
	assert_int_equal(state->es_hce, state->es_ghce);
	assert_int_equal(state->es_lre, state->es_ghce);
	assert_int_equal(state->es_lhe, DAOS_EPOCH_MAX);
	assert_int_equal(state->es_ghce, 0);
	assert_int_equal(state->es_glre, 0);
	assert_int_equal(state->es_ghpce, 0);
	MUST(cont_close(arg, coh));
	MUST(cont_destroy(arg, cont_uuid));
}

static void
test_epoch_query(void **varg)
{
	test_arg_t	       *arg = *varg;
	uuid_t			cont_uuid;
	daos_handle_t		coh;
	daos_epoch_state_t	state;

	MUST(cont_create(arg, cont_uuid));
	MUST(cont_open(arg, cont_uuid, DAOS_COO_RW, &coh));
	MUST(epoch_query(arg, coh, &state));
	assert_epoch_state_equal(&state, &arg->co_info.ci_epoch_state);
	MUST(cont_close(arg, coh));
	MUST(cont_destroy(arg, cont_uuid));
}

static void
test_epoch_hold(void **varg)
{
	test_arg_t	       *arg = *varg;
	uuid_t			cont_uuid;
	daos_handle_t		coh;
	daos_epoch_t		epoch;
	daos_epoch_state_t	state;
	daos_epoch_state_t	state_tmp;

	MUST(cont_create(arg, cont_uuid));
	MUST(cont_open(arg, cont_uuid, DAOS_COO_RW, &coh));

	print_message("simple hold shall succeed\n");
	epoch = 10;
	MUST(epoch_hold(arg, coh, &epoch, &state));
	assert_int_equal(epoch, 10);
	/* Only this shall change. */
	arg->co_info.ci_epoch_state.es_lhe = epoch;
	assert_epoch_state_equal(&state, &arg->co_info.ci_epoch_state);

	print_message("release some epochs shall succeed\n");
	epoch = 20;
	state_tmp = state;
	MUST(epoch_hold(arg, coh, &epoch, &state));
	assert_int_equal(epoch, 20);
	/* Only this shall change. */
	state_tmp.es_lhe = epoch;
	assert_epoch_state_equal(&state, &state_tmp);

	print_message("simple release shall succeed\n");
	epoch = DAOS_EPOCH_MAX;
	state_tmp = state;
	MUST(epoch_hold(arg, coh, &epoch, &state));
	assert_int_equal(epoch, DAOS_EPOCH_MAX);
	/* Only this shall change. */
	state_tmp.es_lhe = epoch;
	assert_epoch_state_equal(&state, &state_tmp);

	MUST(cont_close(arg, coh));
	MUST(cont_destroy(arg, cont_uuid));
}

static void
test_epoch_commit(void **varg)
{
	test_arg_t	       *arg = *varg;
	uuid_t			cont_uuid;
	daos_handle_t		coh;
	daos_epoch_t		epoch;
	daos_epoch_state_t	state;
	daos_epoch_state_t	state_tmp;
	int			rc;

	MUST(cont_create(arg, cont_uuid));
	MUST(cont_open(arg, cont_uuid, DAOS_COO_RW, &coh));

	epoch = 10;
	MUST(epoch_hold(arg, coh, &epoch, &state_tmp));

	print_message("committing an unheld epoch shall get %d\n", -DER_EP_RO);
	epoch = 9;
	rc = epoch_commit(arg, coh, epoch, &state);
	assert_int_equal(rc, -DER_EP_RO);
	/* The epoch state shall stay the same. */
	MUST(epoch_query(arg, coh, &state));
	assert_epoch_state_equal(&state, &state_tmp);

	print_message("committing a held epoch shall succeed\n");
	epoch = 10;
	MUST(epoch_commit(arg, coh, epoch, &state));
	assert_int_equal(state.es_hce, epoch);
	assert_int_equal(state.es_lre, epoch);
	assert_int_equal(state.es_lhe, epoch + 1);
	assert_int_equal(state.es_ghce, epoch);
	assert_int_equal(state.es_glre, epoch);
	assert_int_equal(state.es_ghpce, epoch);

	print_message("committing an already committed epoch shall succeed\n");
	epoch = 8;
	state_tmp = state;
	MUST(epoch_commit(arg, coh, epoch, &state));
	/* The epoch state shall stay the same. */
	assert_epoch_state_equal(&state, &state_tmp);

	MUST(cont_close(arg, coh));
	MUST(cont_destroy(arg, cont_uuid));
}

static void
test_epoch_slip(void **argp)
{
	test_arg_t	       *arg = *argp;
	uuid_t			cont_uuid;
	daos_handle_t		coh;
	daos_epoch_t		epoch, epoch_tmp;
	daos_epoch_state_t	state;
	daos_epoch_state_t	state_tmp;
	daos_cont_info_t	info;
	daos_obj_id_t		oid;

	MUST(cont_create(arg, cont_uuid));
	MUST(cont_open(arg, cont_uuid, DAOS_COO_RW | DAOS_COO_NOSLIP,
		       &coh));

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	print_message("OID: "DF_OID"\n", DP_OID(oid));

	epoch = 10;
	MUST(epoch_hold(arg, coh, &epoch, &state));
	io_for_aggregation(arg, coh, epoch, 100, &state, oid,
			   /* update */ true,
			   /* verify empty record */ false);
	MUST(epoch_commit(arg, coh, (epoch + 99),
			  &state));

	memset(&info, 0, sizeof(daos_cont_info_t));
	print_message("Verfying aggregated epoch before slip.. ");
	MUST(cont_query(arg, coh, &info));
	/** aggregated epoch before aggregation */
	assert_true(info.ci_min_slipped_epoch == 0);
	print_message(" "DF_U64"\n", info.ci_min_slipped_epoch);

	print_message("slip to (LRE, MAX) shall succeed\n");
	epoch = 95; /* LRE = 0 and GHCE = 109 */
	print_message("LRE: "DF_U64", HCE: "DF_U64", epoch: "DF_U64"\n",
		      state.es_lre, state.es_hce, epoch);
	assert_true(state.es_lre < epoch && epoch < state.es_hce);
	state_tmp = state;
	MUST(epoch_slip(arg, coh, epoch, &state));
	state_tmp.es_lre = epoch;
	state_tmp.es_glre = epoch;
	assert_epoch_state_equal(&state, &state_tmp);
	memset(&info, 0, sizeof(daos_cont_info_t));

	if (arg->overlap) {
		epoch_tmp = epoch + 15;
		MUST(epoch_hold(arg, coh, &epoch_tmp, &state));
		io_for_aggregation(arg, coh, epoch_tmp, 15, &state, oid,
				   /* update */true,
				   /* verify empty record */false);
		MUST(epoch_commit(arg, coh, epoch_tmp, &state));
		print_message("LRE: "DF_U64", HCE: "DF_U64", epoch: "DF_U64"\n",
			      state.es_lre, state.es_hce, epoch_tmp);

	}

	/** Wait for aggregation completion */
	print_message("Waiting for epoch_slip to complete .");
	while (info.ci_min_slipped_epoch < epoch)
		MUST(cont_query(arg, coh, &info));
	print_message(". Done!\n");

	/* completed aggregation */
	print_message("aggregated epoch: " DF_U64"\n",
		      info.ci_min_slipped_epoch);

	print_message("slip to [0, LRE] shall be no-op\n");
	epoch = 15; /* LRE = 95 */;

	state_tmp = state;
	MUST(epoch_slip(arg, coh, epoch, &state));
	assert_epoch_state_equal(&state, &state_tmp);

	memset(&info, 0, sizeof(daos_cont_info_t));
	/** Wait for aggregation completion */
	print_message("Waiting for epoch_slip to complete .");
	while (info.ci_min_slipped_epoch < epoch)
		MUST(cont_query(arg, coh, &info));
	print_message(". Done!\n");

	/* completed aggregation */
	print_message("aggregated epoch: " DF_U64"\n",
		      info.ci_min_slipped_epoch);

	/** empty records from 10 -> 94 */
	epoch = 10;
	io_for_aggregation(arg, coh, epoch, 85, &state, oid,
			   /* update */ false,
			   /* verify empty record */ true);

	/** no-empty records from 95 - 109/124 */
	epoch = 95;
	io_for_aggregation(arg, coh, epoch,
			   (arg->overlap) ? 30 : 15,
			   &state, oid,
			   /* update */false,
			   /* verify empty record */false);

	MUST(cont_close(arg, coh));
	MUST(cont_destroy(arg, cont_uuid));
}

static void
test_epoch_commit_complex(void **varg)
{
	test_arg_t	       *arg = *varg;
	uuid_t			cont_uuid;
	daos_handle_t		coh_1;
	daos_handle_t		coh_2;
	daos_epoch_t		epoch_1;
	daos_epoch_t		epoch_2;
	daos_epoch_state_t	state_1;
	daos_epoch_state_t	state_2;
	daos_epoch_state_t	state_tmp;

	MUST(cont_create(arg, cont_uuid));
	MUST(cont_open(arg, cont_uuid, DAOS_COO_RW, &coh_1));
	MUST(cont_open(arg, cont_uuid, DAOS_COO_RW, &coh_2));

	epoch_2 = 20;
	MUST(epoch_hold(arg, coh_2, &epoch_2, &state_2));

	print_message("GHCE shall increase below coh_2's LHE\n");
	epoch_1 = 10; /* state_2.es_lhe = 20 */
	MUST(epoch_hold(arg, coh_1, &epoch_1, &state_1));
	MUST(epoch_commit(arg, coh_1, epoch_1, &state_1));
	assert_int_equal(state_1.es_hce, epoch_1);
	assert_int_equal(state_1.es_lre, epoch_1);
	assert_int_equal(state_1.es_lhe, epoch_1 + 1);
	assert_int_equal(state_1.es_ghce, state_1.es_hce);
	assert_int_equal(state_1.es_glre, state_2.es_lre);
	assert_int_equal(state_1.es_ghpce, state_1.es_hce);

	print_message("GHCE shall be blocked by coh_2's LHE\n");
	epoch_1 = 30; /* state_2.es_lhe = 20 */
	MUST(epoch_commit(arg, coh_1, epoch_1, &state_1));
	assert_int_equal(state_1.es_hce, epoch_1);
	assert_int_equal(state_1.es_lre, epoch_1);
	assert_int_equal(state_1.es_lhe, epoch_1 + 1);
	assert_int_equal(state_1.es_ghce, state_2.es_lhe - 1);
	assert_int_equal(state_1.es_glre, state_2.es_lre);
	assert_int_equal(state_1.es_ghpce, state_1.es_hce);

	print_message("GHCE shall catch up once coh_2 releases some of its "
		      "held epochs\n");
	epoch_2 = 25; /* state_2.es_lhe = 20 and state_1.es_hce = 30 */
	MUST(epoch_commit(arg, coh_2, epoch_2, &state_2));
	assert_int_equal(state_2.es_hce, epoch_2);
	assert_int_equal(state_2.es_lre, epoch_2);
	assert_int_equal(state_2.es_lhe, epoch_2 + 1);
	assert_int_equal(state_2.es_ghce, state_2.es_hce);
	assert_int_equal(state_2.es_glre, state_2.es_lre);
	assert_int_equal(state_2.es_ghpce, state_1.es_hce);

	print_message("GHPCE shall not decrease when coh_1 is closed\n");
	MUST(cont_close(arg, coh_1));
	state_tmp = state_2;
	MUST(epoch_query(arg, coh_2, &state_2));
	/* The epoch state, especially GHPCE, shall not change. */
	assert_epoch_state_equal(&state_2, &state_tmp);

	print_message("GHCE shall catch up once coh_2 releases its hold\n");
	epoch_2 = 27; /* state_2.es_hce = GHCE = 25 and GHPCE = 30 */
	MUST(epoch_commit(arg, coh_2, epoch_2, &state_2));
	assert_int_equal(state_2.es_hce, epoch_2);
	assert_int_equal(state_2.es_lre, epoch_2);
	assert_int_equal(state_2.es_lhe, epoch_2 + 1);
	assert_int_equal(state_2.es_ghce, state_2.es_lhe - 1);
	assert_int_equal(state_2.es_glre, state_2.es_lre);
	assert_int_equal(state_2.es_ghpce, state_1.es_hce);

	print_message("GHCE shall catch up to GHPCE once coh_2 is closed\n");
	/* state_2.es_hce = GHCE = 27, state_2.es_lhe = 28, and GHPCE = 30 */
	MUST(cont_close(arg, coh_2));
	MUST(cont_open(arg, cont_uuid, DAOS_COO_RW, &coh_1));
	assert_int_equal(arg->co_info.ci_epoch_state.es_ghce, state_2.es_ghpce);
	MUST(cont_close(arg, coh_1));

	MUST(cont_destroy(arg, cont_uuid));
}

static void
test_epoch_hold_complex(void **varg)
{
	test_arg_t	       *arg = *varg;
	uuid_t			cont_uuid;
	daos_handle_t		coh_1;
	daos_handle_t		coh_2;
	daos_epoch_t		epoch_1;
	daos_epoch_t		epoch_2;
	daos_epoch_state_t	state_1;
	daos_epoch_state_t	state_2;

	MUST(cont_create(arg, cont_uuid));
	MUST(cont_open(arg, cont_uuid, DAOS_COO_RW, &coh_1));
	MUST(cont_open(arg, cont_uuid, DAOS_COO_RW, &coh_2));

	epoch_1 = 10;
	MUST(epoch_hold(arg, coh_1, &epoch_1, &state_1));
	MUST(epoch_commit(arg, coh_1, epoch_1, &state_1));
	assert_int_equal(state_1.es_ghpce, epoch_1);

	print_message("holding zero shall succeed with LHE = GHPCE + 1");
	epoch_2 = 0; /* GHPCE = 10 */
	MUST(epoch_hold(arg, coh_2, &epoch_2, &state_2));
	assert_int_equal(epoch_2, state_1.es_ghpce + 1);
	assert_int_equal(state_2.es_lhe, state_1.es_ghpce + 1);

	epoch_2 = DAOS_EPOCH_MAX;
	MUST(epoch_hold(arg, coh_2, &epoch_2, &state_2));
	assert_int_equal(epoch_2, DAOS_EPOCH_MAX);

	print_message("holding (0, GHPCE] shall succeed with LHE = GHPCE + 1");
	epoch_2 = 5; /* GHPCE = 10 */
	MUST(epoch_hold(arg, coh_2, &epoch_2, &state_2));
	assert_int_equal(epoch_2, state_1.es_ghpce + 1);
	assert_int_equal(state_2.es_lhe, state_1.es_ghpce + 1);

	MUST(cont_close(arg, coh_2));
	MUST(cont_close(arg, coh_1));
	MUST(cont_destroy(arg, cont_uuid));
}

static const struct CMUnitTest epoch_tests[] = {
	{ "EPOCH1: initial state when opening a new container",
	  test_epoch_init, async_disable, test_case_teardown},
	{ "EPOCH2: epoch_query",
	  test_epoch_query, async_disable, test_case_teardown},
	{ "EPOCH3: epoch_query (async)",
	  test_epoch_query, async_enable, test_case_teardown},
	{ "EPOCH4: epoch_hold",
	  test_epoch_hold, async_disable, test_case_teardown},
	{ "EPOCH5: epoch_hold (async)",
	  test_epoch_hold, async_enable, test_case_teardown},
	{ "EPOCH6: epoch_commit",
	  test_epoch_commit, async_disable, test_case_teardown},
	{ "EPOCH7: epoch_commit (async)",
	  test_epoch_commit, async_enable, test_case_teardown},
	{ "EPOCH8: epoch_slip",
	  test_epoch_slip, async_disable, test_case_teardown},
	{ "EPOCH9: epoch_slip (async)",
	  test_epoch_slip, async_enable, test_case_teardown},
	{ "EPOCH10: epoch_slip (overlap)",
	  test_epoch_slip, async_overlap, test_case_teardown},
	{ "EPOCH11: epoch_commit complex (multiple writers)",
	  test_epoch_commit_complex, async_disable, test_case_teardown},
	{ "EPOCH12: epoch_hold complex (multiple writers)",
	  test_epoch_hold_complex, async_disable, test_case_teardown}
};

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CONNECT, false, DEFAULT_POOL_SIZE);
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
