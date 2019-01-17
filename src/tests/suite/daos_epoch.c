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
/*
 * Epoch Tests
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_test.h"
#include "daos_iotest.h"

#define MUST(rc) assert_int_equal(rc, 0)

static void
io_for_aggregation(test_arg_t *arg, daos_handle_t coh, daos_handle_t ths[],
		   int gs_dkeys, daos_obj_id_t oid, bool update, int *snaps_in,
		   daos_epoch_t *snaps, bool verify_empty)
{
	struct ioreq		req;
	int			i, g_dkeys_strlen = 16;
	const char		akey[] = "slip akey";
	char			dkey[] = "slip dkey";
	char			*rec, *val, *rec_verify;
	const char		*val_fmt = "slip val%d";
	daos_size_t		val_size, rec_size;
	daos_epoch_t		epoch;

	if (verify_empty)
		print_message("Check empty records (%d)\n", gs_dkeys);
	else
		print_message("Check valid records (%d)\n", gs_dkeys);

	ioreq_init(&req, coh, oid, DAOS_IOD_SINGLE, arg);
	if (update && !arg->myrank)
		print_message("Inserting %d keys...\n", gs_dkeys);

	D_ALLOC(rec, strlen(val_fmt) + g_dkeys_strlen + 1);
	assert_non_null(rec);
	D_ALLOC(val, 64);
	assert_non_null(val);
	val_size = 64;

	if (update) {
		daos_event_t	ev;
		int		k = 0;

		if (snaps_in && arg->async)
			MUST(daos_event_init(&ev, arg->eq, NULL));

		for (i = 0; i < gs_dkeys; i++) {
			daos_tx_open(arg->coh, &ths[i], NULL);
			daos_tx_hdl2epoch(ths[i], &epoch);
			memset(rec, 0, (strlen(val_fmt) + g_dkeys_strlen + 1));
			sprintf(rec, val_fmt, epoch);
			rec_size = strlen(rec);
			D_DEBUG(DF_MISC, "  d-key[%d] '%s' val '%d %s'\n", i,
				dkey, (int)rec_size, rec);
			insert_single(dkey, akey, 1100, rec, rec_size, ths[i],
				      &req);

			if (snaps_in && i == snaps_in[k]) {
				MUST(daos_cont_create_snap(coh, &snaps[k++],
						   NULL,
						   arg->async ? &ev : NULL));
				WAIT_ON_ASYNC(arg, ev);
			}
		}

		if (snaps_in && arg->async)
			MUST(daos_event_fini(&ev));
	}

	D_ALLOC(rec_verify, strlen(val_fmt) + g_dkeys_strlen + 1);
	for (i = 0; i < gs_dkeys; i++) {
		memset(rec_verify, 0, (strlen(val_fmt) + g_dkeys_strlen + 1));
		memset(val, 0, 64);
		if (!verify_empty) {
			daos_tx_hdl2epoch(ths[i], &epoch);
			sprintf(rec_verify, val_fmt, epoch);
			lookup_single(dkey, akey, 1100, val, val_size, ths[i],
				      &req);
		} else {
			lookup_empty_single(dkey, akey, 1100, val, val_size,
					    ths[i], &req);
		}
		assert_int_equal(req.iod[0].iod_size, strlen(rec_verify));
		assert_memory_equal(val, rec_verify, req.iod[0].iod_size);
	}

	D_FREE(val);
	D_FREE(rec_verify);
	D_FREE(rec);
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

static int
cont_query(test_arg_t *arg, daos_handle_t coh, daos_cont_info_t *info)
{
	print_message(".");
	return daos_cont_query(coh, info, NULL, NULL);
}

static void
test_epoch_slip(void **argp)
{
	test_arg_t		*arg = *argp;
	uuid_t			cont_uuid;
	daos_handle_t		coh;
	daos_handle_t		*ths = NULL;
	daos_cont_info_t	info;
	daos_obj_id_t		oid;
	int			i;

	MUST(cont_create(arg, cont_uuid));
	MUST(cont_open(arg, cont_uuid, DAOS_COO_RW, &coh));

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, 0, arg->myrank);
	print_message("OID: "DF_OID"\n", DP_OID(oid));

	D_ALLOC_ARRAY(ths, 100);
	assert_non_null(ths);

	io_for_aggregation(arg, coh, ths, 100, oid,
			   /* update */ true, NULL, NULL,
			   /* verify empty record */ false);

	memset(&info, 0, sizeof(daos_cont_info_t));
	print_message("Verfying aggregated epoch before slip.. ");
	MUST(cont_query(arg, coh, &info));
	/** aggregated epoch before aggregation */
	/** TODO - add check when aggregation in commit is enabled */
	memset(&info, 0, sizeof(daos_cont_info_t));

	for (i = 0 ; i < 100; i++)
		daos_tx_close(ths[i], NULL);

	if (arg->overlap) {
		io_for_aggregation(arg, coh, ths, 15, oid,
				   /* update */true, NULL, NULL,
				   /* verify empty record */false);

		for (i = 0 ; i < 15; i++)
			daos_tx_close(ths[i], NULL);
	}

	/** Wait for aggregation completion */
	/** TODO - add check when aggregation in commit is enabled */

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
	int			i;
	daos_epoch_t		garbage	   = 0xAAAAAAAAAAAAAAAAL;
	int			snaps_in[] = { 21, 29, 35, 47, 57, 78, 81 };
	const int		snap_count = ARRAY_SIZE(snaps_in);
	daos_epoch_range_t	epr;
	int			snap_count_out;
	int			snap_split_index = snap_count/2;
	daos_epoch_t		snaps[snap_count];
	daos_epoch_t		snaps_out[snap_count];
	daos_handle_t		*ths = NULL;
	daos_anchor_t		anchor;

	MUST(cont_create(arg, co_uuid));
	MUST(cont_open(arg, co_uuid, DAOS_COO_RW | DAOS_COO_NOSLIP, &coh));

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, 0, arg->myrank);
	print_message("OID: "DF_OID"\n", DP_OID(oid));

	D_ALLOC_ARRAY(ths, 100);
	assert_non_null(ths);

	io_for_aggregation(arg, coh, ths, 100, oid,
			   /* update */ true, snaps_in, snaps,
			   /* verify empty record */ false);

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
		assert_int_equal(snaps_out[i], snaps[i]);

	/** no-empty records at 21 */
	io_for_aggregation(arg, coh, &ths[21], 1, oid,
			   /* update */false, NULL, NULL,
			   /* verify empty record */false);

	/** no-empty records at 29 */
	io_for_aggregation(arg, coh, &ths[29], 1, oid,
			   /* update */false, NULL, NULL,
			   /* verify empty record */false);

	/** no-empty records at 35 */
	io_for_aggregation(arg, coh, &ths[35], 1, oid,
			   /* update */false, NULL, NULL,
			   /* verify empty record */false);

	/** no-empty records from 40 -> 99 */
	io_for_aggregation(arg, coh, &ths[40], 60, oid,
			   /* update */false, NULL, NULL,
			   /* verify empty record */false);

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
	{ "EPOCH1: epoch_slip",
	  test_epoch_slip, async_disable, test_case_teardown},
	{ "EPOCH2: epoch_slip (async)",
	  test_epoch_slip, async_enable, test_case_teardown},
	{ "EPOCH3: epoch_slip (overlap)",
	  test_epoch_slip, async_overlap, test_case_teardown},
	{ "EPOCH4: snapshots",
	  test_snapshots, async_disable, test_case_teardown},
	{ "EPOCH5: snapshots (async)",
	  test_snapshots, async_enable, test_case_teardown},
};

static int
setup(void **state)
{
	return test_setup(state, SETUP_POOL_CONNECT, false, DEFAULT_POOL_SIZE,
			  NULL);
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
