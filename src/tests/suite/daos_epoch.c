/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * Epoch Tests
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_test.h"
#include "daos_iotest.h"

#define MUST(rc)	assert_int_equal(rc, 0)
#define VAL_FMT		"VALUE-%lu"
#define REC_MAX_LEN	32

static void
io_for_aggregation(test_arg_t *arg, daos_handle_t coh, daos_handle_t ths[],
		   int gs_dkeys, daos_obj_id_t oid, bool update, int *snaps_in,
		   daos_epoch_t *snaps, char *verify_data)
{
	struct ioreq		req;
	int			i, k;
	const char		akey[] = "slip akey";
	char			dkey[] = "slip dkey";
	char			verify_buf[REC_MAX_LEN];
	char			rec[REC_MAX_LEN], *rec_verify;

	ioreq_init(&req, coh, oid, DAOS_IOD_SINGLE, arg);
	if (update && !arg->myrank)
		print_message("Inserting %d keys...\n", gs_dkeys);

	if (update) {
		daos_event_t ev;

		if (snaps_in && arg->async)
			MUST(daos_event_init(&ev, arg->eq, NULL));

		for (i = 0, k = 0; i < gs_dkeys; i++) {
			daos_size_t		rec_size;

			MUST(daos_tx_open(coh, &ths[i], 0, NULL));
			memset(rec, 0, REC_MAX_LEN);
			snprintf(rec, REC_MAX_LEN, VAL_FMT, (unsigned long)i);
			rec_size = strnlen(rec, REC_MAX_LEN);
			D_DEBUG(DF_MISC, "  d-key[%d] '%s' val '%d %s'\n",
				i, dkey, (int)rec_size, rec);
			insert_single(dkey, akey, 1100, rec, rec_size, ths[i],
				      &req);

			MUST(daos_tx_commit(ths[i], NULL));
			if (snaps_in && i == snaps_in[k]) {
				MUST(daos_cont_create_snap(coh, &snaps[k++],
							   NULL, arg->async ?
							   &ev : NULL));
				WAIT_ON_ASYNC(arg, ev);
			}
		}

		if (snaps_in && arg->async)
			MUST(daos_event_fini(&ev));
	}

	/* Don't verify if snapshots were created*/
	if (snaps_in)
		return;

	if (verify_data == NULL || strnlen(verify_data, REC_MAX_LEN) != 0)
		print_message("Check valid records (%d)\n", gs_dkeys);
	else
		print_message("Check empty records (%d)\n", gs_dkeys);

	/**
	 * If verification data is provided, check every record against it;
	 * else verify against generated records
	 **/
	rec_verify = verify_data != NULL ?  verify_data : verify_buf;

	for (i = 0, k = 0; i < gs_dkeys; i++) {
		daos_epoch_t	epoch;
		daos_handle_t	th;

		memset(rec, 0, REC_MAX_LEN);
		if (verify_data == NULL)
			snprintf(rec_verify, REC_MAX_LEN, VAL_FMT,
				 (unsigned long)i);
		MUST(daos_tx_hdl2epoch(ths[i], &epoch));
		/*
		 * daos_tx_open_snap should only open epochs of actual
		 * snapshots. We are violating this rule for testing purposes.
		 */
		MUST(daos_tx_open_snap(coh, epoch, &th, NULL));
		lookup_single(dkey, akey, 1100, rec, REC_MAX_LEN, th, &req);
		assert_int_equal(req.iod[0].iod_size,
				 strnlen(rec_verify, REC_MAX_LEN));
		assert_memory_equal(rec, rec_verify, req.iod[0].iod_size);
		MUST(daos_tx_close(th, NULL));
	}
}

static int
cont_create(test_arg_t *arg, uuid_t uuid)
{
	uuid_generate(uuid);
	print_message("creating container "DF_UUIDF"\n", DP_UUID(uuid));
	return daos_cont_create(arg->pool.poh, uuid, NULL, NULL);
}

static int
cont_destroy(test_arg_t *arg, const uuid_t uuid)
{
	print_message("destroying container "DF_UUID"\n", DP_UUID(uuid));
	return daos_cont_destroy(arg->pool.poh, uuid, 1, NULL);
}

static int
cont_open(test_arg_t *arg, const uuid_t uuid, unsigned int flags,
	  daos_handle_t *coh)
{
	print_message("opening container "DF_UUIDF" (flags=%X)\n",
		      DP_UUID(uuid), flags);
	return daos_cont_open(arg->pool.poh, uuid, flags, coh, &arg->co_info,
			      NULL);
}

static int
cont_close(test_arg_t *arg, daos_handle_t coh)
{
	print_message("closing container\n");
	return daos_cont_close(coh, NULL);
}

static void
test_epoch_aggregate(void **argp)
{
	test_arg_t		*arg = *argp;
	uuid_t			cont_uuid;
	daos_handle_t		coh;
	daos_handle_t		*ths = NULL;
	daos_obj_id_t		oid;
	daos_epoch_t		epoch, epc_hi = 0;
	int			i;

	MUST(cont_create(arg, cont_uuid));
	MUST(cont_open(arg, cont_uuid, DAOS_COO_RW, &coh));

	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	print_message("OID: "DF_OID"\n", DP_OID(oid));

	D_ALLOC_ARRAY(ths, 100);
	assert_non_null(ths);

	io_for_aggregation(arg, coh, ths, 100, oid,
			   /* update */ true, NULL, NULL,
			   /* verification data */ NULL);

	for (i = 0 ; i < 100; i++) {
		MUST(daos_tx_hdl2epoch(ths[i], &epoch));
		if (epc_hi < epoch)
			epc_hi = epoch;
		MUST(daos_tx_close(ths[i], NULL));
	}

	/* Trigger aggregation to epc_hi */
	print_message("Aggregate to epoch: "DF_U64"\n", epc_hi);
	MUST(daos_cont_aggregate(coh, epc_hi, NULL));

#if 0
	if (arg->overlap) {
		daos_tx_commit(ths[15], NULL);
		io_for_aggregation(arg, coh, ths, 15, oid,
				   /* update */true, NULL, NULL,
				   /* verification data */ NULL);
		for (i = 0 ; i < 15; i++)
			daos_tx_close(ths[i], NULL);
	}
#endif

	/*
	 * TODO: Monitor aggregation progress and wait for completion, then
	 * verify the aggregated result.
	 */
	sleep(10);

	D_FREE(ths);
	MUST(cont_close(arg, coh));
	MUST(cont_destroy(arg, cont_uuid));
}

static void
test_snapshots(void **argp)
{
	test_arg_t	       *arg = *argp;
	uuid_t			co_uuid;
	daos_handle_t		coh;
	daos_event_t		ev;
	daos_obj_id_t		oid;
	int			i, num_records = 100;
	daos_epoch_t		garbage	   = 0xAAAAAAAAAAAAAAAAL;
	int			snaps_in[] = { 21, 29, 35, 47, 57, 78, 81,
					       10000 /* prevent overflow */ };
	const int		snap_count = ARRAY_SIZE(snaps_in) - 1;
	daos_epoch_range_t	epr;
	int			snap_count_out;
	int			snap_split_index = snap_count/2;
	daos_epoch_t		snaps[snap_count];
	daos_epoch_t		snaps_out[snap_count];
	daos_handle_t		*ths = NULL;
	daos_anchor_t		anchor;

	MUST(cont_create(arg, co_uuid));
	MUST(cont_open(arg, co_uuid, DAOS_COO_RW | DAOS_COO_NOSLIP, &coh));

	oid = dts_oid_gen(OC_RP_XSF, 0, arg->myrank);
	print_message("OID: "DF_OID"\n", DP_OID(oid));

	D_ALLOC_ARRAY(ths, num_records);
	assert_non_null(ths);

	io_for_aggregation(arg, coh, ths, num_records, oid,
			   /* update */ true, snaps_in, snaps,
			   /* verification data */ NULL);

	if (arg->async)
		MUST(daos_event_init(&ev, arg->eq, NULL));

	print_message("Snapshot listing shall succeed with no buffer\n");
	snap_count_out = 0;
	memset(&anchor, 0, sizeof(anchor));
	MUST(daos_cont_list_snap(coh, &snap_count_out, NULL, NULL, &anchor,
				 arg->async ? &ev : NULL));
	WAIT_ON_ASYNC(arg, ev);
	daos_anchor_is_eof(&anchor);
	assert_int_equal(snap_count_out, snap_count);

	print_message("Snapshot listing shall succeed with a small buffer\n");
	snap_count_out = snap_split_index;
	memset(snaps_out, 0xAA, snap_count * sizeof(daos_epoch_t));
	memset(&anchor, 0, sizeof(anchor));
	MUST(daos_cont_list_snap(coh, &snap_count_out, snaps_out, NULL, &anchor,
				 arg->async ? &ev : NULL));
	WAIT_ON_ASYNC(arg, ev);
	daos_anchor_is_eof(&anchor);
	assert_int_equal(snap_count_out, snap_count);
	for (i = 0; i < snap_split_index; i++)
		assert_int_equal(snaps_out[i], snaps[i]);
	for (i = snap_split_index; i < snap_count; i++)
		assert_int_equal(snaps_out[i], garbage);

	print_message("Snapshot listing shall succeed with a large buffer\n");
	snap_count_out = snap_count;
	memset(snaps_out, 0xAA, snap_count * sizeof(daos_epoch_t));
	memset(&anchor, 0, sizeof(anchor));
	MUST(daos_cont_list_snap(coh, &snap_count_out, snaps_out, NULL, &anchor,
				 arg->async ? &ev : NULL));
	WAIT_ON_ASYNC(arg, ev);
	daos_anchor_is_eof(&anchor);
	assert_int_equal(snap_count_out, snap_count);
	for (i = 0; i < snap_count; i++)
		assert_int_not_equal(snaps_out[i], garbage);

	/*
	 * FIXME: I'm not able to understand following testing code, let's just
	 * disable it for this moment. All the test cases in this file needs be
	 * reviewed & rewritten once snapshot feature is completed. (IO barrier
	 * needs be introduced to ensure immutable snapshot).
	 */
#if 0
	last_snap = 0;
	memset(buf_verify, 0, REC_MAX_LEN);
	for (i = 0; i < snap_count; ++i) {
		daos_epoch_t epoch;

		print_message("Snapshots: checking epochs %d to %d\n",
			      last_snap, snaps_in[i]);
		io_for_aggregation(arg, coh, &ths[last_snap],
				   snaps_in[i] - last_snap, oid,
				   /* update */ false, NULL, NULL,
				   /* verification data */ buf_verify);
		last_snap = snaps_in[i];
		daos_tx_hdl2epoch(ths[last_snap], &epoch);
		snprintf(buf_verify, REC_MAX_LEN, VAL_FMT, epoch);
	}

	/** no-empty records from 82 -> 99 */
	print_message("Checking non-aggregated epochs %d to %d\n",
		      last_snap + 1, num_records - 1);
	io_for_aggregation(arg, coh, &ths[last_snap + 1],
			   num_records - last_snap - 1, oid,
			   /* update */ false, NULL, NULL,
			   /* verification data */ NULL);
#endif
	print_message("Snapshot deletion shall succeed\n");
	epr.epr_hi = epr.epr_lo = snaps[2];
	MUST(daos_cont_destroy_snap(coh, epr, arg->async ? &ev : NULL));
	WAIT_ON_ASYNC(arg, ev);

	if (arg->async)
		MUST(daos_event_fini(&ev));
	for (i = 0 ; i < 100; i++)
		daos_tx_close(ths[i], NULL);

	D_FREE(ths);
	MUST(cont_close(arg, coh));
	MUST(cont_destroy(arg, co_uuid));
}

static const struct CMUnitTest epoch_tests[] = {
	{ "EPOCH1: epoch_aggregate",
	  test_epoch_aggregate, async_disable, test_case_teardown},
	{ "EPOCH2: epoch_aggregate (async)",
	  test_epoch_aggregate, async_enable, test_case_teardown},
#if 0
	{ "EPOCH3: epoch_aggregate (overlap)",
	  test_epoch_aggregate, async_overlap, test_case_teardown},
#endif
	{ "EPOCH4: snapshots",
	  test_snapshots, async_disable, test_case_teardown},
	{ "EPOCH5: snapshots (async)",
	  test_snapshots, async_enable, test_case_teardown},
};

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CONNECT, false, DEFAULT_POOL_SIZE,
			  0, NULL);
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
